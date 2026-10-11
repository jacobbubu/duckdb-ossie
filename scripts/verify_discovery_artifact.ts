/** Bound vocabulary, actual preflight types and generic resource-driven requests. */
import { join, resolve } from "node:path";
import { readFileSync, writeFileSync } from "node:fs";
import {
  Database,
  assert,
  digest,
  json,
  ROOT,
  temporary,
  emitReport,
} from "./artifact_helpers.ts";

export async function verify(artifact: string) {
  const db = await Database.open(),
    checks: string[] = [],
    observations: unknown[] = [];
  const check = (name: string, action: () => void) => {
    action();
    checks.push(name);
  };
  async function discovery() {
    const metrics = await db.records(
      "SELECT * FROM ossie_population_metrics() ORDER BY name",
    );
    const fields = await db.records(
      "SELECT * FROM ossie_population_fields() ORDER BY dataset,name",
    );
    const [capability] = await db.records(
      "SELECT * FROM ossie_population_capabilities()",
    );
    const capabilities = JSON.parse(String(capability!.capabilities_json));
    check("version and active snapshot", () => {
      assert.equal(capability!.api_version, "2.2.0");
      assert.equal(
        capability!.discovery_schema,
        "ossie-population-discovery-v1",
      );
      assert.equal(capability!.plan_schema, "ossie-population-plan-v1");
      assert.equal(capability!.ossie_spec_version, "0.2.0.dev0");
      assert.deepEqual(capabilities.engine, {
        version: "v1.5.6",
        source_id: "069cc9f9b5",
      });
      assert(capabilities.supported_profiles.includes(capability!.profile));
      for (const row of [...metrics, ...fields]) {
        for (const name of [
          "profile",
          "snapshot_id",
          "content_sha256",
          "core_sha256",
          "ontology_sha256",
          "mapping_sha256",
          "manifest_sha256",
        ])
          assert.equal(row[name], capability![name]);
        assert.equal(row.load_preflight_validated, true);
        assert.equal(row.execution_verified, false);
        assert.deepEqual(JSON.parse(String(row.available_dialects_json)), [
          row.selected_dialect,
        ]);
      }
      assert.equal(
        capabilities.error_contract.schema,
        "ossie-population-error-v1",
      );
      assert.equal(capabilities.compile.final_output_binding, false);
      assert.equal(
        capabilities.mcp_example.exact_numeric_transport_verified,
        false,
      );
      assert.equal(capabilities.cross_call_reload_atomicity, false);
    });
    const result = { metrics, fields, capability, capabilities };
    observations.push(result);
    return result;
  }
  try {
    const engine = await db.loadArtifact(artifact);
    for (const endpoint of ["metrics", "fields", "capabilities"]) {
      await assert.rejects(
        () => db.rows(`SELECT * FROM ossie_population_${endpoint}()`),
        /no bundle loaded/,
      );
      checks.push("no bundle refuses " + endpoint);
    }
    await db.run(
      "CREATE TABLE dws_gaap_di(ds DATE, product VARCHAR, region VARCHAR, user_type VARCHAR,user_id VARCHAR,gaap_val DECIMAL(18,2),ytd_gaap DECIMAL(18,2))",
    );
    await db.run(
      "COPY dws_gaap_di FROM 'docs/research/ossie-req-001/gaap-boundary.csv' (HEADER)",
    );
    await db.run("CALL ossie_bundle_load(?)", [
      join(ROOT, "test/fixtures/population/bundle-10.json"),
    ]);
    const first = await discovery();
    check("GAAP vocabulary and actual money/count/AVG types", () => {
      assert.deepEqual(
        first.metrics.map((m) => [m.name, m.bound_type]),
        [
          ["gaap_sum", "DECIMAL(38,2)"],
          ["gaap_value_count", "BIGINT"],
          ["mean_gaap", "DOUBLE"],
          ["record_count", "BIGINT"],
        ],
      );
      assert.equal(first.fields.length, 7);
      const date = first.fields.find((f) => f.name === "ds")!;
      assert.equal(date.is_time, true);
      assert.equal(date.is_time_explicit, false);
      assert.equal(date.bound_type, "DATE");
      assert.equal(date.qualified_name, "dws_gaap_di.ds");
      assert.equal(date.object_column, "dws_gaap_di.ds");
    });
    const saved = await db.connection.prepare(
      "SELECT * FROM ossie_population_capabilities()",
    );
    try {
      await db.run("CALL ossie_bundle_load(?)", [
        join(ROOT, "test/fixtures/population/bundle-20.json"),
      ]);
      const newer = await discovery();
      check("prepared discovery retains immutable old bundle", () => {
        assert.notEqual(
          newer.capability!.snapshot_id,
          first.capability!.snapshot_id,
        );
      });
      assert.deepEqual((await saved.runAndReadAll()).getRowObjectsJson(), [
        first.capability,
      ]);
      await assert.rejects(() =>
        db.run("CALL ossie_bundle_load('missing.json')"),
      );
      assert.deepEqual(
        await db.records("SELECT * FROM ossie_population_capabilities()"),
        [newer.capability],
      );
      checks.push("failed reload preserves discovery");
      await db.run(
        "CALL ossie_load('test/fixtures/tpcds_semantic_model.json')",
      );
      assert.deepEqual(
        await db.records(
          "SELECT * FROM ossie_population_metrics() ORDER BY name",
        ),
        newer.metrics,
      );
      checks.push("independent core load cannot replace population vocabulary");
    } finally {
      saved.destroySync();
    }

    await db.run(
      readFileSync(
        join(ROOT, "test/fixtures/safe-join-research/rows.sql"),
        "utf8",
      ),
    );
    await db.run("CALL ossie_bundle_load(?)", [
      join(ROOT, "test/fixtures/joined-population/bundle.json"),
    ]);
    const joined = await discovery();
    check("non-GAAP original namespaces and capture capabilities", () => {
      assert.equal(joined.capability!.profile, "fact-dimension-population-v1");
      assert.equal(joined.capabilities.execution.capture_join.type, "INNER");
      assert.equal(
        joined.capabilities.execution.capture_join.missing,
        "reject",
      );
      assert(
        joined.fields.some((f) => f.qualified_name === 'dimension."label"'),
      );
      assert(!JSON.stringify(joined.fields).includes("_ossie_field_"));
      assert(!joined.metrics.some((m) => String(m.name).includes("gaap")));
    });
    // Names for the requests come from discovery. Hand-derived totals remain independent.
    const sum = joined.metrics.find((m) => m.name === "amount_sum")!;
    const count = joined.metrics.find(
      (m) => m.bound_type === "BIGINT" && m.name === "record_count",
    )!;
    const label = joined.fields.find(
      (f) => f.dataset === "dimension" && f.name === "label",
    )!;
    const concept = joined.capabilities.concepts[1];
    assert.deepEqual(
      await db.one("SELECT * FROM ossie_concept_query(?,?,[],[])", [
        concept,
        [String(count.name), String(sum.name)],
      ]),
      [4, "24.98"],
    );
    const groups = await db.rows(
      "SELECT * FROM ossie_concept_query(?,?,?,[])",
      [concept, [String(sum.name)], [String(label.qualified_name)]],
    );
    assert.deepEqual(groups, [["North", "24.98"]]);
    const rootGroups = await db.rows(
      "SELECT * FROM ossie_concept_query(?,?,?,[]) ORDER BY 1",
      [
        joined.capabilities.concepts[0],
        [String(sum.name)],
        [String(label.qualified_name)],
      ],
    );
    assert.deepEqual(rootGroups, [
      ["North", "24.98"],
      ["South", "30.03"],
    ]);
    checks.push(
      "non-GAAP discovered names drive real aggregation and grouping",
    );
    await assert.rejects(
      () =>
        db.rows("SELECT * FROM ossie_concept_query(?,?,[],[])", [
          concept,
          [String(sum.name), String(sum.name)],
        ]),
      /duplicate column/i,
    );
    checks.push(
      "single preflight does not promise combined duplicate output binding",
    );

    await temporary("ossie-discovery-", async (directory) => {
      const base = join(ROOT, "test/fixtures/joined-population");
      const core = json<any>(join(base, "core.json"));
      const ontology = json<any>(join(base, "ontology.json"));
      const mapping = json<any>(join(base, "mapping.json"));
      // Reverse original datasets and add computed/quoted vocabulary. Logical names are generic.
      core.datasets.reverse();
      const dim = core.datasets.find((d: any) => d.name === "dimension");
      dim.fields.push({
        name: 'display "label',
        datatype: "String",
        label: "Display label",
        description: "Computed retail region",
        dimension: { is_time: false },
        ai_context: {
          instructions: "Use for grouping",
          synonyms: ["region"],
          examples: ["north"],
          vendor: "retained",
        },
        custom_extensions: { "vendor.unit": "text" },
        expression: {
          dialects: [
            {
              dialect: "ANSI_SQL",
              expression:
                "CASE WHEN label IS NULL THEN 'missing' ELSE label END",
            },
          ],
        },
      });
      const manifest = json<any>(join(base, "bundle.json"));
      for (const [kind, doc] of Object.entries({ core, ontology, mapping })) {
        const path = join(directory, kind + ".json");
        writeFileSync(path, JSON.stringify(doc));
        manifest[kind] = {
          path,
          name: (doc as any).name,
          sha256: digest(path),
        };
      }
      const path = join(directory, "bundle.json");
      writeFileSync(path, JSON.stringify(manifest));
      const beforeMalformed = await db.rows(
        "SELECT * FROM ossie_population_fields() ORDER BY dataset,name",
      );
      await assert.rejects(
        db.run("CALL ossie_bundle_load(?)", [path]),
        /field 'custom_extensions' must be an array/,
      );
      assert.deepEqual(
        await db.rows(
          "SELECT * FROM ossie_population_fields() ORDER BY dataset,name",
        ),
        beforeMalformed,
      );
      checks.push(
        "malformed descriptive extension refuses without changing discovery",
      );
      dim.fields.at(-1).custom_extensions = [
        { vendor_name: "vendor.unit", data: "text" },
      ];
      writeFileSync(manifest.core.path, JSON.stringify(core));
      manifest.core.sha256 = digest(manifest.core.path);
      writeFileSync(path, JSON.stringify(manifest));
      await db.run("CALL ossie_bundle_load(?)", [path]);
      const synthetic = await discovery();
      const field = synthetic.fields.find((f) => f.name === 'display "label')!;
      check("reversed dataset and computed quoted field provenance", () => {
        assert.equal(field.dataset, "dimension");
        assert.equal(field.bound_type, "VARCHAR");
        assert.equal(field.qualified_name, 'dimension."display ""label"');
        assert.equal(field.object_column, 'dimension.display "label');
        assert.equal(field.label, "Display label");
        assert.equal(field.description, "Computed retail region");
        assert.equal(field.is_computed, true);
        assert.equal(field.is_time_explicit, true);
        assert.equal(field.is_time, false);
        assert.deepEqual(JSON.parse(String(field.synonyms_json)), ["region"]);
        assert.deepEqual(JSON.parse(String(field.examples_json)), ["north"]);
        assert.equal(field.instructions, "Use for grouping");
        assert.equal(
          JSON.parse(String(field.ai_context_json)).vendor,
          "retained",
        );
        assert.deepEqual(JSON.parse(String(field.custom_extensions_json)), [
          { vendor_name: "vendor.unit", data: "text" },
        ]);
      });
      assert.deepEqual(
        await db.rows("SELECT * FROM ossie_concept_query(?,?,?,[])", [
          concept,
          [String(sum.name)],
          [String(field.qualified_name)],
        ]),
        [["North", "24.98"]],
      );
      const old = await db.connection.prepare(
        "SELECT * FROM ossie_population_fields() ORDER BY dataset,name",
      );
      try {
        writeFileSync(join(directory, "core.json"), "{}");
        assert.deepEqual(
          await db.records(
            "SELECT * FROM ossie_population_fields() ORDER BY dataset,name",
          ),
          synthetic.fields,
        );
        await db.run("CALL ossie_bundle_load(?)", [join(base, "bundle.json")]);
        assert.deepEqual(
          (await old.runAndReadAll()).getRowObjectsJson(),
          synthetic.fields,
        );
        assert(!(await discovery()).fields.some((f) => f.name === field.name));
        checks.push(
          "file edits and reload cannot alter bound original vocabulary",
        );
      } finally {
        old.destroySync();
      }
    });
    return {
      artifact_sha256: digest(artifact),
      verifier_sha256: digest(import.meta.path),
      population_source_sha256: digest(
        join(ROOT, "src/population/population.cpp"),
      ),
      engine,
      checks,
      observations,
    };
  } finally {
    db.close();
  }
}
if (import.meta.main) emitReport(await verify(resolve(process.argv[2]!)));
