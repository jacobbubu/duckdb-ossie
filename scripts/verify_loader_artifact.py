#!/usr/bin/env python3
"""Verify a real loadable artifact using an independently installed DuckDB wheel.

This verifies the bounded core loader/metadata package, not ontology execution.
Run with DuckDB 1.5.6: python scripts/verify_loader_artifact.py ARTIFACT --output RESULT.json
"""

import argparse
import hashlib
import json
from pathlib import Path
import tempfile

import duckdb


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(artifact, root):
    con = duckdb.connect(config={'allow_unsigned_extensions': 'true'})
    version = con.execute('SELECT * FROM pragma_version()').fetchone()
    assert version[:2] == ('v1.5.6', '069cc9f9b5'), version
    con.execute("LOAD '" + str(artifact).replace("'", "''") + "'")
    assert con.execute("SELECT loaded FROM duckdb_extensions() WHERE extension_name='ossie'").fetchone() == (True,)
    assert con.execute(
        "SELECT count(DISTINCT function_name) FROM duckdb_functions() WHERE starts_with(function_name, 'ossie_')"
    ).fetchone() == (13,)
    fixtures = root / 'test/fixtures/loader_contract'
    con.execute('CALL ossie_load(?)', [str(fixtures / 'valid.json')])
    result = con.execute('SELECT * FROM ossie_metadata()')
    columns = [col[0] for col in result.description]
    rows = [dict(zip(columns, row)) for row in result.fetchall()]
    amount = next(row for row in rows if row['kind'] == 'field' and row['name'] == 'amount')
    assert json.loads(amount['ai_context'])['vendor_hint'] == {'keep': True}
    assert json.loads(json.loads(amount['custom_extensions'])[0]['data']) == {'unit': 'CNY'}
    assert amount['label'] == '收入（元）'
    assert list(amount['examples']) == ['每日收入是多少？']
    assert con.execute("SELECT is_time FROM ossie_fields() WHERE name='day'").fetchone() == (True,)
    assert con.execute("SELECT is_time FROM ossie_fields() WHERE name='audit'").fetchone() == (False,)
    rejected = []
    for path in sorted(fixtures.glob('*.json')):
        if path.name == 'valid.json':
            continue
        try:
            con.execute('CALL ossie_load(?)', [str(path)])
        except duckdb.InvalidInputException:
            rejected.append(path.name)
        else:
            raise AssertionError(f'Invalid document unexpectedly accepted: {path.name}')
        assert con.execute("SELECT name FROM ossie_metadata() WHERE kind='model'").fetchone() == ('metadata_contract',)
    assert len(rejected) == 17, rejected

    # Check all metadata-bearing scopes against the original hand-maintained model.
    reference = root / 'test/fixtures/tpcds_semantic_model.json'
    document = json.loads(reference.read_text())
    con.execute('CALL ossie_load(?)', [str(reference)])
    result = con.execute('SELECT * FROM ossie_metadata()')
    columns = [col[0] for col in result.description]
    records = [dict(zip(columns, row)) for row in result.fetchall()]
    indexed = {(row['kind'], row['dataset'], row['name']): row for row in records}
    expected = [('model', None, document)]
    for dataset in document['datasets']:
        expected.append(('dataset', None, dataset))
        expected.extend(('field', dataset['name'], field) for field in dataset.get('fields', []))
    expected.extend(('relationship', None, row) for row in document.get('relationships', []))
    expected.extend(('metric', None, row) for row in document.get('metrics', []))
    assert len(records) == len(expected)
    for kind, dataset, source in expected:
        row = indexed[kind, dataset, source['name']]
        for key in ['ai_context', 'custom_extensions']:
            if key in source:
                assert json.loads(row[key]) == source[key], (kind, source['name'], key)
            else:
                assert row[key] is None, (kind, source['name'], key)
        if kind != 'relationship':
            assert row['description'] == source.get('description', '')
        if kind == 'field':
            assert row['label'] == source.get('label')

    # Exercise multiple output chunks independently of the small SQL fixture.
    count = 2500
    large = {
        'version': '0.2.0.dev0',
        'name': 'multiple_chunks',
        'datasets': [
            {
                'name': 'facts',
                'source': 'memory.main.facts',
                'fields': [
                    {'name': f'f{i}', 'expression': {'dialects': [{'dialect': 'ANSI_SQL', 'expression': f'f{i}'}]}}
                    for i in range(count)
                ],
            }
        ],
    }
    with tempfile.TemporaryDirectory(prefix='ossie-loader-artifact-') as directory:
        path = Path(directory) / 'large.json'
        path.write_text(json.dumps(large))
        con.execute('CALL ossie_load(?)', [str(path)])
        names = con.execute("SELECT name FROM ossie_metadata() WHERE kind='field'").fetchall()
        assert names == [(f'f{i}',) for i in range(count)]
        assert con.execute('SELECT count(*) FROM ossie_metadata()').fetchone() == (count + 2,)
    return {
        'scope': 'bounded core loader and metadata; no ontology execution',
        'duckdb_version': version[0],
        'duckdb_source_id': version[1],
        'extension_base_commit': 'c2947c7de8e583b70e3f8b859a94c0b9830a36a4',
        'artifact_sha256': sha256(artifact),
        'implementation_sources_sha256': {
            str(path): sha256(root / path)
            for path in [
                Path('src/model/parser.cpp'),
                Path('src/include/ossie/model.hpp'),
                Path('src/functions/describe.cpp'),
            ]
        },
        'verification_inputs_sha256': {
            str(path.relative_to(root)): sha256(path)
            for path in [reference, root / 'test/sql/ossie_loader_contract.test', *sorted(fixtures.glob('*'))]
        },
        'artifact_loaded_in_stock_wheel': True,
        'rejected_documents': rejected,
        'failed_load_preserves_model': True,
        'reference_metadata_scopes_checked': len(expected),
        'multi_chunk_field_rows_checked': count,
        'verification_script_sha256': sha256(Path(__file__)),
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = verify(args.artifact.resolve(), Path(__file__).resolve().parents[1])
    rendered = json.dumps(report, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end='')
