/** Real callback envelopes, preserved transactions and external error boundaries. */
import { join, resolve } from "node:path";
import { writeFileSync } from "node:fs";
import {
  Database,
  assert,
  digest,
  json,
  ROOT,
  temporary,
  emitReport,
} from "./artifact_helpers.ts";
import {
  ERROR_MARKER,
  populationError,
  type PopulationError,
} from "./population_error.ts";
export async function verify(artifact: string) {
  const db = await Database.open(),
    checks: string[] = [],
    errors: unknown[] = [];
  async function refuse(
    name: string,
    sql: string,
    parameters: (string | string[])[] = [],
    stage?: string,
  ) {
    let envelope: PopulationError | undefined;
    await assert.rejects(
      () => db.run(sql, parameters),
      (error: Error) => {
        envelope = populationError(error.message);
        assert(envelope, error.message);
        assert.equal(envelope.schema_version, "ossie-population-error-v1");
        if (stage) assert.equal(envelope.stage, stage, error.message);
        // JSON inside a diagnostic can contain the marker; it must never become the outer error.
        assert(!envelope.reason.startsWith(ERROR_MARKER), "nested envelope");
        errors.push({ name, envelope, carrier: error.message });
        return true;
      },
    );
    checks.push(name);
    return envelope!;
  }
  try {
    const engine = await db.loadArtifact(artifact);
    for (const endpoint of [
      "ossie_concepts",
      "ossie_population_metrics",
      "ossie_population_fields",
      "ossie_population_capabilities",
    ])
      assert.equal(
        (
          await refuse(
            "unloaded " + endpoint,
            `SELECT * FROM ${endpoint}()`,
            [],
            "bind",
          )
        ).object,
        "bundle",
      );
    for (const value of [
      "null",
      "{}",
      "1",
      '"x"',
      "[1]",
      "[true]",
      "[null]",
      "[{}]",
      "[[]]",
      '["ok",null]',
      "[",
      "[] trailing",
    ])
      await refuse(
        "JSON list " + value,
        "SELECT ossie_population_strings(?)",
        [value],
        "arguments",
      );
    await refuse(
      "SQL NULL list",
      "SELECT ossie_population_strings(NULL)",
      [],
      "arguments",
    );
    // SQL helper preserves string bytes, including NUL; downstream expression/name validation remains separate.
    const [valid] = await db.records(
      'SELECT ossie_population_strings(\'["a,b;引号\\"\\\\\\n","\\u0000"]\') AS items',
    );
    assert.deepEqual(valid!.items, ['a,b;引号"\\\n', "\0"]);
    checks.push(
      "strict helper preserves special and length-aware NUL string bytes",
    );
    const [empty] = await db.records(
      "SELECT ossie_population_strings('[]') AS items",
    );
    assert.deepEqual(empty!.items, []);
    checks.push("valid empty list is explicit");
    await db.run(
      "CREATE TABLE dws_gaap_di(ds DATE,product VARCHAR,region VARCHAR,user_type VARCHAR,user_id VARCHAR,gaap_val DECIMAL(18,2),ytd_gaap DECIMAL(18,2))",
    );
    await db.run(
      "COPY dws_gaap_di FROM 'docs/research/ossie-req-001/gaap-boundary.csv' (HEADER)",
    );
    const path = join(ROOT, "test/fixtures/population/bundle-10.json");
    await db.run("CALL ossie_bundle_load(?)", [path]);
    for (const endpoint of [
      "ossie_objects",
      "ossie_concept_query",
      "ossie_concept_compile",
    ]) {
      const query =
        endpoint === "ossie_objects"
          ? `SELECT * FROM ${endpoint}(?,[])`
          : `SELECT * FROM ${endpoint}(?,['gaap_sum'],[],[])`;
      const value =
        '不存在 "\\\n ' + ERROR_MARKER + '{"schema_version":"fake"}';
      const e = await refuse(
        endpoint + " unknown quoted concept",
        query,
        [value],
        "bind",
      );
      assert.equal(e.object, value);
      assert.equal(e.code, "INVALID_REQUEST");
      assert.equal(e.missing_capability, null);
    }
    await refuse(
      "core metric resolution wrapped",
      "SELECT * FROM ossie_concept_query('User',['ghost'],[],[])",
      [],
      "bind",
    );
    await refuse(
      "core dimension resolution wrapped",
      "SELECT * FROM ossie_concept_query('User',['gaap_sum'],['dws_gaap_di.ghost'],[])",
      [],
      "bind",
    );
    await refuse(
      "SQL NULL list member wrapped",
      "SELECT * FROM ossie_concept_query('User',['gaap_sum',NULL],[],[])",
      [],
      "bind",
    );
    await refuse(
      "unknown attribute",
      "SELECT * FROM ossie_objects('User',['User.secret = 1'])",
      [],
      "rule",
    );
    await refuse(
      "scalar AST refusal",
      "SELECT * FROM ossie_objects('User',['random() > 0'])",
      [],
      "rule",
    );
    await refuse(
      "parser preserves actual rule stage",
      "SELECT * FROM ossie_objects('User',['TRUE; SELECT 1'])",
      [],
      "rule",
    );
    await refuse(
      "conversion execute envelope",
      "SELECT * FROM ossie_concept_query('User',['gaap_sum'],[],['CAST(User.product AS INTEGER)>0'])",
      [],
      "execute",
    );
    const cap = await refuse(
      "request capability",
      "SELECT * FROM ossie_concept_query('User',list_resize(['gaap_sum'],65,'gaap_sum'),[],[])",
      [],
      "capability",
    );
    assert.equal(cap.code, "UNSUPPORTED_CAPABILITY");
    assert(cap.missing_capability?.includes("64 metrics"));
    const snapshot = await db.rows(
      "SELECT * FROM ossie_population_capabilities()",
    );
    await refuse(
      "missing file load",
      "CALL ossie_bundle_load(?)",
      [path + ".missing"],
      "load",
    );
    assert.deepEqual(
      await db.rows("SELECT * FROM ossie_population_capabilities()"),
      snapshot,
    );
    checks.push("load failure preserves frozen bundle");
    await temporary("ossie-errors-", async (directory) => {
      const manifest = json<any>(path);
      for (const kind of ["core", "ontology", "mapping"])
        manifest[kind].path = resolve(
          ROOT,
          "test/fixtures/population",
          manifest[kind].path,
        );
      const invalid = join(directory, "invalid.json");
      writeFileSync(
        invalid,
        JSON.stringify({ ...manifest, profile: "unknown" }),
      );
      const e = await refuse(
        "load preserves inner capability stage",
        "CALL ossie_bundle_load(?)",
        [invalid],
        "capability",
      );
      assert.equal(e.code, "UNSUPPORTED_CAPABILITY");
      writeFileSync(
        invalid,
        JSON.stringify({
          ...manifest,
          core: { ...manifest.core, sha256: "0".repeat(64) },
        }),
      );
      await refuse("hash failure stage", "CALL ossie_bundle_load(?)", [
        invalid,
      ]);
      // Rehash the deliberately malformed core: the refusal must come from the
      // loader, rather than failing the manifest integrity gate first.
      const core = json<any>(manifest.core.path);
      core.ai_context = { examples: ["valid", true] };
      const invalidCore = join(directory, "nonstring-core.json");
      writeFileSync(invalidCore, JSON.stringify(core));
      writeFileSync(
        invalid,
        JSON.stringify({
          ...manifest,
          core: {
            ...manifest.core,
            path: invalidCore,
            sha256: digest(invalidCore),
          },
        }),
      );
      const before = await db.rows("SELECT * FROM ossie_concepts()");
      const badString = await refuse(
        "core string array load refusal",
        "CALL ossie_bundle_load(?)",
        [invalid],
        "load",
      );
      assert.equal(badString.code, "LOAD_FAILED");
      assert(
        badString.reason.includes(
          "field 'examples' member at index 1 must be a string",
        ),
      );
      assert.deepEqual(await db.rows("SELECT * FROM ossie_concepts()"), before);
      checks.push("hash-valid malformed core retains frozen bundle");
      core.ai_context = { instructions: "before\0after" };
      writeFileSync(invalidCore, JSON.stringify(core));
      writeFileSync(
        invalid,
        JSON.stringify({
          ...manifest,
          core: {
            ...manifest.core,
            path: invalidCore,
            sha256: digest(invalidCore),
          },
        }),
      );
      await refuse(
        "population profile still refuses NUL core metadata",
        "CALL ossie_bundle_load(?)",
        [invalid],
        "parse",
      );
      assert.deepEqual(await db.rows("SELECT * FROM ossie_concepts()"), before);
      checks.push("NUL profile refusal retains previous frozen bundle");
      core.ai_context = { instructions: "legal" };
      core.description = false;
      writeFileSync(invalidCore, JSON.stringify(core));
      writeFileSync(
        invalid,
        JSON.stringify({
          ...manifest,
          core: {
            ...manifest.core,
            path: invalidCore,
            sha256: digest(invalidCore),
          },
        }),
      );
      const badScalar = await refuse(
        "strict core optional scalar type",
        "CALL ossie_bundle_load(?)",
        [invalid],
        "load",
      );
      assert.equal(badScalar.code, "LOAD_FAILED");
      assert(badScalar.reason.includes("field 'description' must be a string"));
      assert.deepEqual(await db.rows("SELECT * FROM ossie_concepts()"), before);
      checks.push("hash-valid core scalar refusal preserves frozen bundle");
      writeFileSync(invalid, "{invalid");
      await refuse(
        "document parse stage retained",
        "CALL ossie_bundle_load(?)",
        [invalid],
        "parse",
      );
    });
    await db.run(
      "UPDATE dws_gaap_di SET user_id='duplicate' WHERE user_id IN ('e01','e02')",
    );
    await refuse(
      "identity stage retained",
      "CALL ossie_bundle_load(?)",
      [path],
      "identity",
    );
    assert.deepEqual(
      await db.rows("SELECT * FROM ossie_population_capabilities()"),
      snapshot,
    );
    checks.push("identity failure does not publish candidate");

    // Native errors raised outside plugin callbacks are deliberately not labeled population errors.
    for (const [name, sql] of [
      [
        "outer duplicate binding",
        "SELECT * FROM ossie_concept_query('User',['gaap_sum','gaap_sum'],[],[])",
      ],
      ["SQL arity", "SELECT * FROM ossie_objects('User')"],
    ]) {
      await assert.rejects(
        () => db.run(sql!),
        (error: Error) => {
          assert.equal(populationError(error.message), undefined);
          errors.push({ name, external: error.message });
          return true;
        },
      );
      checks.push(name! + " stays external");
    }
    // Compare shared-core Invalid Input exceptions inside an explicit transaction.
    await db.run(
      "CALL ossie_load('docs/research/ossie-req-001/proposed-definitions/gaap.core.json')",
    );
    for (const [name, sql] of [
      ["core", "SELECT * FROM ossie_query(['ghost'])"],
      [
        "population",
        "SELECT * FROM ossie_concept_query('User',['ghost'],[],[])",
      ],
    ]) {
      await db.run("BEGIN");
      await assert.rejects(() => db.run(sql!));
      await assert.rejects(
        () => db.run("SELECT * FROM dws_gaap_di"),
        /aborted/i,
      );
      await db.run("ROLLBACK");
      checks.push(name! + " invalid-input transaction behavior retained");
    }
    assert.deepEqual(
      await db.one(
        "SELECT record_count FROM ossie_concept_query('User',['record_count'],[],[])",
      ),
      [11],
    );
    checks.push("real scan recovery after all refusals");
    return {
      artifact_sha256: digest(artifact),
      verifier_sha256: digest(import.meta.path),
      source_sha256: Object.fromEntries(
        [
          "src/population/population.cpp",
          "src/model/parser.cpp",
          "scripts/population_error.ts",
        ].map((p) => [p, digest(join(ROOT, p))]),
      ),
      engine,
      checks,
      errors,
    };
  } finally {
    db.close();
  }
}
if (import.meta.main) emitReport(await verify(resolve(process.argv[2]!)));
