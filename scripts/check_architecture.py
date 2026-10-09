#!/usr/bin/env python3
"""Check the bounded project include graph; DuckDB/vendor transitive graphs are not expanded."""

import argparse
from collections import deque
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {'.cpp', '.cc', '.hpp', '.h'}
HEADER_SUFFIXES = {'.hpp', '.h'}
FUNCTION_HEADERS = {'state.hpp', 'functions.hpp', 'describe_functions.hpp'}
MODEL_HEADERS = {'model.hpp', 'parser.hpp', 'catalog.hpp', 'dialect.hpp', 'graph.hpp', 'validate.hpp', 'yaml.hpp'}
# These two implementation files contain adapters as well as pure helpers.
# Exceptions permit only these direct external edges, never state/functions dependencies.
ADAPTER_EDGES = {
    'src/model/parser.cpp': {'duckdb/common/file_system.hpp', 'duckdb/main/client_context.hpp'},
    'src/model/catalog.cpp': {
        'duckdb/catalog/catalog.hpp',
        'duckdb/catalog/entry_lookup_info.hpp',
        'duckdb/main/client_context.hpp',
    },
}


def uncomment(text):
    # Retain quoted strings and newlines; commented-out includes do not create edges.
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/'
    return re.sub(pattern, lambda m: re.sub(r'[^\n]', ' ', m[0]) if m[0].startswith(('//', '/*')) else m[0], text)


def includes(text):
    for line in uncomment(text.replace('\\\n', '')).splitlines():
        directive = re.match(r'^\s*#\s*include\s+(.+?)\s*$', line)
        if not directive:
            continue
        literal = re.fullmatch(r'"([^"\n]+)"|<([^>\n]+)>', directive[1])
        if not literal:
            raise ValueError(f'nonliteral include cannot be checked: {directive[1]}')
        yield literal[1] or literal[2], literal[1] is not None


def is_function(node):
    return (
        node.startswith('src/functions/')
        or node in {'src/ossie_extension.cpp', 'src/include/ossie_extension.hpp'}
        or (node.startswith('src/include/ossie/') and Path(node).name in FUNCTION_HEADERS)
    )


def is_compile(node):
    return node.startswith('src/compile/') or node == 'src/include/ossie/compile.hpp'


def is_model(node):
    return node.startswith('src/model/') or (node.startswith('src/include/ossie/') and Path(node).name in MODEL_HEADERS)


def runtime_header(header):
    return (
        header.startswith(('duckdb/main/', 'duckdb/catalog/', 'duckdb/storage/'))
        or header == 'duckdb/common/file_system.hpp'
    )


def path_to(graph, start, predicate):
    queue = deque([(start, [start])])
    seen = {start}
    while queue:
        node, path = queue.popleft()
        if node != start and predicate(node):
            return path
        for target in sorted(graph[node]):
            if target not in seen:
                seen.add(target)
                queue.append((target, path + [target]))
    return None


def cycle_in_headers(graph):
    headers = {node for node in graph if Path(node).suffix in HEADER_SUFFIXES}
    complete, active = set(), []

    def visit(node):
        if node in active:
            return active[active.index(node) :] + [node]
        if node in complete:
            return None
        active.append(node)
        for child in sorted(graph[node] & headers):
            cycle = visit(child)
            if cycle:
                return cycle
        active.pop()
        complete.add(node)
        return None

    for node in sorted(headers):
        cycle = visit(node)
        if cycle:
            return cycle
    return None


def project_reference(header, quoted):
    if header.startswith(('ossie/', '../', './')):
        return True
    if header.startswith(('duckdb/', 'fkYAML/')) or header in {
        'duckdb.hpp',
        'yyjson.hpp',
        'stdint.h',
        'stddef.h',
        'stdlib.h',
        'stdio.h',
        'string.h',
        'math.h',
    }:
        return False
    return quoted or header.endswith(('.hpp', '.h'))


def check(root):
    root = root.resolve()
    files = sorted(path for path in (root / 'src').rglob('*') if path.is_file() and path.suffix in SOURCE_SUFFIXES)
    if not files:
        raise ValueError('zero project source/header inputs: refusing an empty check')
    nodes = {path.resolve(): path.relative_to(root).as_posix() for path in files}
    graph = {node: set() for node in nodes.values()}
    errors, external = [], []
    cmake = root / 'CMakeLists.txt'
    if cmake.exists():
        declared = re.search(r'set\(EXTENSION_SOURCES\s+([^)]*)\)', cmake.read_text(), re.S)
        if not declared:
            errors.append('EXTENSION_SOURCES inventory missing')
        else:
            listed = set(re.findall(r'src/[\w/.-]+\.(?:cpp|cc)', declared[1]))
            if not listed:
                errors.append('empty compiled source inventory')
            scanned = {node for node in graph if Path(node).suffix in {'.cpp', '.cc'}}
            if listed != scanned:
                errors.append(
                    f'compiled source inventory differs: unscanned={sorted(listed-scanned)}, unlisted={sorted(scanned-listed)}'
                )
    for source in files:
        node = nodes[source.resolve()]
        try:
            for header, quoted in includes(source.read_text()):
                candidates = [source.parent / header, root / 'src/include' / header, root / 'src' / header]
                target = next((nodes[p.resolve()] for p in candidates if p.resolve() in nodes), None)
                if target:
                    graph[node].add(target)
                elif project_reference(header, quoted):
                    errors.append(f'{node}: unresolved project include {header}')
                else:
                    external.append((node, header))
        except ValueError as error:
            errors.append(f'{node}: {error}')
    for node in sorted(graph):
        if is_compile(node) or is_model(node):
            path = path_to(graph, node, is_function)
            if path:
                errors.append('forbidden state/functions dependency: ' + ' -> '.join(path))
            # Runtime includes are checked at every reachable project header, too.
            reachable = {node}
            queue = deque([node])
            while queue:
                for child in graph[queue.popleft()]:
                    if child not in reachable:
                        reachable.add(child)
                        queue.append(child)
            for owner, header in external:
                if owner in reachable and runtime_header(header) and header not in ADAPTER_EDGES.get(owner, set()):
                    path = [node] if owner == node else path_to(graph, node, lambda candidate: candidate == owner)
                    errors.append('forbidden runtime include: ' + ' -> '.join(path + [header]))
    cycle = cycle_in_headers(graph)
    if cycle:
        errors.append('project header cycle: ' + ' -> '.join(cycle))
    if errors:
        raise ValueError('\n'.join(errors))
    return {
        'scope': 'all project src C++ files; project include reachability and header cycles; direct runtime includes; no vendor expansion',
        'source_files': len(files),
        'project_edges': sum(len(edges) for edges in graph.values()),
        'external_edges_unexpanded': len(external),
        'adapter_exceptions': ADAPTER_EDGES,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    try:
        report = check(args.root)
    except (ValueError, OSError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
    # Sets in the precise exception table are serialized explicitly.
    report['adapter_exceptions'] = {owner: sorted(edges) for owner, edges in ADAPTER_EDGES.items()}
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
