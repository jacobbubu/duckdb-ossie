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
    assert.equal(rejected.length, 19);
    // Quoted numeric/Boolean-looking field names make key coercion observable:
    // a malformed [123]/[true] would otherwise resolve to valid declared columns.
    const strings = ["123", "true", '中文\\引号"\n\t', "before\0after", ""];
    const field = (name: string) => ({
      name,
      expression: {
        dialects: [{ dialect: "ANSI_SQL", expression: `"${name}"` }],
      },
    });
    const stringModel = () => ({
      version: "0.2.0.dev0",
      name: "string_arrays",
      ai_context: { synonyms: strings, examples: strings },
      datasets: ["facts", "dim"].map((name) => ({
        name,
        source: `memory.main.${name}`,
        primary_key: ["123"],
        unique_keys: [["true"]],
        ai_context: { synonyms: strings, examples: strings },
        fields: [field("123"), field("true")].map((f) => ({
          ...f,
          ai_context: { synonyms: strings, examples: strings },
        })),
      })),
      relationships: [
        {
          name: "link",
          from: "facts",
          to: "dim",
          from_columns: ["123"],
          to_columns: ["123"],
          ai_context: { synonyms: strings, examples: strings },
        },
      ],
      metrics: [
        {
          name: "total",
          expression: {
            dialects: [{ dialect: "ANSI_SQL", expression: 'SUM(facts."123")' }],
          },
          ai_context: { synonyms: strings, examples: strings },
        },
      ],
    });
    const strictCases: {
      name: string;
      diagnostic: string;
      document_sha256: string;
    }[] = [];
    await temporary("ossie-loader-strings-", async (directory) => {
      const path = join(directory, "model.json");
      writeFileSync(path, JSON.stringify(stringModel()));
      await con.run("CALL ossie_load(?)", [path]);
      const preserved = await con.records("SELECT * FROM ossie_metadata()");
      for (const row of preserved) {
        assert.deepEqual(row.synonyms, strings);
        assert.deepEqual(row.examples, strings);
        const ai = JSON.parse(String(row.ai_context));
        assert.deepEqual(ai.synonyms, strings);
        assert.deepEqual(ai.examples, strings);
      }
      const [dataset] = await con.records(
        "SELECT * FROM ossie_datasets() WHERE name='facts'",
      );
      assert.deepEqual(dataset!.primary_key, ["123"]);
      assert.deepEqual(dataset!.unique_keys, [["true"]]);
      const [relationship] = await con.records(
        "SELECT * FROM ossie_relationships()",
      );
      assert.deepEqual(relationship!.from_columns, ["123"]);
      assert.deepEqual(relationship!.to_columns, ["123"]);
      async function refuse(name: string, model: unknown, diagnostic: string) {
        writeFileSync(path, JSON.stringify(model));
        await assert.rejects(
          con.run("CALL ossie_load(?)", [path]),
          (error: unknown) => {
            assert(String(error).includes(diagnostic), String(error));
            return true;
          },
        );
        assert.deepEqual(
          await con.records("SELECT * FROM ossie_metadata()"),
          preserved,
        );
        strictCases.push({ name, diagnostic, document_sha256: digest(path) });
      }
      for (const scope of [
        "model",
        "dataset",
        "field",
        "metric",
        "relationship",
      ]) {
        for (const key of ["synonyms", "examples"] as const) {
          for (const value of [123, true]) {
            const model = stringModel();
            const target =
              scope === "model"
                ? model
                : scope === "dataset"
                  ? model.datasets[0]!
                  : scope === "field"
                    ? model.datasets[0]!.fields[0]!
                    : scope === "metric"
                      ? model.metrics[0]!
                      : model.relationships[0]!;
            target.ai_context[key] = ["valid", value] as unknown as string[];
            await refuse(
              `${scope}.${key}.${typeof value}`,
              model,
              `field '${key}' member at index 1 must be a string`,
            );
          }
        }
      }
      for (const key of [
        "primary_key",
        "from_columns",
        "to_columns",
      ] as const) {
        for (const value of [123, true]) {
          const model = stringModel();
          if (key === "primary_key")
            model.datasets[0]![key] = [value] as unknown as string[];
          else model.relationships[0]![key] = [value] as unknown as string[];
          await refuse(
            `${key}.${typeof value}`,
            model,
            `field '${key}' member at index 0 must be a string`,
          );
        }
      }
      for (const value of [null, {}, []]) {
        const model = stringModel();
        model.ai_context.synonyms = ["valid", value] as unknown as string[];
        await refuse(
          `synonyms.member.${JSON.stringify(value)}`,
          model,
          "field 'synonyms' member at index 1 must be a string",
        );
      }
      for (const value of [null, "text"]) {
        const model = stringModel();
        model.ai_context.examples = value as unknown as string[];
        await refuse(
          `examples.array.${JSON.stringify(value)}`,
          model,
          "field 'examples' must be an array of strings",
        );
      }
      for (const value of [123, true]) {
        const model = stringModel();
        model.datasets[0]!.unique_keys = [[value]] as unknown as string[][];
        await refuse(
          `unique_keys.${typeof value}`,
          model,
          "non-string column in 'unique_keys'",
        );
      }
      const yaml = join(directory, "model.yaml");
      const yamlBase =
        'version: "0.2.0.dev0"\nname: yaml_string_arrays\ndatasets:\n  - name: facts\n    source: memory.main.facts\n    fields:\n      - name: amount\n        expression:\n          dialects:\n            - dialect: ANSI_SQL\n              expression: amount\n';
      writeFileSync(
        yaml,
        yamlBase +
          'ai_context:\n  synonyms: ["123", "true", "中文"]\n  examples: []\n',
      );
      await con.run("CALL ossie_load(?)", [yaml]);
      const yamlPreserved = await con.records("SELECT * FROM ossie_metadata()");
      const yamlModel = yamlPreserved.find((row) => row.kind === "model")!;
      assert.deepEqual(yamlModel.synonyms, ["123", "true", "中文"]);
      assert.deepEqual(yamlModel.examples, []);
      for (const value of ["123", "true"]) {
        writeFileSync(
          yaml,
          yamlBase + `ai_context:\n  synonyms: ["合法", ${value}]\n`,
        );
        await assert.rejects(
          con.run("CALL ossie_load(?)", [yaml]),
          /field 'synonyms' member at index 1 must be a string/,
        );
        assert.deepEqual(
          await con.records("SELECT * FROM ossie_metadata()"),
          yamlPreserved,
        );
        strictCases.push({
          name: `yaml.synonyms.${value}`,
          diagnostic: "field 'synonyms' member at index 1 must be a string",
          document_sha256: digest(yaml),
        });
      }
      const empty = stringModel();
      empty.ai_context = { synonyms: [], examples: [] };
      delete (empty.datasets[0] as Partial<(typeof empty.datasets)[0]>)!
        .ai_context;
      writeFileSync(path, JSON.stringify(empty));
      await con.run("CALL ossie_load(?)", [path]);
      const [modelRow] = await con.records(
        "SELECT synonyms, examples FROM ossie_metadata() WHERE kind='model'",
      );
      assert.deepEqual(modelRow, { synonyms: [], examples: [] });
    });
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
      strict_string_array_cases: strictCases,
      string_array_bytes_preserved: true,
      strict_string_array_scope:
        "AI five scopes, primary_key and relationship columns; existing unique_keys rejection; empty/absent arrays",

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
