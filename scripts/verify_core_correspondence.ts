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
      await db.run("CALL ossie_load(?)", [base]);
      async function refused(
        name: string,
        change: (model: any) => void,
        diagnostic: string,
      ) {
        const model = json<any>(base);
        change(model);
        const path = join(directory, name + ".json");
        writeFileSync(path, JSON.stringify(model));
        const before = await db.records("SELECT * FROM ossie_metadata()");
        await assert.rejects(
          db.run("CALL ossie_load(?)", [path]),
          (error: unknown) => {
            assert(String(error).includes(diagnostic), String(error));
            return true;
          },
        );
        assert.deepEqual(
          await db.records("SELECT * FROM ossie_metadata()"),
          before,
        );
        observations.push({
          name,
          accepted: false,
          classification:
            "non-string scalar value rejected; full previous metadata preserved",
          diagnostic,
          input_sha256: digest(path),
        });
      }
      await refused(
        "scalar-model-name",
        (m) => {
          m.name = 123;
        },
        "field 'name' must be a string",
      );
      await refused(
        "scalar-description-label",
        (m) => {
          m.description = false;
          m.datasets[0].fields[7].label = 123;
        },
        "field 'description' must be a string",
      );
      await refused(
        "container-description-label",
        (m) => {
          m.description = {};
          m.datasets[0].fields[7].label = [];
        },
        "field 'description' must be a string",
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
      await refused(
        "numeric-expression",
        (m) => {
          m.datasets[0].fields[0].expression.dialects = [
            { dialect: "ANSI_SQL", expression: 123 },
          ];
        },
        "field 'expression' must be a string",
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
      const paths: {
        scope: string;
        path: (string | number)[];
        required: boolean;
        ai?: boolean;
      }[] = [
        { scope: "model.name", path: ["name"], required: true },
        {
          scope: "dataset.name",
          path: ["datasets", 0, "name"],
          required: true,
        },
        {
          scope: "field.name",
          path: ["datasets", 0, "fields", 7, "name"],
          required: true,
        },
        { scope: "metric.name", path: ["metrics", 0, "name"], required: true },
        {
          scope: "relationship.name",
          path: ["relationships", 0, "name"],
          required: true,
        },
        {
          scope: "dataset.source",
          path: ["datasets", 0, "source"],
          required: true,
        },
        {
          scope: "relationship.from",
          path: ["relationships", 0, "from"],
          required: true,
        },
        {
          scope: "relationship.to",
          path: ["relationships", 0, "to"],
          required: true,
        },
        {
          scope: "variant.dialect",
          path: [
            "datasets",
            0,
            "fields",
            7,
            "expression",
            "dialects",
            0,
            "dialect",
          ],
          required: true,
        },
        {
          scope: "variant.expression",
          path: [
            "datasets",
            0,
            "fields",
            7,
            "expression",
            "dialects",
            0,
            "expression",
          ],
          required: true,
        },
        { scope: "model.description", path: ["description"], required: false },
        {
          scope: "dataset.description",
          path: ["datasets", 0, "description"],
          required: false,
        },
        {
          scope: "field.description",
          path: ["datasets", 0, "fields", 7, "description"],
          required: false,
        },
        {
          scope: "metric.description",
          path: ["metrics", 0, "description"],
          required: false,
        },
        {
          scope: "field.label",
          path: ["datasets", 0, "fields", 7, "label"],
          required: false,
        },
        ...[
          ["model", []],
          ["dataset", ["datasets", 0]],
          ["field", ["datasets", 0, "fields", 7]],
          ["metric", ["metrics", 0]],
          ["relationship", ["relationships", 0]],
        ].map(([scope, path]) => ({
          scope: `${scope}.instructions`,
          path: [
            ...(path as (string | number)[]),
            "ai_context",
            "instructions",
          ],
          required: false,
          ai: true,
        })),
      ];
      for (const spec of paths)
        for (const [kind, value] of [
          ["integer", 123],
          ["real", 1.5],
          ["boolean", true],
          ["null", null],
          ["object", {}],
          ["array", []],
        ] as const) {
          const key = String(spec.path.at(-1));
          const diagnostic = spec.ai
            ? "ai_context.instructions must be a string"
            : value === null && spec.required
              ? `required string field '${key}'`
              : `field '${key}' must be a string`;
          await refused(
            `strict-${spec.scope}-${kind}`,
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
              if (spec.ai) {
                let target = m;
                for (const part of spec.path.slice(0, -2))
                  target = target[part];
                target.ai_context = { instructions: "legal" };
              }
              let target = m;
              for (const part of spec.path.slice(0, -1)) target = target[part];
              target[spec.path.at(-1)!] = value;
            },
            diagnostic,
          );
        }
      await accepted(
        "quoted-scalar-text",
        (m) => {
          m.name = "123";
          m.description = "true";
          m.datasets[0].fields[7].label = "123";
          m.datasets[0].fields[0].expression.dialects = [
            { dialect: "ANSI_SQL", expression: "123" },
          ];
        },
        async (rows) => {
          const model = rows.find((r) => r.kind === "model")!;
          assert.equal(model.name, "123");
          assert.equal(model.description, "true");
          assert.equal(rows.find((r) => r.name === "amount")!.label, "123");
          const [field] = await db.records(
            "SELECT expression FROM ossie_fields() WHERE name='day'",
          );
          assert.equal(field!.expression, "123");
        },
        "quoted scalar-looking strings remain valid without coercion",
      );
      await accepted(
        "absent-optional-text",
        (m) => {
          delete m.description;
          delete m.datasets[0].description;
          delete m.datasets[0].fields[7].description;
          delete m.datasets[0].fields[7].label;
          delete m.metrics[0].description;
        },
        (rows) => {
          assert.equal(rows.find((r) => r.kind === "model")!.description, "");
          const amount = rows.find((r) => r.name === "amount")!;
          assert.equal(amount.description, "");
          assert.equal(amount.label, null);
        },
        "missing optional values preserve previous empty/SQL NULL discovery behavior",
      );
      const yaml = join(directory, "scalar.yaml");
      const yamlBase =
        'version: "0.2.0.dev0"\nname: "123"\ndatasets:\n  - name: facts\n    source: memory.main.facts\n    fields:\n      - name: amount\n        expression:\n          dialects:\n            - dialect: ANSI_SQL\n              expression: amount\n';
      writeFileSync(yaml, yamlBase + 'description: "true"\n');
      await db.run("CALL ossie_load(?)", [yaml]);
      const yamlBefore = await db.records("SELECT * FROM ossie_metadata()");
      assert.equal(
        yamlBefore.find((r) => r.kind === "model")!.description,
        "true",
      );
      for (const text of ["123", "1.5", "true", "null", "{}", "[]"]) {
        writeFileSync(yaml, yamlBase + `description: ${text}\n`);
        await assert.rejects(
          db.run("CALL ossie_load(?)", [yaml]),
          /field 'description' must be a string/,
        );
        assert.deepEqual(
          await db.records("SELECT * FROM ossie_metadata()"),
          yamlBefore,
        );
        observations.push({
          name: `yaml.description.${text}`,
          accepted: false,
          classification:
            "YAML actual scalar/container type refused; quoted text accepted; prior state preserved",
          input_sha256: digest(yaml),
        });
      }
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
      base_commit: "9bb9622",
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
        "minLength/additionalProperties/extension/dialect validation deviations need explicit compatibility conclusion",
        "final rule-driven P0 acceptance",
      ],
    };
  } finally {
    db.close();
  }
}
if (import.meta.main) emitReport(await verify(resolve(process.argv[2]!)));
