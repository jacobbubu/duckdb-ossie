/** Characterize the remaining core schema deviations; acceptance is not conformance. */
import { join, resolve } from "node:path";
import { writeFileSync } from "node:fs";
import {
  Database,
  ROOT,
  assert,
  digest,
  json,
  temporary,
  emitReport,
} from "./artifact_helpers.ts";
export async function verify(artifact: string) {
  const db = await Database.open();
  const observations: unknown[] = [];
  const base = join(ROOT, "test/fixtures/loader_contract/valid.json");
  const schemaPath = join(
    ROOT,
    "docs/research/ossie-req-001/sources/apache-ossie/core-spec/ossie-schema.json",
  );
  const schema = json<any>(schemaPath);
  try {
    const engine = await db.loadArtifact(artifact);
    await temporary("ossie-core-correspondence-", async (directory) => {
      async function accepted(
        name: string,
        change: (model: any) => void,
        check: (rows: Record<string, unknown>[]) => void | Promise<void>,
        classification: string,
      ) {
        const model = json<any>(base);
        change(model);
        const path = join(directory, name + ".json");
        writeFileSync(path, JSON.stringify(model));
        await db.run("CALL ossie_load(?)", [path]);
        const rows = await db.records("SELECT * FROM ossie_metadata()");
        await check(rows);
        observations.push({
          name,
          accepted: true,
          classification,
          input_document: model,
          input_sha256: digest(path),
          metadata: rows,
        });
      }
      await accepted(
        "scalar-model-name",
        (m) => {
          m.name = 123;
        },
        (rows) =>
          assert.equal(rows.find((r) => r.kind === "model")!.name, "123"),
        "invalid schema value converted to text",
      );
      await accepted(
        "scalar-description-label",
        (m) => {
          m.description = false;
          m.datasets[0].fields[7].label = 123;
        },
        (rows) => {
          assert.equal(
            rows.find((r) => r.kind === "model")!.description,
            "false",
          );
          assert.equal(rows.find((r) => r.name === "amount")!.label, "123");
        },
        "invalid schema value converted to text",
      );
      await accepted(
        "container-description-label",
        (m) => {
          m.description = {};
          m.datasets[0].fields[7].label = [];
        },
        (rows) => {
          assert.equal(rows.find((r) => r.kind === "model")!.description, "");
          assert.equal(rows.find((r) => r.name === "amount")!.label, null);
        },
        "invalid optional values silently treated as absent/empty",
      );
      await accepted(
        "empty-model-name",
        (m) => {
          m.name = "";
        },
        (rows) => assert.equal(rows.find((r) => r.kind === "model")!.name, ""),
        "schema minLength is not enforced",
      );
      await accepted(
        "unknown-root-property",
        (m) => {
          m.vendor_rule = "ignore valid-user threshold";
        },
        (rows) => assert(!JSON.stringify(rows).includes("vendor_rule")),
        "additionalProperties=false is not enforced; unknown field discarded, not executable",
      );
      await accepted(
        "malformed-extension",
        (m) => {
          m.custom_extensions = { data: 123 };
        },
        (rows) =>
          assert.deepEqual(
            JSON.parse(
              String(rows.find((r) => r.kind === "model")!.custom_extensions),
            ),
            { data: 123 },
          ),
        "custom extension container/member schema not validated; raw descriptive value retained",
      );
      await accepted(
        "ai-vendor-property",
        (m) => {
          m.ai_context = { instructions: "合法", vendor_hint: { unit: "CNY" } };
        },
        (rows) =>
          assert.deepEqual(
            JSON.parse(
              String(rows.find((r) => r.kind === "model")!.ai_context),
            ),
            { instructions: "合法", vendor_hint: { unit: "CNY" } },
          ),
        "valid additional AI properties preserved as descriptive JSON",
      );
      await accepted(
        "lowercase-dialect",
        (m) => {
          m.datasets[0].fields[0].expression.dialects = [
            { dialect: "ansi_sql", expression: "day" },
          ];
        },
        (rows) => {
          const row = rows.find((r) => r.kind === "field" && r.name === "day")!;
          assert.equal(row.expression_dialect, "ANSI_SQL");
          assert.deepEqual(row.available_dialects, ["ansi_sql"]);
        },
        "case-insensitive dialect acceptance extends exact schema enum",
      );
      await accepted(
        "numeric-expression",
        (m) => {
          m.datasets[0].fields[0].expression.dialects = [
            { dialect: "ANSI_SQL", expression: 123 },
          ];
        },
        async () => {
          const [row] = await db.records(
            "SELECT expression FROM ossie_fields() WHERE name='day'",
          );
          assert.equal(row!.expression, "123");
        },
        "invalid schema expression scalar converted and parsed as constant",
      );
      await accepted(
        "nul-ai-string",
        (m) => {
          m.ai_context = "before\0after";
        },
        (rows) => {
          const row = rows.find((r) => r.kind === "model")!;
          assert.equal(JSON.parse(String(row.ai_context)), "before\0after");
          assert.equal(row.instructions, "before\0after");
        },
        "valid JSON string preserved identically in raw AI and flattened instructions",
      );
      await accepted(
        "nul-ai-instructions",
        (m) => {
          m.ai_context = { instructions: "before\0after" };
        },
        (rows) => {
          const row = rows.find((r) => r.kind === "model")!;
          assert.equal(
            JSON.parse(String(row.ai_context)).instructions,
            "before\0after",
          );
          assert.equal(row.instructions, "before\0after");
        },
        "valid JSON string preserved identically in raw AI and flattened instructions",
      );
      await accepted(
        "nul-unique-key",
        (m) => {
          m.datasets[0].unique_keys = [["before\0after"]];
        },
        () => {},
        "valid string unique key preserved in internal list",
      );
      const [dataset] = await db.records(
        "SELECT unique_keys FROM ossie_datasets()",
      );
      assert.deepEqual(dataset!.unique_keys, [["before\0after"]]);
      await accepted(
        "undeclared-keys",
        (m) => {
          m.datasets[0].primary_key = ["not_declared"];
        },
        () => {},
        "schema permits physical key names without fields; core defers actual data checks",
      );
      const [keys] = await db.records(
        "SELECT primary_key FROM ossie_datasets()",
      );
      assert.deepEqual(keys!.primary_key, ["not_declared"]);
      const bytes = 'before\0after中文\\"\n\t';
      for (const scope of [
        "model",
        "dataset",
        "field",
        "metric",
        "relationship",
      ]) {
        for (const shape of ["string", "object"]) {
          await accepted(
            `fidelity-${scope}-${shape}`,
            (m) => {
              m.relationships = [
                {
                  name: "self",
                  from: "facts",
                  to: "facts",
                  from_columns: ["amount"],
                  to_columns: ["amount"],
                },
              ];
              const target =
                scope === "model"
                  ? m
                  : scope === "dataset"
                    ? m.datasets[0]
                    : scope === "field"
                      ? m.datasets[0].fields[7]
                      : scope === "metric"
                        ? m.metrics[0]
                        : m.relationships[0];
              target.ai_context =
                shape === "string" ? bytes : { instructions: bytes };
            },
            (rows) => {
              const target = rows.find(
                (r) =>
                  r.kind === scope &&
                  (scope !== "field" || r.name === "amount"),
              )!;
              assert.equal(target.instructions, bytes);
              const raw = JSON.parse(String(target.ai_context));
              assert.equal(shape === "string" ? raw : raw.instructions, bytes);
            },
            "legal AI raw and flattened bytes agree across all five scopes",
          );
        }
      }
      await accepted(
        "fidelity-descriptions-label",
        (m) => {
          m.description = bytes;
          m.datasets[0].description = bytes;
          m.datasets[0].fields[7].description = bytes;
          m.datasets[0].fields[7].label = bytes;
          m.metrics[0].description = bytes;
        },
        (rows) => {
          for (const kind of ["model", "dataset", "metric"])
            assert.equal(rows.find((r) => r.kind === kind)!.description, bytes);
          const amount = rows.find(
            (r) => r.kind === "field" && r.name === "amount",
          )!;
          assert.equal(amount.description, bytes);
          assert.equal(amount.label, bytes);
        },
        "optional scalar strings preserve legal control and UTF-8 bytes",
      );
      const fixture = join(ROOT, "test/fixtures/conformance/nul_metadata.json");
      await db.run("CALL ossie_load(?)", [fixture]);
      const before = await db.records("SELECT * FROM ossie_metadata()");
      const [composite] = await db.records(
        "SELECT unique_keys FROM ossie_datasets()",
      );
      assert.deepEqual(composite!.unique_keys, [["before\0after", "中文"]]);
      const malformed = join(
        ROOT,
        "test/fixtures/conformance/nul_expression.json",
      );
      await assert.rejects(
        db.run("CALL ossie_load(?)", [malformed]),
        /expression must not contain NUL/,
      );
      assert.deepEqual(
        await db.records("SELECT * FROM ossie_metadata()"),
        before,
      );
      observations.push({
        name: "NUL expression refusal preserves full prior metadata",
        accepted: false,
        classification: "SQL parser prefix truncation prevented before parsing",
        input_sha256: digest(malformed),
      });
      const emptyKey = json<any>(base);
      emptyKey.datasets[0].unique_keys = [[]];
      const emptyPath = join(directory, "empty-unique-key.json");
      writeFileSync(emptyPath, JSON.stringify(emptyKey));
      await assert.rejects(
        db.run("CALL ossie_load(?)", [emptyPath]),
        (error: unknown) => {
          assert(
            String(error).includes("empty entry in 'unique_keys'"),
            String(error),
          );
          observations.push({
            name: "empty-unique-key",
            accepted: false,
            classification:
              "schema permits empty inner key; implementation refuses ambiguous key",
            input_document: emptyKey,
            input_sha256: digest(emptyPath),
            diagnostic: String(error),
          });
          return true;
        },
      );
    });
    const inventory = [
      ...Object.keys(schema.properties).map((field) => ({
        scope: "root",
        field,
      })),
      ...Object.entries(schema.$defs).flatMap(([scope, value]) =>
        Object.keys((value as any).properties || {}).map((field) => ({
          scope,
          field,
        })),
      ),
    ];
    return {
      scope:
        "read-only characterization and complete schema property inventory, not approval of deviations or full conformance",
      base_commit: "2470cff",
      engine,
      artifact_sha256: digest(artifact),
      verifier_sha256: digest(import.meta.path),
      source_sha256: Object.fromEntries(
        [
          "src/model/parser.cpp",
          "test/fixtures/conformance/nul_metadata.json",
          "test/fixtures/conformance/nul_expression.json",
          "src/model/validate.cpp",
          "src/functions/describe.cpp",
          "src/include/ossie/model.hpp",
          "scripts/artifact_helpers.ts",
          "test/fixtures/loader_contract/valid.json",
          "docs/research/ossie-req-001/sources/apache-ossie/core-spec/ossie-schema.json",
        ].map((p) => [p, digest(join(ROOT, p))]),
      ),
      schema_property_inventory: inventory,
      observations,
      production_changed: true,
      remaining: [
        "final R2 acceptance still requires remaining validation deviation conclusion",
        "scalar coercion/minLength/additionalProperties/extension validation deviations need explicit compatibility conclusion",
        "final rule-driven P0 acceptance",
      ],
    };
  } finally {
    db.close();
  }
}
if (import.meta.main) emitReport(await verify(resolve(process.argv[2]!)));
