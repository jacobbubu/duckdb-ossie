/** Loader/metadata controls in a stock Node binding, without build-tree fallback. */
import { join, basename, relative, resolve } from "node:path";
import { readdirSync, writeFileSync } from "node:fs";
import {
  Database,
  assert,
  digest,
  json,
  temporary,
  emitReport,
  ROOT,
} from "./artifact_helpers.ts";
interface MetadataSource {
  name: string;
  description?: string;
  label?: string;
  ai_context?: unknown;
  custom_extensions?: unknown;
  fields?: MetadataSource[];
}
interface ModelSource extends MetadataSource {
  datasets: MetadataSource[];
  relationships?: MetadataSource[];
  metrics?: MetadataSource[];
}
export async function verify(artifact: string) {
  const con = await Database.open();
  try {
    const version = await con.loadArtifact(artifact);
    const required = [
      "ossie_load",
      "ossie_datasets",
      "ossie_fields",
      "ossie_metrics",
      "ossie_relationships",
      "ossie_metadata",
      "ossie_compile",
      "ossie_query",
    ].sort();
    const registered = (
      await con.rows(
        "SELECT DISTINCT function_name FROM duckdb_functions() WHERE starts_with(function_name, 'ossie_')",
      )
    )
      .map((row) => String(row[0]))
      .sort();
    assert(
      required.every((name) => registered.includes(name)),
      "missing loader function",
    );
    assert.deepEqual(
      await con.one(
        "SELECT count(*) FROM duckdb_functions() WHERE function_name='ossie_query'",
      ),
      [3],
    );
    const fixtures = join(ROOT, "test/fixtures/loader_contract");
    await con.run("CALL ossie_load(?)", [join(fixtures, "valid.json")]);
    const rows = await con.records("SELECT * FROM ossie_metadata()");
    const amount = rows.find(
      (row) => row.kind === "field" && row.name === "amount",
    )!;
    assert.deepEqual(JSON.parse(String(amount.ai_context)).vendor_hint, {
      keep: true,
    });
    assert.deepEqual(
      JSON.parse(JSON.parse(String(amount.custom_extensions))[0].data),
      { unit: "CNY" },
    );
    assert.equal(amount.label, "收入（元）");
    assert.deepEqual(amount.examples, ["每日收入是多少？"]);
    assert.deepEqual(
      await con.one("SELECT is_time FROM ossie_fields() WHERE name='day'"),
      [true],
    );
    assert.deepEqual(
      await con.one("SELECT is_time FROM ossie_fields() WHERE name='audit'"),
      [false],
    );
    const rejected: string[] = [];
    for (const name of readdirSync(fixtures)
      .filter((name) => name.endsWith(".json") && name !== "valid.json")
      .sort()) {
      await assert.rejects(
        con.run("CALL ossie_load(?)", [join(fixtures, name)]),
        /Invalid Input Error/,
      );
      rejected.push(name);
      assert.deepEqual(
        await con.one("SELECT name FROM ossie_metadata() WHERE kind='model'"),
        ["metadata_contract"],
      );
    }
    assert.equal(rejected.length, 17);
    const reference = join(ROOT, "test/fixtures/tpcds_semantic_model.json"),
      document = json<ModelSource>(reference);
    await con.run("CALL ossie_load(?)", [reference]);
    const records = await con.records("SELECT * FROM ossie_metadata()");
    const indexed = new Map(
      records.map((row) => [
        JSON.stringify([row.kind, row.dataset, row.name]),
        row,
      ]),
    );
    const expected: [string, string | null, MetadataSource][] = [
      ["model", null, document],
    ];
    for (const dataset of document.datasets) {
      expected.push(["dataset", null, dataset]);
      for (const field of dataset.fields || [])
        expected.push(["field", dataset.name, field]);
    }
    for (const relationship of document.relationships || [])
      expected.push(["relationship", null, relationship]);
    for (const metric of document.metrics || [])
      expected.push(["metric", null, metric]);
    assert.equal(records.length, expected.length);
    for (const [kind, dataset, source] of expected) {
      const row = indexed.get(JSON.stringify([kind, dataset, source.name]))!;
      assert(row);
      for (const key of ["ai_context", "custom_extensions"] as const)
        assert.deepEqual(
          row[key] === null ? null : JSON.parse(String(row[key])),
          source[key] ?? null,
          `${kind}/${source.name}/${key}`,
        );
      if (kind !== "relationship")
        assert.equal(row.description, source.description || "");
      if (kind === "field") assert.equal(row.label, source.label ?? null);
    }
    const count = 2500;
    await temporary("ossie-loader-artifact-", async (directory) => {
      const path = join(directory, "large.json");
      writeFileSync(
        path,
        JSON.stringify({
          version: "0.2.0.dev0",
          name: "multiple_chunks",
          datasets: [
            {
              name: "facts",
              source: "memory.main.facts",
              fields: Array.from({ length: count }, (_, i) => ({
                name: `f${i}`,
                expression: {
                  dialects: [{ dialect: "ANSI_SQL", expression: `f${i}` }],
                },
              })),
            },
          ],
        }),
      );
      await con.run("CALL ossie_load(?)", [path]);
      assert.deepEqual(
        await con.rows("SELECT name FROM ossie_metadata() WHERE kind='field'"),
        Array.from({ length: count }, (_, i) => [`f${i}`]),
      );
      assert.deepEqual(await con.one("SELECT count(*) FROM ossie_metadata()"), [
        count + 2,
      ]);
    });
    return {
      scope: "bounded core loader and metadata; no ontology execution",
      runtime: `Bun ${Bun.version}`,
      client: "@duckdb/node-api 1.5.6-r.1",
      duckdb_version: version[0],
      duckdb_source_id: version[1],
      artifact_sha256: digest(artifact),
      artifact_loaded_in_stock_node_bindings: true,
      required_loader_functions: required,
      registered_public_functions: registered,
      rejected_documents: rejected,
      failed_load_preserves_model: true,
      reference_metadata_scopes_checked: expected.length,
      multi_chunk_field_rows_checked: count,
      verification_script_sha256: digest(import.meta.path),
      verification_helper_sha256: digest(
        join(ROOT, "scripts/artifact_helpers.ts"),
      ),
      implementation_sources_sha256: Object.fromEntries(
        [
          "src/model/parser.cpp",
          "src/include/ossie/model.hpp",
          "src/functions/describe.cpp",
        ].map((path) => [path, digest(join(ROOT, path))]),
      ),
      verification_inputs_sha256: Object.fromEntries(
        [
          reference,
          join(ROOT, "test/sql/ossie_loader_contract.test"),
          ...readdirSync(fixtures)
            .sort()
            .map((name) => join(fixtures, name)),
        ].map((path) => [relative(ROOT, path), digest(path)]),
      ),
    };
  } finally {
    con.close();
  }
}
if (import.meta.main) emitReport(await verify(resolve(process.argv[2])));
