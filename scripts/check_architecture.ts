/** Bounded project include graph; vendor transitive graphs are not expanded. */
import { existsSync, readFileSync, readdirSync, realpathSync } from "node:fs";
import { basename, dirname, extname, join, relative, resolve } from "node:path";

export const ROOT = resolve(import.meta.dir, "..");
const sourceSuffixes = new Set([".cpp", ".cc", ".hpp", ".h"]);
const headerSuffixes = new Set([".hpp", ".h"]);
const functionHeaders = new Set([
  "state.hpp",
  "functions.hpp",
  "describe_functions.hpp",
]);
const modelHeaders = new Set([
  "model.hpp",
  "parser.hpp",
  "catalog.hpp",
  "dialect.hpp",
  "graph.hpp",
  "validate.hpp",
  "yaml.hpp",
]);
// Exact implementation edges for the existing mixed pure-helper/loader adapters.
const adapterEdges: Record<string, string[]> = {
  "src/model/parser.cpp": [
    "duckdb/common/file_system.hpp",
    "duckdb/main/client_context.hpp",
  ],
  "src/model/catalog.cpp": [
    "duckdb/catalog/catalog.hpp",
    "duckdb/catalog/entry_lookup_info.hpp",
    "duckdb/main/client_context.hpp",
  ],
};
export function filesUnder(root: string): string[] {
  if (!existsSync(root)) return [];
  return readdirSync(root, { withFileTypes: true })
    .flatMap((entry) =>
      entry.isDirectory()
        ? filesUnder(join(root, entry.name))
        : entry.isFile()
          ? [join(root, entry.name)]
          : [],
    )
    .sort();
}
function includes(text: string): [string, boolean][] {
  const clean = text
    .replace(/\\\n/g, "")
    .replace(
      /"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\/[^\n]*|\/\*[\s\S]*?\*\//g,
      (match) =>
        match.startsWith("//") || match.startsWith("/*")
          ? match.replace(/[^\n]/g, " ")
          : match,
    );
  const result: [string, boolean][] = [];
  for (const line of clean.split(/\r?\n/)) {
    const directive = /^\s*#\s*include\s+(.+?)\s*$/.exec(line);
    if (!directive) continue;
    const literal = /^(?:"([^"\n]+)"|<([^>\n]+)>)$/.exec(directive[1]);
    if (!literal)
      throw new Error(`nonliteral include cannot be checked: ${directive[1]}`);
    result.push([literal[1] || literal[2], literal[1] !== undefined]);
  }
  return result;
}
type Graph = Map<string, Set<string>>;
function pathTo(
  graph: Graph,
  start: string,
  predicate: (node: string) => boolean,
): string[] | undefined {
  const queue = [[start]],
    seen = new Set([start]);
  for (let index = 0; index < queue.length; index++) {
    const path = queue[index],
      node = path.at(-1)!;
    if (node !== start && predicate(node)) return path;
    for (const child of [...graph.get(node)!].sort())
      if (!seen.has(child)) {
        seen.add(child);
        queue.push([...path, child]);
      }
  }
}
function headerCycle(graph: Graph): string[] | undefined {
  const headers = new Set(
    [...graph.keys()].filter((node) => headerSuffixes.has(extname(node))),
  );
  const complete = new Set<string>(),
    active: string[] = [];
  function visit(node: string): string[] | undefined {
    const index = active.indexOf(node);
    if (index >= 0) return [...active.slice(index), node];
    if (complete.has(node)) return;
    active.push(node);
    for (const child of [...graph.get(node)!]
      .filter((child) => headers.has(child))
      .sort()) {
      const cycle = visit(child);
      if (cycle) return cycle;
    }
    active.pop();
    complete.add(node);
  }
  for (const node of [...headers].sort()) {
    const cycle = visit(node);
    if (cycle) return cycle;
  }
}
function projectReference(header: string, quoted: boolean) {
  if (/^(ossie\/|\.\.\/|\.\/)/.test(header)) return true;
  if (
    /^(duckdb\/|fkYAML\/)/.test(header) ||
    [
      "duckdb.hpp",
      "yyjson.hpp",
      "stdint.h",
      "stddef.h",
      "stdlib.h",
      "stdio.h",
      "string.h",
      "math.h",
    ].includes(header)
  )
    return false;
  return quoted || /\.(hpp|h)$/.test(header);
}
export function check(root: string = ROOT) {
  root = realpathSync(root);
  const files = filesUnder(join(root, "src")).filter((path) =>
    sourceSuffixes.has(extname(path)),
  );
  if (!files.length)
    throw new Error(
      "zero project source/header inputs: refusing an empty check",
    );
  const nodes = new Map(
    files.map((path) => [
      realpathSync(path),
      relative(root, path).replaceAll("\\", "/"),
    ]),
  );
  const graph: Graph = new Map(
    [...nodes.values()].map((node) => [node, new Set()]),
  );
  const errors: string[] = [],
    external: [string, string][] = [];
  const cmake = join(root, "CMakeLists.txt");
  if (existsSync(cmake)) {
    const declared = /set\(EXTENSION_SOURCES\s+([^)]*)\)/s.exec(
      readFileSync(cmake, "utf8"),
    );
    if (!declared) errors.push("EXTENSION_SOURCES inventory missing");
    else {
      const listed = new Set(
        declared[1].match(/src\/[\w/.-]+\.(?:cpp|cc)/g) || [],
      );
      if (!listed.size) errors.push("empty compiled source inventory");
      const scanned = new Set(
        [...graph.keys()].filter((node) => /\.(cpp|cc)$/.test(node)),
      );
      const unscanned = [...listed].filter((node) => !scanned.has(node)).sort(),
        unlisted = [...scanned].filter((node) => !listed.has(node)).sort();
      if (unscanned.length || unlisted.length)
        errors.push(
          `compiled source inventory differs: unscanned=${JSON.stringify(unscanned)}, unlisted=${JSON.stringify(unlisted)}`,
        );
    }
  }
  for (const source of files) {
    const node = nodes.get(realpathSync(source))!;
    try {
      for (const [header, quoted] of includes(readFileSync(source, "utf8"))) {
        const target = [
          resolve(dirname(source), header),
          resolve(root, "src/include", header),
          resolve(root, "src", header),
        ]
          .map((path) =>
            existsSync(path) ? nodes.get(realpathSync(path)) : undefined,
          )
          .find(Boolean);
        if (target) graph.get(node)!.add(target);
        else if (projectReference(header, quoted))
          errors.push(`${node}: unresolved project include ${header}`);
        else external.push([node, header]);
      }
    } catch (error) {
      errors.push(`${node}: ${String(error)}`);
    }
  }
  const isFunction = (node: string) =>
    node.startsWith("src/functions/") ||
    ["src/ossie_extension.cpp", "src/include/ossie_extension.hpp"].includes(
      node,
    ) ||
    (node.startsWith("src/include/ossie/") &&
      functionHeaders.has(basename(node)));
  for (const node of [...graph.keys()].sort()) {
    if (
      !(
        node.startsWith("src/compile/") ||
        node === "src/include/ossie/compile.hpp" ||
        node.startsWith("src/model/") ||
        (node.startsWith("src/include/ossie/") &&
          modelHeaders.has(basename(node)))
      )
    )
      continue;
    const path = pathTo(graph, node, isFunction);
    if (path)
      errors.push("forbidden state/functions dependency: " + path.join(" -> "));
    for (const [owner, header] of external) {
      const route =
        owner === node
          ? [node]
          : pathTo(graph, node, (child) => child === owner);
      if (
        route &&
        (/^duckdb\/(main|catalog|storage)\//.test(header) ||
          header === "duckdb/common/file_system.hpp") &&
        !adapterEdges[owner]?.includes(header)
      )
        errors.push(
          "forbidden runtime include: " + [...route, header].join(" -> "),
        );
    }
  }
  const cycle = headerCycle(graph);
  if (cycle) errors.push("project header cycle: " + cycle.join(" -> "));
  if (errors.length) throw new Error(errors.join("\n"));
  return {
    scope:
      "all project src C++ files; project include reachability and header cycles; direct runtime includes; no vendor expansion",
    source_files: files.length,
    project_edges: [...graph.values()].reduce(
      (count, edges) => count + edges.size,
      0,
    ),
    external_edges_unexpanded: external.length,
    adapter_exceptions: adapterEdges,
  };
}
if (import.meta.main) {
  try {
    const args = process.argv.slice(2);
    if (args.length && (args[0] !== "--root" || args.length !== 2))
      throw new Error(
        "Usage: bun scripts/check_architecture.ts [--root DIRECTORY]",
      );
    console.log(JSON.stringify(check(args[1] || ROOT), null, 2));
  } catch (error) {
    console.error(`FAIL: ${String(error)}`);
    process.exitCode = 1;
  }
}
