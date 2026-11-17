#!/usr/bin/env python3
"""Run apache/ossie's wide-table compliance cases against the built extension.

Each case is a query in the JSON binding of the Wide Table Query Interface plus the rows or the
error it must produce. The case is translated to ossie_query(metrics, dimensions, filters), run in
a fresh duckdb CLI, and classified:

  pass              rows and output names match, or the refusal names the expected error code
  names             rows match, output names differ
  refused           refused as required, without the expected error code in the message
  wrong             rows differ
  false-refusal     an error where rows were expected
  false-acceptance  rows where an error was expected
  inexpressible     the query uses a part of the binding ossie_query has no argument for

Usage: scripts/wide_table_compliance.py [--duckdb CLI] [--joins left|inner] [--check | --update] [-v]
Exit:  0, or 1 when --check finds a status that differs from the pinned one.
"""
import argparse
import json
import subprocess
import sys
from decimal import Decimal
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SUITE = REPO / "test/fixtures/conformance/compliance/wide_table"
EXTENSION = REPO / "build/release/extension/ossie/ossie.duckdb_extension"
SHELL = REPO / "build/release/duckdb"
PINNED = REPO / "test/fixtures/conformance/wide_table_status.tsv"

FIXTURES = {
    "retail": ("retail/retail.sql", "retail/retail_model.ossie.yaml"),
    "stores": ("stores/stores.sql", "stores/stores_model.ossie.yaml"),
    "stores_ambiguous": ("stores/stores.sql", "stores/stores_ambiguous_model.ossie.yaml"),
}
QUERY_KEYS = {"dimensions", "metrics", "filter"}
# The shell's JSON mode writes HUGEINT and DECIMAL as strings, so the described type says which strings are numbers.
NUMERIC_TYPES = {"TINYINT", "SMALLINT", "INTEGER", "BIGINT", "HUGEINT", "UTINYINT", "USMALLINT", "UINTEGER"}
NUMERIC_TYPES |= {"UBIGINT", "UHUGEINT", "DECIMAL", "FLOAT", "DOUBLE"}


def sql_string(text):
    return "'" + text.replace("'", "''") + "'"


def sql_list(items):
    return "[" + ", ".join(sql_string(item) for item in items) + "]"


def translate(query):
    """Return (metrics, dimensions, filters), or the reason the query cannot be submitted."""
    unknown = sorted(set(query) - QUERY_KEYS)
    if unknown:
        return None, "key " + ", ".join(unknown)
    names = {"dimensions": [], "metrics": []}
    for part, key in (("dimensions", "field"), ("metrics", "metric")):
        for entry in query.get(part, []):
            if not isinstance(entry, str):
                return None, "alias"
            names[part].append(entry)
    filters = []
    if "filter" in query:
        if query["filter"] is None:
            return None, "null filter"
        filters.append(query["filter"])
    return (names["metrics"], names["dimensions"], filters), None


def run(shell, fixture, arguments, joins):
    data, model = FIXTURES[fixture]
    option = ", left_joins => true" if joins == "left" else ""
    load = "CALL ossie_load(%s%s);" % (sql_string(str(SUITE / model)), option)
    call = ", ".join(sql_list(argument) for argument in arguments)
    script = "\n".join(
        [
            ".bail on",
            "LOAD %s;" % sql_string(str(EXTENSION)),
            ".output /dev/null",
            ".read %s" % (SUITE / data),
            load,
            ".output",
            ".mode json",
            "DESCRIBE SELECT * FROM ossie_query(%s);" % call,
            "SELECT * FROM ossie_query(%s);" % call,
        ]
    )
    done = subprocess.run([shell, "-unsigned"], input=script, capture_output=True, text=True)
    if done.returncode != 0 or done.stderr.strip():
        return None, None, " ".join(done.stderr.split("LINE 1:")[0].split())
    decoder = json.JSONDecoder(parse_float=Decimal, object_pairs_hook=list)
    described, end = decoder.raw_decode(done.stdout.lstrip())
    records, _ = decoder.raw_decode(done.stdout.lstrip()[end:].lstrip())
    columns = [dict(column) for column in described]
    numeric = [column["column_type"].split("(")[0] in NUMERIC_TYPES for column in columns]
    rows = []
    for record in records:
        values = [value for _, value in record]
        rows.append([Decimal(v) if n and isinstance(v, str) else v for v, n in zip(values, numeric)])
    return [column["column_name"] for column in columns], rows, None


def is_number(value):
    return isinstance(value, (int, Decimal)) and not isinstance(value, bool)


def same_value(actual, expected):
    if actual is None or expected is None:
        return actual is None and expected is None
    if is_number(actual) and is_number(expected):
        if isinstance(expected, int):
            return Decimal(actual) == expected
        tolerance = Decimal("1e-9") * max(abs(Decimal(actual)), abs(expected), 1)
        return abs(Decimal(actual) - expected) <= tolerance
    return type(actual) is type(expected) and actual == expected


def same_rows(actual, expected):
    """Rows match as multisets; within a row, values match by position."""
    left = list(actual)
    for row in expected:
        for index, candidate in enumerate(left):
            if len(candidate) == len(row) and all(same_value(a, e) for a, e in zip(candidate, row)):
                del left[index]
                break
        else:
            return False
    return not left


def show(rows):
    return json.dumps(rows, default=lambda number: float(number) if number % 1 else int(number))


def classify(shell, query, expected, joins):
    arguments, reason = translate(query)
    if arguments is None:
        return "inexpressible", reason
    columns, rows, error = run(shell, expected.get("fixture", "retail"), arguments, joins)
    if "error" in expected:
        if error is None:
            return "false-acceptance", show(rows)
        return ("pass" if expected["error"] in error else "refused"), error
    if error is not None:
        return "false-refusal", error
    if not same_rows(rows, expected["rows"]):
        return "wrong", show(rows)
    if [name.lower() for name in columns] != [name.lower() for name in expected["columns"]]:
        return "names", ", ".join(columns)
    return "pass", ""


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--joins", choices=["left", "inner"], default="left")
    parser.add_argument("--duckdb", default=str(SHELL), help="duckdb CLI to drive (default: the build tree's shell)")
    parser.add_argument("--check", action="store_true", help="fail when a status differs from the pinned one")
    parser.add_argument("--update", action="store_true", help="pin the statuses of this run")
    parser.add_argument("-v", "--verbose", action="store_true", help="print what each case returned")
    options = parser.parse_args()

    statuses = {}
    for case in sorted((SUITE / "cases").iterdir()):
        query = json.loads((case / "query.json").read_text())
        expected = json.loads((case / "expected.json").read_text(), parse_float=Decimal)
        status, detail = classify(options.duckdb, query, expected, options.joins)
        statuses[case.name] = status
        want = expected.get("error", "rows")
        print("%-17s %-38s %s" % (status, case.name, want))
        if options.verbose and detail:
            print("    " + detail)

    counts = {}
    for status in statuses.values():
        counts[status] = counts.get(status, 0) + 1
    print("\n%d cases: %s" % (len(statuses), ", ".join("%s %d" % item for item in sorted(counts.items()))))

    if options.update:
        PINNED.write_text("".join("%s\t%s\n" % item for item in sorted(statuses.items())))
    if options.check:
        pinned = dict(line.split("\t") for line in PINNED.read_text().splitlines())
        changed = sorted(case for case in set(pinned) | set(statuses) if pinned.get(case) != statuses.get(case))
        for case in changed:
            print("CHANGED %s: pinned %s, now %s" % (case, pinned.get(case), statuses.get(case)))
        return 1 if changed else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
