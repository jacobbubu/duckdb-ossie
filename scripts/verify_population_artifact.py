#!/usr/bin/env python3
"""Independent Decimal/control oracle against the real single-table population artifact."""

import argparse
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
import copy
import csv
from datetime import date
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

import duckdb

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "docs/research/ossie-req-001"
DEFINITIONS = EVIDENCE / "proposed-definitions"
FILTERS = [
    "User.product = 'X'",
    "User.date >= DATE '2026-03-01' AND User.date < DATE '2026-04-01'",
]
METRICS = ["record_count", "gaap_value_count", "gaap_sum", "mean_gaap"]
TABLE = "CREATE TABLE dws_gaap_di(ds DATE, product VARCHAR, region VARCHAR, user_type VARCHAR, user_id VARCHAR, gaap_val DECIMAL(18,2), ytd_gaap DECIMAL(18,2))"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rows(path):
    with path.open() as handle:
        return [
            {
                **row,
                "gaap_val": Decimal(row["gaap_val"]) if row["gaap_val"] else None,
                "ytd_gaap": Decimal(row["ytd_gaap"]) if row["ytd_gaap"] else None,
            }
            for row in csv.DictReader(handle)
        ]


def aggregate(selected):
    values = [row["gaap_val"] for row in selected if row["gaap_val"] is not None]
    total = sum(values, Decimal(0)) if values else None
    return len(selected), len(values), total, total / len(values) if values else None


def same(actual, expected):
    assert actual[:3] == expected[:3], (actual, expected)
    if expected[3] is None:
        assert actual[3] is None
    else:
        assert abs(Decimal(str(actual[3])) - expected[3]) <= Decimal("1e-10"), (
            actual,
            expected,
        )


def bundle_files(directory, docs):
    entries = {}
    for kind, doc in zip(["core", "ontology", "mapping"], docs):
        path = directory / (kind + ".json")
        path.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n")
        entries[kind] = {"path": path.name, "name": doc["name"], "sha256": digest(path)}
    manifest = directory / "bundle.json"
    manifest.write_text(json.dumps({"profile": "gaap-scalar-population-v1", **entries}, indent=2) + "\n")
    return manifest


def load(con, path):
    return con.execute("CALL ossie_bundle_load(?)", [str(path)]).fetchone()


def objects(con, filters=FILTERS, concept="ValidUser"):
    return sorted(
        row[0] for row in con.execute("SELECT entity_id FROM ossie_objects(?, ?)", [concept, filters]).fetchall()
    )


def metric(con, filters=FILTERS, dimensions=None):
    return con.execute(
        "SELECT * FROM ossie_concept_query(?, ?, ?, ?)",
        ["ValidUser", METRICS, dimensions or [], filters],
    ).fetchall()


def verify(artifact):
    con = duckdb.connect(config={"allow_unsigned_extensions": True, "threads": 1})
    assert con.execute("SELECT * FROM pragma_version()").fetchone()[:2] == (
        "v1.5.6",
        "069cc9f9b5",
    )
    con.execute("LOAD '" + str(artifact).replace("'", "''") + "'")
    con.execute(TABLE)
    baseline = json.loads((EVIDENCE / "baseline-results.json").read_text())
    original = EVIDENCE / "gaap-original-475.csv"
    assert digest(original) == baseline["fixture_sha256"]
    con.execute(
        "INSERT INTO dws_gaap_di SELECT * FROM read_csv(?, header=true)",
        [str(original)],
    )
    independent = rows(original)
    cases = {}
    content_hashes = []
    for threshold in [10, 20]:
        snapshot = load(con, ROOT / f"test/fixtures/population/bundle-{threshold}.json")
        content_hashes.append(snapshot[2])
        selected = [
            row
            for row in independent
            if row["product"] == "X"
            and row["ytd_gaap"] is not None
            and row["ytd_gaap"] >= threshold
            and "2026-03-01" <= row["ds"] < "2026-04-01"
        ]
        ids = objects(con)
        assert ids == sorted(row["user_id"] for row in selected)
        assert ids == json.loads((EVIDENCE / f"baseline-objects-threshold-{threshold}.json").read_text())
        same(metric(con)[0], aggregate(selected))
        assert len(ids) == baseline["cases"][str(threshold)]["record_count"]
        grouped = defaultdict(list)
        for row in selected:
            grouped[row["ds"]].append(row)
        daily = metric(con, dimensions=["dws_gaap_di.ds"])
        assert len(daily) == len(grouped) == 15
        for row in daily:
            same(row[1:], aggregate(grouped[str(row[0])]))
        grouped_drill = defaultdict(list)
        for row in selected:
            grouped_drill[(row["ds"], row["region"], row["user_type"])].append(row)
        drill = metric(
            con,
            dimensions=[
                "dws_gaap_di.ds",
                "dws_gaap_di.region",
                "dws_gaap_di.user_type",
            ],
        )
        assert len(drill) == len(grouped_drill)
        for result in drill:
            key = (str(result[0]), result[1], result[2])
            same(result[3:], aggregate(grouped_drill[key]))
            narrowed = FILTERS + [
                f"User.date = DATE '{key[0]}'",
                f"User.region = '{key[1]}'",
                f"User.user_type = '{key[2]}'",
            ]
            assert objects(con, narrowed) == sorted(row["user_id"] for row in grouped_drill[key])
        compiled = con.execute(
            "SELECT * FROM ossie_concept_compile(?, ?, ?, ?)",
            ["ValidUser", METRICS, [], FILTERS],
        ).fetchone()
        assert compiled[2:4] == snapshot[1:3]
        assert compiled[-1] is False
        traced = con.execute(
            "SELECT * FROM ossie_concept_query('ValidUser', ?, [], ?, include_trace=>true)",
            [METRICS, FILTERS],
        ).fetchone()
        assert traced[-3] == snapshot[1] and traced[-2] == snapshot[2] and traced[-1] == compiled[5]
        cases[str(threshold)] = {
            "objects": len(ids),
            "sum": str(metric(con)[0][2]),
            "daily_groups": len(daily),
            "drill_groups": len(drill),
            "snapshot_id": snapshot[1],
            "content_sha256": snapshot[2],
            "sql": compiled[0],
            "ontology_sha256": compiled[5],
        }
    assert content_hashes[0] == content_hashes[1]
    assert cases["10"]["ontology_sha256"] != cases["20"]["ontology_sha256"]

    # Hand-derived boundary controls are separate from the original generator.
    boundary = EVIDENCE / "gaap-boundary.csv"
    controls = json.loads((EVIDENCE / "boundary-results.json").read_text())
    assert digest(boundary) == controls["fixture_sha256"]
    con.execute("DELETE FROM dws_gaap_di")
    con.execute("INSERT INTO dws_gaap_di SELECT * FROM read_csv(?,header=true)", [str(boundary)])
    for threshold in [10, 20]:
        load(con, ROOT / f"test/fixtures/population/bundle-{threshold}.json")
        assert objects(con) == controls["cases"][str(threshold)]["object_ids"]
        daily = sorted(metric(con, dimensions=["dws_gaap_di.ds"]))
        expected_daily = controls["cases"][str(threshold)]["daily"]
        assert len(daily) == len(expected_daily)
        for actual, expected in zip(daily, expected_daily):
            assert str(actual[0]) == expected[0]
            same(
                actual[1:],
                (
                    expected[1],
                    expected[2],
                    Decimal(str(expected[3])) if expected[3] is not None else None,
                    Decimal(str(expected[4])) if expected[4] is not None else None,
                ),
            )
    assert metric(con, ["User.product = 'absent'"])[0] == (0, 0, None, None)
    assert metric(con, ["User.product = 'absent'"], ["dws_gaap_di.ds"]) == []

    # Binding retains its exact bundle across reload, including execution evidence.
    old = load(con, ROOT / "test/fixtures/population/bundle-10.json")
    con.execute(
        "PREPARE frozen_trace AS SELECT * FROM ossie_concept_query('ValidUser',['record_count'],[],[],include_trace=>true)"
    )
    new = load(con, ROOT / "test/fixtures/population/bundle-20.json")
    prepared = con.execute("EXECUTE frozen_trace").fetchone()
    assert prepared[0] == 7 and prepared[-3] == old[1]
    assert prepared[-3] != new[1]
    con.execute("DEALLOCATE frozen_trace")
    con.execute("INSERT INTO dws_gaap_di VALUES (DATE '2026-03-01','X','North','student','added',30,30)")
    assert len(objects(con, [], "User")) == 11
    load(con, ROOT / "test/fixtures/population/bundle-20.json")
    assert len(objects(con, [], "User")) == 12
    con.execute("DELETE FROM dws_gaap_di WHERE user_id='added'")

    base_docs = [
        json.loads((DEFINITIONS / name).read_text())
        for name in [
            "gaap.core.json",
            "gaap.ontology.threshold-10.json",
            "gaap.mapping.json",
        ]
    ]
    rejected = []

    def rejection(name, path, reason, rollback=False):
        before = con.execute("SELECT snapshot_id FROM ossie_concepts() LIMIT 1").fetchone()
        try:
            load(con, path)
        except duckdb.Error as error:
            assert reason in str(error), (name, str(error), reason)
            assert "object=" in str(error) and "stage=" in str(error) and "missing_capability=" in str(error)
        else:
            raise AssertionError(f"{name} unexpectedly accepted")
        if rollback:
            con.execute("ROLLBACK")
        assert con.execute("SELECT snapshot_id FROM ossie_concepts() LIMIT 1").fetchone() == before
        rejected.append(name)

    with tempfile.TemporaryDirectory(prefix="ossie-population-controls-") as temporary:
        directory = Path(temporary)
        # Description/AI/vendor units are observable metadata, never executable rules.
        unchanged = load(con, ROOT / "test/fixtures/population/bundle-10.json")
        before_ids, before_values = objects(con, []), metric(con, [])[0]
        descriptive = copy.deepcopy(base_docs)
        for document in descriptive:
            document["description"] = "synthetic editorial change without a rule change"
        descriptive[0]["ai_context"] = {"instructions": "Synthetic alternative wording", "examples": ["example"]}
        descriptive[0]["custom_extensions"] = [
            {"vendor": "quality-control", "data": json.dumps({"unit": "CNY", "scale": 1000})}
        ]
        for field in descriptive[0]["datasets"][0]["fields"]:
            field["description"] = "synthetic field description"
            field["label"] = "synthetic display label"
            field["ai_context"] = "synthetic vocabulary"
        descriptive[0]["metrics"][0]["description"] = "synthetic metric description"
        descriptive[0]["metrics"][0]["custom_extensions"] = [
            {"vendor": "quality-control", "data": json.dumps({"unit": "CNY"})}
        ]
        modified = load(con, bundle_files(directory, descriptive))
        assert modified[2] == unchanged[2]
        assert objects(con, []) == before_ids
        assert metric(con, [])[0] == before_values
        load(con, ROOT / "test/fixtures/population/bundle-10.json")
        mutations = {
            "root_requires": lambda c, o, m: o["ontology"][0].update(requires=["TRUE"]),
            "derived_requires": lambda c, o, m: o["ontology"][1].update(requires=["TRUE"]),
            "identity_requires": lambda c, o, m: o["ontology"][0]["relationships"][0].update(requires=["TRUE"]),
            "attribute_requires": lambda c, o, m: o["ontology"][0]["relationships"][-1].update(requires=["TRUE"]),
            "value_type_requires": lambda c, o, m: o["ontology"].append(
                {"concept": "Decimal", "type": "ValueType", "requires": ["TRUE"]}
            ),
            "ontology_requires": lambda c, o, m: o.update(requires=["TRUE"]),
            "multiple_inheritance": lambda c, o, m: o["ontology"][1].update(extends=["User", "Other"]),
            "derived_identity_override": lambda c, o, m: o["ontology"][1].update(identify_by=["id"]),
            "derived_relation_override": lambda c, o, m: o["ontology"][1].update(
                relationships=o["ontology"][0]["relationships"]
            ),
            "derived_mapping": lambda c, o, m: m["concept_mappings"].append(
                {**copy.deepcopy(m["concept_mappings"][0]), "concept": "ValidUser"}
            ),
            "link_identity_override": lambda c, o, m: m["concept_mappings"][0]["link_mappings"][0]["object_mapping"][
                "referent_mappings"
            ][0].update(expression="dws_gaap_di.region"),
            "duplicate_binding": lambda c, o, m: m["concept_mappings"][0]["link_mappings"][0]["children"].append(
                copy.deepcopy(m["concept_mappings"][0]["link_mappings"][0]["children"][0])
            ),
            "bad_target": lambda c, o, m: m["concept_mappings"][0]["link_mappings"][0]["children"][0][
                "object_mapping"
            ].update(concept="Decimal"),
            "reference_name_mismatch": lambda c, o, m: m["ontology_ref"].update(name="other"),
            "nul_core_field_expression": lambda c, o, m: c["datasets"][0]["fields"][-1]["expression"]["dialects"][
                0
            ].update(expression="ytd_gaap\x00 + 999"),
            "nul_rule": lambda c, o, m: o["ontology"][1].update(derived_by=["User.ytd_gaap >= 10\x00 OR TRUE"]),
            "unknown_version": lambda c, o, m: o.update(version="0.2.0"),
            "unknown_rule_function": lambda c, o, m: o["ontology"][1].update(derived_by=["random() > 0"]),
            "aggregate_rule": lambda c, o, m: o["ontology"][1].update(derived_by=["SUM(User.ytd_gaap) > 0"]),
            "subquery_rule": lambda c, o, m: o["ontology"][1].update(derived_by=["EXISTS(SELECT 1)"]),
            "implicit_boolean_rule": lambda c, o, m: o["ontology"][1].update(derived_by=["User.ytd_gaap"]),
            "implicit_boolean_conjunction": lambda c, o, m: o["ontology"][1].update(
                derived_by=["User.ytd_gaap AND TRUE"]
            ),
            "implicit_boolean_case": lambda c, o, m: o["ontology"][1].update(
                derived_by=["CASE WHEN User.ytd_gaap THEN TRUE ELSE FALSE END"]
            ),
            "hidden_aggregate_field": lambda c, o, m: c["datasets"][0]["fields"][-1]["expression"]["dialects"][
                0
            ].update(expression="SUM(gaap_val)"),
            "hidden_window_field": lambda c, o, m: c["datasets"][0]["fields"][-1]["expression"]["dialects"][0].update(
                expression="SUM(gaap_val) OVER ()"
            ),
            "hidden_subquery_field": lambda c, o, m: c["datasets"][0]["fields"][-1]["expression"]["dialects"][0].update(
                expression="(SELECT MAX(gaap_val) FROM elsewhere)"
            ),
            "hidden_unknown_field": lambda c, o, m: c["datasets"][0]["fields"][-1]["expression"]["dialects"][0].update(
                expression="random()"
            ),
            "metric_unknown_function": lambda c, o, m: c["metrics"][0]["expression"]["dialects"][0].update(
                expression="mystery(dws_gaap_di.gaap_val)"
            ),
            "metric_nested_aggregate": lambda c, o, m: c["metrics"][0]["expression"]["dialects"][0].update(
                expression="SUM(AVG(dws_gaap_di.gaap_val))"
            ),
            "metric_qualified_function": lambda c, o, m: c["metrics"][0]["expression"]["dialects"][0].update(
                expression="main.avg(dws_gaap_di.gaap_val)"
            ),
            "multi_dataset": lambda c, o, m: c["datasets"].append(
                {**copy.deepcopy(c["datasets"][0]), "name": "another"}
            ),
            "metric_without_aggregate": lambda c, o, m: c["metrics"][0]["expression"]["dialects"][0].update(
                expression="dws_gaap_di.gaap_val"
            ),
            "metric_type_mismatch": lambda c, o, m: c["metrics"][0].update(datatype="Decimal"),
            "reserved_metric_case": lambda c, o, m: c["metrics"][0].update(name="_OSSIE_snapshot_id"),
            "physical_type_mismatch": lambda c, o, m: c["datasets"][0]["fields"][0].update(datatype="String"),
            "nonunique_attribute": lambda c, o, m: o["ontology"][0]["relationships"][2].update(multiplicity="OneToOne"),
            "missing_physical_column": lambda c, o, m: c["datasets"][0]["fields"][-1]["expression"]["dialects"][
                0
            ].update(expression="missing_column"),
        }
        for name, mutate in mutations.items():
            docs = copy.deepcopy(base_docs)
            mutate(*docs)
            path = bundle_files(directory, docs)
            rejection(name, path, "ossie_population")
        path = bundle_files(directory, base_docs)
        manifest = json.loads(path.read_text())
        manifest["core"]["sha256"] = "0" * 64
        path.write_text(json.dumps(manifest))
        rejection("sha256_mismatch", path, "SHA-256 mismatch")
        path = bundle_files(directory, base_docs)
        manifest = json.loads(path.read_text())
        manifest["core"]["name"] = "wrong_name"
        path.write_text(json.dumps(manifest))
        rejection("manifest_name_mismatch", path, "manifest document name mismatch")
        path = bundle_files(directory, base_docs)
        con.execute("INSERT INTO dws_gaap_di SELECT * FROM dws_gaap_di WHERE user_id='e01'")
        rejection("duplicate_identical_row", path, "entire batch violates")
        con.execute("DELETE FROM dws_gaap_di WHERE user_id='e01'")
        con.execute("INSERT INTO dws_gaap_di VALUES (DATE '2026-03-01','X','North','student','e01',10,10)")
        con.execute("INSERT INTO dws_gaap_di VALUES (DATE '2026-06-01','Y','North','student','e01',10,0)")
        rejection("duplicate_id_outside_filter", path, "entire batch violates")
        con.execute("DELETE FROM dws_gaap_di WHERE ds=DATE '2026-06-01'")
        con.execute("INSERT INTO dws_gaap_di VALUES (DATE '2026-06-01','Y','North','student',NULL,10,0)")
        rejection("null_id_outside_filter", path, "entire batch violates")
        con.execute("DELETE FROM dws_gaap_di WHERE user_id IS NULL")
        con.execute("BEGIN TRANSACTION")
        rejection(
            "explicit_transaction_load",
            path,
            "outside an explicit transaction",
            rollback=True,
        )
        con.execute("CREATE VIEW fact_view AS SELECT * FROM dws_gaap_di")
        docs = copy.deepcopy(base_docs)
        docs[0]["datasets"][0]["source"] = "memory.main.fact_view"
        rejection("view_source", bundle_files(directory, docs), "native tables")
        con.execute("CREATE TEMP TABLE temp_facts AS SELECT * FROM dws_gaap_di")
        docs[0]["datasets"][0]["source"] = "temp.main.temp_facts"
        rejection("temp_source", bundle_files(directory, docs), "ossie_population")
        con.execute(
            "CREATE TABLE oversized AS SELECT ds,product,region,user_type,CAST(i AS VARCHAR) user_id,gaap_val,ytd_gaap FROM dws_gaap_di CROSS JOIN range(10001) r(i) LIMIT 10001"
        )
        docs[0]["datasets"][0]["source"] = "memory.main.oversized"
        rejection("row_limit_no_truncation", bundle_files(directory, docs), "exceeds 10000")
        # A physical/logical name collision must be expanded exactly once, not re-fed to the core filter parser.
        docs = copy.deepcopy(base_docs)
        docs[0]["datasets"][0]["fields"][-1]["expression"]["dialects"][0]["expression"] = "ytd_gaap + 1"
        con.execute("UPDATE dws_gaap_di SET ytd_gaap=8.5 WHERE user_id='e02'")
        once = load(con, bundle_files(directory, docs))
        assert "e02" not in objects(con, [])
        compiled = con.execute("SELECT sql FROM ossie_concept_compile('ValidUser',['record_count'],[],[])").fetchone()[
            0
        ]
        assert compiled.count("+ 1") == 1, compiled
        assert once[3] == 11
        # Native DATE, DECIMAL, NULL and quoted text survive capture without reinterpretation.
        con.execute("UPDATE dws_gaap_di SET region='O''Brien' WHERE user_id='e01'")
        load(con, ROOT / "test/fixtures/population/bundle-10.json")
        result = con.execute(
            "SELECT \"dws_gaap_di.ds\", \"dws_gaap_di.gaap_val\", \"dws_gaap_di.region\" FROM ossie_objects('User', ['User.id = ''e01'''])"
        ).fetchone()
        assert result == (date(2026, 3, 1), Decimal("10.00"), "O'Brien")
        assert con.execute(
            "SELECT \"dws_gaap_di.gaap_val\" FROM ossie_objects('User',['User.id = ''e04'''])"
        ).fetchone() == (None,)

    # The hidden row reader is usable only through its captured AST dependency.
    try:
        con.execute("SELECT * FROM system.main._ossie_snapshot_rows()")
    except duckdb.Error as error:
        assert "internal frozen AST dependency required" in str(error)
    else:
        raise AssertionError("diagnostic SQL unexpectedly accessed a snapshot")
    # A caller macro cannot shadow the engine aggregate used by the population plan.
    before_metric = metric(con, [])[0]
    con.execute("CREATE MACRO avg(x) AS 999")
    assert metric(con, [])[0] == before_metric
    con.execute("DROP MACRO avg")

    # Interrupt a slow, bounded native capture and verify atomic publication.
    before_cancel = con.execute("SELECT snapshot_id FROM ossie_concepts() LIMIT 1").fetchone()
    con.execute(
        "CREATE TABLE cancel_facts AS SELECT DATE '2026-03-01' ds, 'X' product, repeat('r',1024) region, 'student' user_type, CAST(i AS VARCHAR) user_id, 10::DECIMAL(18,2) gaap_val, 10::DECIMAL(18,2) ytd_gaap FROM range(10000) r(i)"
    )
    with tempfile.TemporaryDirectory(prefix="ossie-cancel-") as temporary:
        docs = copy.deepcopy(base_docs)
        docs[0]["datasets"][0]["source"] = "memory.main.cancel_facts"
        cancel_path = bundle_files(Path(temporary), docs)
        started, finished = threading.Event(), threading.Event()
        outcome = []

        def interrupted_load():
            started.set()
            try:
                load(con, cancel_path)
                outcome.append("completed")
            except duckdb.Error as error:
                outcome.append(str(error))
            finally:
                finished.set()

        thread = threading.Thread(target=interrupted_load)
        thread.start()
        assert started.wait(5)
        while not finished.wait(0.001):
            con.interrupt()
        thread.join(5)
        assert not thread.is_alive() and len(outcome) == 1
        assert "interrupt" in outcome[0].lower(), outcome
        assert con.execute("SELECT snapshot_id FROM ossie_concepts() LIMIT 1").fetchone() == before_cancel
    con.execute("DROP TABLE cancel_facts")

    # Parallel callers share snapshots but each query owns its materialized result.
    expected = len(objects(con, [], "ValidUser"))

    def parallel_query(_):
        cursor = con.cursor()
        try:
            return cursor.execute("SELECT count(*) FROM ossie_objects('ValidUser', [])").fetchone()[0]
        finally:
            cursor.close()

    with ThreadPoolExecutor(max_workers=2) as pool:
        assert list(pool.map(parallel_query, range(4))) == [expected] * 4

    # Closing a DB with a loaded batch must release its file lock for another process.
    # This detects a cache -> bundle -> connection -> DB ownership cycle.
    with tempfile.TemporaryDirectory(prefix="ossie-lifetime-") as temporary:
        directory = Path(temporary)
        database_path = directory / "frozen_lifetime.duckdb"
        closing = duckdb.connect(str(database_path), config={"allow_unsigned_extensions": True, "threads": 1})
        closing.execute("LOAD '" + str(artifact).replace("'", "''") + "'")
        closing.execute(TABLE)
        closing.execute(
            "INSERT INTO dws_gaap_di SELECT * FROM read_csv(?,header=true)",
            [str(boundary)],
        )
        docs = copy.deepcopy(base_docs)
        docs[0]["datasets"][0]["source"] = "frozen_lifetime.main.dws_gaap_di"
        load(closing, bundle_files(directory, docs))
        closing.close()
        reopened = subprocess.run(
            [
                sys.executable,
                "-c",
                "import duckdb,sys; c=duckdb.connect(sys.argv[1]); assert c.execute('SELECT count(*) FROM dws_gaap_di').fetchone()==(11,); c.close()",
                str(database_path),
            ],
            capture_output=True,
            text=True,
            timeout=10,
        )
        assert reopened.returncode == 0, reopened.stderr

    report = {
        "scope": "real single-table population execution; not full P0/A1-A8 acceptance",
        "duckdb_version": "1.5.6",
        "duckdb_source_id": "069cc9f9b5",
        "artifact_sha256": digest(artifact),
        "script_sha256": digest(Path(__file__)),
        "inputs_sha256": {
            str(path.relative_to(ROOT)): digest(path)
            for path in [
                original,
                boundary,
                EVIDENCE / "baseline-results.json",
                EVIDENCE / "boundary-results.json",
                *sorted(DEFINITIONS.glob("*.json")),
                *sorted((ROOT / "test/fixtures/population").glob("*.json")),
                ROOT / "test/sql/ossie_population.test",
            ]
        },
        "cases": cases,
        "same_capture_content_for_thresholds": True,
        "prepared_query_retains_original_snapshot": True,
        "explicit_reload_observes_source_changes": True,
        "failed_load_preserves_bundle": True,
        "computed_field_expanded_once": True,
        "typed_null_date_decimal_quoted_text_preserved": True,
        "parallel_queries_threads_1": True,
        "interrupted_load_preserves_bundle": True,
        "engine_macro_shadowing_prevented": True,
        "diagnostic_sql_cannot_access_frozen_batch": True,
        "descriptive_metadata_does_not_change_values": True,
        "database_close_releases_native_file_lock": True,
        "implementation_sources_sha256": {
            str(path.relative_to(ROOT)): digest(path)
            for path in [
                ROOT / "CMakeLists.txt",
                *sorted((ROOT / "src").rglob("*.cpp")),
                *sorted((ROOT / "src").rglob("*.hpp")),
            ]
        },
        "rejected_controls": rejected,
    }
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = verify(args.artifact.resolve())
    rendered = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end="")
