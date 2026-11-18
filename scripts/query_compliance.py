#!/usr/bin/env python3
"""Measure the built extension against apache/ossie's query compliance cases.

The cases are written for the Relational Query Interface: SQL over a model's relations, with AGG()
evaluating a measure. Each case is measured twice, against the rows of its expected.sql.

As written: its query.ossie.sql, unchanged, handed to ossie_sql.

As a request: whether its question can be asked as one ossie_query request, the Wide Table interface.
The requests are in test/fixtures/conformance/query_requests.sql, written here.

Every case gets one status of each kind:

  pass        the rows and the column names match
  names       the rows match, the column names differ
  wrong       the rows differ
  refused     the query, or the request, is refused
  no-request  no single ossie_query request asks the question (as a request only)

Each case's reference.duckdb.sql is also run and compared with its expected.sql. A difference there
means the vendored suite and this DuckDB disagree, which no status of ours describes: exit 2.

Usage: scripts/query_compliance.py [--duckdb CLI] [--check | --update] [-v]
Exit:  0; 1 when --check finds a status that differs from the pinned one; 2 when the suite is broken.
"""
import argparse
import json
import subprocess
import sys
from decimal import Decimal

sys.dont_write_bytecode = True
from wide_table_compliance import EXTENSION, NUMERIC_TYPES, REPO, SHELL, same_rows, same_value, sql_string

SUITE = REPO / "test/fixtures/conformance/compliance/query"
REQUESTS = REPO / "test/fixtures/conformance/query_requests.sql"
PINNED = REPO / "test/fixtures/conformance/query_status.tsv"
APPROXIMATE_TYPES = {"FLOAT", "DOUBLE"}


def statement(text):
    """The one statement of a file or section, without comment lines and the closing semicolon."""
    lines = [line for line in text.splitlines() if line.strip() and not line.startswith("--")]
    return "\n".join(lines).strip().rstrip(";")


def read_requests():
    """query_requests.sql: `-- case: NAME`, then a statement or a `-- none: REASON` line."""
    requests = {}
    case = None
    for line in REQUESTS.read_text().splitlines():
        if line.startswith("-- case:"):
            case = line.split(":", 1)[1].strip()
            requests[case] = {"sql": [], "none": ""}
        elif case and line.startswith("-- none:"):
            requests[case]["none"] = line.split(":", 1)[1].strip()
        elif case:
            requests[case]["sql"].append(line)
    return {name: (statement("\n".join(entry["sql"])), entry["none"]) for name, entry in requests.items()}


def run(shell, queries):
    """Run the queries in order in one CLI. Returns a (names, types, rows) per query that ran, and the error."""
    script = [
        ".bail on",
        "LOAD %s;" % sql_string(str(EXTENSION)),
        ".output /dev/null",
        ".read %s" % (SUITE / "scott/scott.sql"),
        "CALL ossie_load(%s, left_joins => true);" % sql_string(str(SUITE / "scott/scott_model.ossie.yaml")),
        ".output",
        ".mode json",
    ]
    for query in queries:
        script += ["DESCRIBE %s;" % query, "%s;" % query]
    done = subprocess.run([shell, "-unsigned"], input="\n".join(script), capture_output=True, text=True)
    decoder = json.JSONDecoder(parse_float=Decimal, object_pairs_hook=list)
    rest = done.stdout.lstrip()
    documents = []
    while rest:
        document, end = decoder.raw_decode(rest)
        documents.append(document)
        rest = rest[end:].lstrip()
    results = []
    for described, records in zip(documents[0::2], documents[1::2]):
        columns = [dict(column) for column in described]
        types = [column["column_type"].split("(")[0] for column in columns]
        rows = []
        for record in records:
            values = [value for _, value in record]
            rows.append([Decimal(v) if t in NUMERIC_TYPES and isinstance(v, str) else v for v, t in zip(values, types)])
        results.append(([column["column_name"] for column in columns], types, rows))
    error = " ".join(done.stderr.split("LINE 1:")[0].split()) if done.returncode != 0 else None
    return results, error


def same_result(actual, expected, ordered):
    """The suite's README, "Comparing results": by position, in order when the query orders, else as multisets."""
    _, _, rows = actual
    _, types, want = expected
    approximate = [name in APPROXIMATE_TYPES for name in types]
    if not ordered:
        return same_rows(rows, want, approximate)
    if len(rows) != len(want):
        return False
    for row, wanted in zip(rows, want):
        if len(row) != len(wanted) or not all(same_value(a, e, loose) for a, e, loose in zip(row, wanted, approximate)):
            return False
    return True


def show(rows):
    return json.dumps(rows, default=lambda number: float(number) if number % 1 else int(number))


def judge(actual, expected, ordered):
    """The status of a result against the expected one, and a detail to print."""
    if not same_result(actual, expected, ordered):
        return "wrong", show(actual[2])
    if [name.lower() for name in actual[0]] != [name.lower() for name in expected[0]]:
        return "names", ", ".join(actual[0])
    return "pass", ""


def classify(shell, case, request):
    """Returns (status, detail) as written and as a request, and whether the suite's own reference query failed."""
    written = statement((case / "query.ossie.sql").read_text())
    # An ORDER BY inside a subquery (b03) never stands alone: every such case orders its result too.
    ordered = "ORDER BY" in written
    queries = [statement((case / "expected.sql").read_text()), statement((case / "reference.duckdb.sql").read_text())]
    results, error = run(shell, queries + ["SELECT * FROM ossie_sql(%s)" % sql_string(written)])
    if len(results) < 2:
        detail = error or "the suite's own queries did not run"
        return ("refused", detail), ("no-request", detail), True
    expected, reference = results[0], results[1]
    broken = not same_result(reference, expected, ordered)
    as_written = judge(results[2], expected, ordered) if len(results) > 2 else ("refused", error or "")
    if not request:
        return as_written, ("no-request", ""), broken
    results, error = run(shell, [request])
    as_request = judge(results[0], expected, ordered) if results else ("refused", error or "")
    return as_written, as_request, broken


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--duckdb", default=str(SHELL), help="duckdb CLI to drive (default: the build tree's shell)")
    parser.add_argument("--check", action="store_true", help="fail when a status differs from the pinned one")
    parser.add_argument("--update", action="store_true", help="pin the statuses of this run")
    parser.add_argument("-v", "--verbose", action="store_true", help="print what a request returned, or why none")
    options = parser.parse_args()

    cases = sorted(path for path in (SUITE / "cases").iterdir() if path.is_dir())
    requests = read_requests()
    if set(requests) != set(case.name for case in cases):
        print("query_requests.sql and the cases differ: %s" % sorted(set(requests) ^ set(case.name for case in cases)))
        return 2

    written = {}
    statuses = {}
    broken = []
    for case in cases:
        request, reason = requests[case.name]
        if request and (request.count("ossie_query(") != 1 or "scott." in request):
            print("%s: a request is one ossie_query call and reads no table itself" % case.name)
            return 2
        as_written, as_request, failed = classify(options.duckdb, case, request)
        written[case.name] = as_written[0]
        statuses[case.name] = as_request[0]
        if failed:
            broken.append(case.name)
        print("%-8s %-11s %s" % (as_written[0], as_request[0], case.name))
        if options.verbose and as_written[1]:
            print("    as written: " + as_written[1])
        if options.verbose and (as_request[1] or reason):
            print("    as a request: " + (as_request[1] or reason))

    print()
    for label, column in (("as written", written), ("as requests", statuses)):
        counts = {}
        for status in column.values():
            counts[status] = counts.get(status, 0) + 1
        print("%d cases %s: %s" % (len(column), label, ", ".join("%s %d" % item for item in sorted(counts.items()))))
    print("%d of %d reference queries return their expected result" % (len(cases) - len(broken), len(cases)))
    if broken:
        print("BROKEN %s" % ", ".join(broken))
        return 2

    if options.update:
        PINNED.write_text("".join("%s\t%s\t%s\n" % (case, written[case], statuses[case]) for case in sorted(statuses)))
    if options.check:
        now = {case: "%s\t%s" % (written[case], statuses[case]) for case in statuses}
        pinned = dict(line.split("\t", 1) for line in PINNED.read_text().splitlines())
        changed = sorted(case for case in set(pinned) | set(now) if pinned.get(case) != now.get(case))
        for case in changed:
            print("CHANGED %s: pinned %s, now %s" % (case, pinned.get(case), now.get(case)))
        return 1 if changed else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
