"""Independent GAAP oracle; requires duckdb==1.5.6, never loads ossie.

Run: /tmp/duckdb-ossie-req-001-venv/bin/python <this file>
No network, source project writes, dotenv loading or service initialization.
"""

import ast
import csv
import hashlib
import io
import json
import random
import sys
from collections import defaultdict
from datetime import date, timedelta
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path

import duckdb


ROOT = Path(__file__).resolve().parent
MANIFEST_PATH = ROOT / "sources" / "manifest.json"
SQL = """SELECT ds, COUNT(*) AS record_count,
       COUNT(DISTINCT user_id) AS distinct_user_count,
       SUM(gaap_val) AS gaap_sum, AVG(gaap_val) AS gaap_average,
       ROUND(AVG(gaap_val), 2) AS gaap_average_rounded
FROM dws_gaap_di
WHERE product = 'X' AND ytd_gaap >= ?
  AND ds >= DATE '2026-03-01' AND ds < DATE '2026-04-01'
GROUP BY ds ORDER BY ds"""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def immutable_write(path, text):
    if path.exists() and path.read_text() != text:
        raise ValueError(f"Refusing to overwrite different source fixture: {path}")
    path.write_text(text)


def source_rows(path):
    # Execute only the reviewed generator and expected constants, not module top-level code.
    tree = ast.parse(path.read_text())
    selected = [node for node in tree.body if (
        isinstance(node, ast.FunctionDef) and node.name == "demo_rows"
    ) or (
        isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == "EXPECTED_DAILY_AVERAGES"
            for target in node.targets
        )
    )]
    namespace = {"random": random, "date": date, "timedelta": timedelta}
    exec(compile(ast.Module(body=selected, type_ignores=[]), str(path), "exec"), namespace)
    rows = [tuple(row[:5]) + (Decimal(str(row[5])), Decimal(str(row[6])))
            for row in namespace["demo_rows"]()]
    return rows, namespace["EXPECTED_DAILY_AVERAGES"]


def selected_rows(rows, threshold):
    return [row for row in rows if row[1] == "X" and row[6] >= threshold
            and "2026-03-01" <= row[0] < "2026-04-01"]


def oracle(rows):
    groups = defaultdict(list)
    for row in rows:
        groups[row[0]].append(row)
    result = []
    for ds, items in sorted(groups.items()):
        total = sum((row[5] for row in items), Decimal(0))
        average = total / len(items)
        result.append({"ds": ds, "record_count": len(items),
                       "distinct_user_count": len({row[4] for row in items}),
                       "gaap_sum": str(total), "gaap_average": str(average),
                       "gaap_average_rounded": str(average.quantize(
                           Decimal("0.01"), rounding=ROUND_HALF_UP))})
    return result


def main():
    if duckdb.__version__ != "1.5.6":
        raise ValueError(f"Expected DuckDB 1.5.6, got {duckdb.__version__}")
    manifest = json.loads(MANIFEST_PATH.read_text())
    for entry in manifest["files"]:
        if "saved_path" in entry:
            path = ROOT.parents[2] / entry["saved_path"]
            if digest(path) != entry["sha256"]:
                raise ValueError(f"Source hash mismatch: {path}")
    generator = ROOT / "sources/qwenpaw-data/examples/init_demo.py"
    rows, expected = source_rows(generator)
    assert len(rows) == 475
    assert len({row[4] for row in rows}) == 475
    assert min(row[0] for row in rows) == "2026-03-01"
    assert max(row[0] for row in rows) == "2026-03-15"
    csv_buffer = io.StringIO(newline="")
    writer = csv.writer(csv_buffer, lineterminator="\n")
    writer.writerow(["ds", "product", "region", "user_type", "user_id", "gaap_val", "ytd_gaap"])
    writer.writerows(rows)
    fixture = ROOT / "gaap-original-475.csv"
    immutable_write(fixture, csv_buffer.getvalue())
    immutable_write(ROOT / "baseline.sql", SQL + ";\n")
    connection = duckdb.connect()
    connection.execute("""CREATE TABLE dws_gaap_di (
        ds DATE NOT NULL, product VARCHAR NOT NULL, region VARCHAR NOT NULL,
        user_type VARCHAR NOT NULL, user_id VARCHAR PRIMARY KEY,
        gaap_val DECIMAL(18,2) NOT NULL, ytd_gaap DECIMAL(18,2) NOT NULL)""")
    connection.executemany("INSERT INTO dws_gaap_di VALUES (?, ?, ?, ?, ?, ?, ?)", rows)
    db_count, db_ids = connection.execute(
        "SELECT COUNT(*), COUNT(DISTINCT user_id) FROM dws_gaap_di").fetchone()
    assert (db_count, db_ids) == (475, 475)
    cases = {}
    for threshold in (10, 20):
        chosen = selected_rows(rows, Decimal(threshold))
        calculated = oracle(chosen)
        sql_rows = connection.execute(SQL, [threshold]).fetchall()
        assert len(sql_rows) == len(calculated) == 15
        for actual, independent in zip(sql_rows, calculated):
            assert str(actual[0]) == independent["ds"]
            assert actual[1:3] == (independent["record_count"], independent["distinct_user_count"])
            assert actual[3] == Decimal(independent["gaap_sum"])
            assert abs(Decimal(str(actual[4])) - Decimal(independent["gaap_average"])) < Decimal("1e-10")
            assert Decimal(str(actual[5])) == Decimal(independent["gaap_average_rounded"])
            if threshold == 10:
                assert actual[5] == expected[str(actual[0])]
        # Full region/user-type groups preserve numerator and denominator, not sum of averages.
        grouped = connection.execute("""SELECT ds, region, user_type, COUNT(*), SUM(gaap_val)
            FROM dws_gaap_di WHERE product='X' AND ytd_gaap >= ?
            GROUP BY ds, region, user_type ORDER BY ds, region, user_type""", [threshold]).fetchall()
        python_groups = defaultdict(list)
        for row in chosen:
            python_groups[(row[0], row[2], row[3])].append(row)
        assert len(grouped) == len(python_groups)
        for ds, region, user_type, count, total in grouped:
            group = python_groups[(str(ds), region, user_type)]
            assert count == len(group)
            assert total == sum((row[5] for row in group), Decimal(0))
        # Compare object IDs exactly, not only cardinality.
        objects = connection.execute("""SELECT user_id FROM dws_gaap_di
            WHERE product='X' AND ytd_gaap >= ? ORDER BY user_id""", [threshold]).fetchall()
        assert [row[0] for row in objects] == sorted(row[4] for row in chosen)
        object_file = ROOT / f"baseline-objects-threshold-{threshold}.json"
        object_file.write_text(json.dumps([row[0] for row in objects], indent=2) + "\n")
        cases[str(threshold)] = {"population_stage": "record selection before aggregation",
            "object_ids_sha256": digest(object_file), "record_count": len(chosen),
            "gaap_sum": str(sum((row[5] for row in chosen), Decimal(0))),
            "daily": calculated,
            "region_user_type_groups": [
                {"ds": str(ds), "region": region, "user_type": user_type,
                 "record_count": count, "gaap_sum": str(total)}
                for ds, region, user_type, count, total in grouped]}
    empty = connection.execute("""SELECT COUNT(*), SUM(gaap_val), AVG(gaap_val)
        FROM dws_gaap_di WHERE product='X' AND ytd_gaap >= 1000""").fetchone()
    assert empty == (0, None, None)
    results = {"scope": "Independent SQL and Decimal oracle; ossie extension not loaded; A1/A2/A3/A4 acceptance pending",
        "duckdb_version": duckdb.__version__,
        "duckdb_engine": connection.execute("SELECT version(), library_version, source_id FROM pragma_version()").fetchone(),
        "python_version": sys.version.split()[0], "spec_commit": manifest["spec_commit"],
        "generator_sha256": digest(generator), "source_manifest_sha256": digest(MANIFEST_PATH),
        "fixture_sha256": digest(fixture), "baseline_sql_sha256": digest(ROOT / "baseline.sql"),
        "fixture_rows": 475, "distinct_user_ids": 475,
        "unit_source": "demo_semantic_config.xlsx metric!E2 = USD; no conversion",
        "date_interval": "[2026-03-01, 2026-04-01)", "cases": cases,
        "empty_aggregate": {"count": empty[0], "sum": empty[1], "average": empty[2]},
        "checks": ["all 15 published threshold-10 rounded daily averages",
            "both thresholds daily exact sum/count and average tolerance 1e-10",
            "both thresholds exact object ID sets", "all region/user-type group sums and counts",
            "original source hash verification", "empty SQL aggregate"]}
    (ROOT / "baseline-results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n")
    connection.close()
    print(json.dumps({"duckdb": duckdb.__version__, "rows": len(rows),
                      "march_10": {threshold: next(row for row in case["daily"]
                          if row["ds"] == "2026-03-10") for threshold, case in cases.items()},
                      "checks": results["checks"]}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
