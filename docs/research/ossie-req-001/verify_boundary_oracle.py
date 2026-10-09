"""Hand-derived SQL controls, not tests of the unimplemented ontology executor."""

import hashlib
import json
from pathlib import Path

import duckdb


ROOT = Path(__file__).resolve().parent


def main():
    assert duckdb.__version__ == "1.5.6"
    fixture = ROOT / "gaap-boundary.csv"
    content = """ds,product,region,user_type,user_id,gaap_val,ytd_gaap
2026-03-01,X,North,student,e01,10.00,10.00
2026-03-01,X,North,student,e02,100.00,9.99
2026-03-01,X,South,individual,e03,20.00,
2026-03-01,X,South,individual,e04,,20.00
2026-03-01,Y,North,student,e05,30.00,20.00
2026-03-02,X,North,student,e06,5.00,0.00
2026-03-03,X,North,student,e07,0.00,20.00
2026-03-03,X,South,individual,e08,20.00,10.00
2026-03-04,X,South,individual,e09,,
2026-03-31,X,North,student,e10,50.00,10.00
2026-04-01,X,North,student,e11,100.00,20.00
"""
    if fixture.exists() and fixture.read_text() != content:
        raise ValueError("Refusing to overwrite changed boundary fixture")
    fixture.write_text(content)
    db = duckdb.connect()
    db.execute("CREATE TABLE boundary(ds DATE, product VARCHAR, region VARCHAR, user_type VARCHAR, "
               "user_id VARCHAR, gaap_val DECIMAL(18,2), ytd_gaap DECIMAL(18,2))")
    db.execute("INSERT INTO boundary SELECT * FROM read_csv(?, header=true, nullstr='')", [str(fixture)])
    where = "product='X' AND ds >= DATE '2026-03-01' AND ds < DATE '2026-04-01'"
    daily_sql = f"""SELECT ds, COUNT(*), COUNT(gaap_val), SUM(gaap_val), AVG(gaap_val)
        FROM boundary WHERE {where} AND ytd_gaap >= ? GROUP BY ds ORDER BY ds"""
    expected = {
        10: [("2026-03-01", 2, 1, 10.0, 10.0), ("2026-03-03", 2, 2, 20.0, 10.0),
             ("2026-03-31", 1, 1, 50.0, 50.0)],
        20: [("2026-03-01", 1, 0, None, None), ("2026-03-03", 1, 1, 0.0, 0.0)]}
    normalized = lambda rows: [[str(row[0]), *row[1:3],
                               None if row[3] is None else float(row[3]), row[4]] for row in rows]
    cases = {}
    for threshold in (10, 20):
        actual = normalized(db.execute(daily_sql, [threshold]).fetchall())
        assert actual == [list(row) for row in expected[threshold]]
        ids = [row[0] for row in db.execute(
            f"SELECT user_id FROM boundary WHERE {where} AND ytd_gaap >= ? ORDER BY user_id",
            [threshold]).fetchall()]
        assert ids == (["e01", "e04", "e07", "e08", "e10"] if threshold == 10 else ["e04", "e07"])
        cases[str(threshold)] = {"daily": actual, "object_ids": ids}
    original_case = db.execute(f"""SELECT ds, AVG(CASE WHEN ytd_gaap >= 10 THEN gaap_val END)
        FROM boundary WHERE {where} GROUP BY ds ORDER BY ds""").fetchall()
    assert len(original_case) == 5
    assert [(str(ds), value) for ds, value in original_case if value is None] == [
        ("2026-03-02", None), ("2026-03-04", None)]
    empty = db.execute(f"SELECT COUNT(*),COUNT(gaap_val),SUM(gaap_val),AVG(gaap_val) "
                       f"FROM boundary WHERE {where} AND ytd_gaap>=1000").fetchone()
    assert empty == (0, 0, None, None)
    global_average = db.execute(f"SELECT COUNT(*),COUNT(gaap_val),SUM(gaap_val),AVG(gaap_val) "
                                f"FROM boundary WHERE {where} AND ytd_gaap>=10").fetchone()
    assert global_average == (5, 4, 80, 20.0)
    result = {"scope": "SQL oracle on synthetic controls; no ontology executor loaded",
              "duckdb_version": duckdb.__version__,
              "fixture_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
              "expected_values_source": "Explicit hand-derived constants in verify_boundary_oracle.py",
              "columns": ["ds", "record_count", "gaap_value_count", "gaap_sum", "gaap_average"],
              "cases": cases, "old_case_groups": [[str(ds), value] for ds, value in original_case],
              "empty": list(empty), "global_threshold_10": [5, 4, "80.00", 20.0],
              "checks": ["threshold equality", "NULL YTD excludes population", "NULL GAAP changes denominator",
                         "zero GAAP retained", "product filter", "March 31 inclusive / April 1 exclusive",
                         "empty scalar aggregate", "CASE versus population-filter group difference"]}
    (ROOT / "boundary-results.json").write_text(json.dumps(result, indent=2) + "\n")
    db.close()
    print(json.dumps({"cases": cases, "checks": result["checks"]}, indent=2))


if __name__ == "__main__":
    main()
