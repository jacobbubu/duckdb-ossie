/** Independent CSV/typed-tuple oracle for the opt-in daily observation profile. */
import { readFileSync, writeFileSync } from "node:fs";
import { join, relative, resolve } from "node:path";
import {
  Database,
  ROOT,
  assert,
  digest,
  temporary,
  emitReport,
  sqlString,
  type Cell,
} from "./artifact_helpers.ts";
import { fixedSchemaValidator } from "./check_fixed_schema.ts";
import { populationError } from "./population_error.ts";
import { filesUnder } from "./check_architecture.ts";
const FIXTURES = join(ROOT, "test/fixtures/population-observation");
const CSV = join(
  ROOT,
  "docs/research/ossie-req-001/multi-period-research/observations.csv",
);
const METRICS = [
  "observation_count",
  "user_count",
  "amount_value_count",
  "amount_sum",
  "mean_observation_amount",
];
const QUERY = "SELECT * FROM ossie_concept_query(?, ?, ?, ?)";
const TABLE =
  "CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER)";
interface Observation {
  id: string;
  user: string;
  date: string;
  product: string;
  cents: bigint | null;
  classification: number;
}
const input: Observation[] = readFileSync(CSV, "utf8")
  .trim()
  .split("\n")
  .slice(1)
  .map((line) => {
    const [id, user, date, product, amount, classification] = line.split(",");
    return {
      id,
      user,
      date,
      product,
      cents: amount === "" ? null : BigInt(amount.replace(".", "")),
      classification: Number(classification),
    };
  });
// Independent tuple representation: JSON array framing preserves components and exact strings.
const tuple = (row: Observation) =>
  JSON.stringify([row.user, row.date, row.product]);
function oracle(rows: Observation[]): Cell[] {
  const values = rows.filter((r) => r.cents !== null);
  const cents = values.reduce((sum, r) => sum + r.cents!, 0n);
  return [
    rows.length,
    new Set(rows.map((r) => r.user)).size,
    values.length,
    values.length ? (Number(cents) / 100).toFixed(2) : null,
    values.length ? Number(cents) / 100 / values.length : null,
  ];
}
function equalAggregate(actual: Cell[], expected: Cell[]) {
  assert.deepEqual(actual.slice(0, 4), expected.slice(0, 4));
  if (expected[4] === null) assert.equal(actual[4], null);
  else assert(Math.abs(Number(actual[4]) - Number(expected[4])) <= 1e-12);
}
export async function verifyObservation(artifact: string) {
  const checks: string[] = [],
    refusals: { label: string; error: unknown }[] = [],
    results: unknown[] = [];
  const db = await Database.open();
  const validate = fixedSchemaValidator();
  const bundle = (threshold = 10) => join(FIXTURES, `bundle-${threshold}.json`);
  const load = (path = bundle()) => db.run("CALL ossie_bundle_load(?)", [path]);
  const state = () =>
    db.one(
      "SELECT snapshot_id,content_sha256,ontology_sha256 FROM ossie_population_capabilities()",
    );
  const check = (label: string, fn: () => void) => {
    fn();
    checks.push(label);
  };
  async function refuse(
    label: string,
    action: () => Promise<unknown>,
    expected: string,
  ) {
    const before = await state();
    let error: unknown;
    try {
      await action();
    } catch (e) {
      error = populationError(String(e));
    }
    assert(error, `${label}: normalized refusal required`);
    assert.equal((error as { code: string }).code, expected, label);
    assert.deepEqual(
      await state(),
      before,
      `${label}: previous snapshot retained`,
    );
    refusals.push({ label, error });
  }
  async function baseline() {
    await db.run("DROP TABLE IF EXISTS observations");
    await db.run(TABLE);
    await db.run(
      `INSERT INTO observations SELECT * FROM read_csv(${sqlString(CSV)},header=true)`,
    );
  }
  async function aggregate(
    label: string,
    threshold: number,
    predicate: (r: Observation) => boolean,
    filters: string[],
    dimensions: string[] = [],
  ) {
    const selected = input.filter(
      (r) => r.classification >= threshold && predicate(r),
    );
    const actual = await db.rows(QUERY, [
      "QualifiedObservation",
      METRICS,
      dimensions,
      filters,
    ]);
    if (!dimensions.length) equalAggregate(actual[0], oracle(selected));
    else {
      const groups = new Map<string, Observation[]>();
      for (const row of selected) {
        const key = JSON.stringify(
          dimensions.map((d) => (d.endsWith(".ds") ? row.date : row.product)),
        );
        groups.set(key, [...(groups.get(key) ?? []), row]);
      }
      assert.equal(actual.length, groups.size);
      for (const row of actual) {
        const group = groups.get(
          JSON.stringify(row.slice(0, dimensions.length)),
        );
        assert(group);
        equalAggregate(row.slice(dimensions.length), oracle(group));
      }
    }
    const objects = await db.rows("SELECT * FROM ossie_objects(?,?)", [
      "QualifiedObservation",
      filters,
    ]);
    check(label, () =>
      assert.deepEqual(
        objects.map((row) => JSON.stringify(row.slice(0, 3))).sort(),
        selected.map(tuple).sort(),
      ),
    );
    results.push({ label, actual });
  }
  try {
    const engine = await db.loadArtifact(artifact);
    await baseline();
    await load();
    check("valid batch preserves all eight observations", () =>
      assert.equal(input.length, new Set(input.map(tuple)).size),
    );
    await aggregate(
      "all products: independent counts, cents sum and nonnull mean",
      10,
      () => true,
      [],
    );
    await aggregate("X across dates", 10, (r) => r.product === "X", [
      "DailyObservation.product = 'X'",
    ]);
    await aggregate("Jan31 only", 10, (r) => r.date === "2026-01-31", [
      "observations.ds = DATE '2026-01-31'",
    ]);
    await aggregate("Feb1 only", 10, (r) => r.date === "2026-02-01", [
      "DailyObservation.date = DATE '2026-02-01'",
    ]);
    await aggregate("null-only amount", 10, (r) => r.user === "C", [
      "observations.user_id = 'C'",
    ]);
    await aggregate("empty scalar", 10, () => false, [
      "DailyObservation.product = 'missing'",
    ]);
    await aggregate(
      "date/product groups",
      10,
      () => true,
      [],
      ["observations.ds", "observations.product"],
    );
    const root = await db.rows(QUERY, ["DailyObservation", METRICS, [], []]);
    check("root has no qualification rule", () =>
      equalAggregate(root[0], oracle(input)),
    );
    const concepts = await db.records(
      "SELECT * FROM ossie_concepts_v2() ORDER BY concept",
    );
    check("structured identities, queryability and raw capture counts", () => {
      assert.equal(concepts.length, 3);
      for (const c of concepts) {
        const user = c.concept === "User";
        assert.equal(c.objects_queryable, !user);
        assert.equal(c.metrics_queryable, !user);
        assert.equal(c.captured_observation_rows, "8");
        assert.equal(c.captured_user_count, "4");
        const components = JSON.parse(String(c.identity_components_json));
        assert.deepEqual(
          components.map((r: any) => r.field),
          user ? ["user_id"] : ["user_id", "ds", "product"],
        );
        assert.deepEqual(
          components.map((r: any) => r.relationship),
          user ? ["id"] : ["user", "date", "product"],
        );
        if (!user)
          assert.deepEqual(components[0].referent, {
            concept: "User",
            relationship: "id",
          });
        assert.deepEqual(
          components.map((r: any) => r.bound_type),
          user ? ["VARCHAR"] : ["VARCHAR", "DATE", "VARCHAR"],
        );
        if (!user)
          assert.equal(
            JSON.parse(String(c.referenced_entity_identity_json))
              .entity_equals_record,
            false,
          );
      }
    });
    const [planText] = await db.one(
      "SELECT plan_json FROM ossie_concept_compile(?, ?, ?, ?)",
      ["QualifiedObservation", METRICS, [], []],
    );
    const plan = JSON.parse(String(planText));
    check("plan-v2 explains observation vs referenced User identity", () => {
      assert.equal(plan.schema_version, "ossie-population-plan-v2");
      assert.equal(plan.executed, false);
      assert.equal(plan.identity.entity_equals_record, true);
      assert.equal(plan.identity.internal_string_comparison, "binary");
      assert.equal(plan.identity.components.length, 3);
      assert.equal(plan.identity.referenced_entity.entity_equals_record, false);
      assert.equal(plan.snapshot.captured_population_rows, 8);
    });
    const [typed] = await db.one(
      "SELECT result_json FROM ossie_objects_json(?,?)",
      ["QualifiedObservation", []],
    );
    const objectResult = JSON.parse(String(typed));
    check("typed objects preserve tuple columns without synthetic IDs", () => {
      assert.equal(objectResult.schema_version, "ossie-population-result-v1");
      assert.deepEqual(
        objectResult.columns.map((c: any) => c.name),
        [
          "observations.user_id",
          "observations.ds",
          "observations.product",
          "observations.amount_usd",
          "observations.classification_value",
        ],
      );
      assert.equal(objectResult.row_count, 6);
    });
    const [caps] = await db.records(
      "SELECT * FROM ossie_population_capabilities()",
    );
    const capability = JSON.parse(String(caps.capabilities_json));
    check("active capabilities declare restrictions", () => {
      assert.equal(caps.api_version, "2.2.0");
      assert.equal(caps.plan_schema, "ossie-population-plan-v2");
      assert.equal(capability.concept_discovery.function, "ossie_concepts_v2");
      assert.equal(capability.observation_contract.user_queryable, false);
      assert.equal(
        capability.observation_contract.internal_string_comparison,
        "binary",
      );
      assert.deepEqual(capability.observation_contract.groupable_fields, [
        "observations.ds",
        "observations.product",
      ]);
    });
    await refuse(
      "legacy concepts refuses tuple profile",
      () => db.rows("SELECT * FROM ossie_concepts()"),
      "UNSUPPORTED_CAPABILITY",
    );
    for (const endpoint of ["ossie_objects", "ossie_objects_json"])
      await refuse(
        `${endpoint} User referent-only`,
        () => db.rows(`SELECT * FROM ${endpoint}(?,?)`, ["User", []]),
        "UNSUPPORTED_CAPABILITY",
      );
    for (const endpoint of [
      "ossie_concept_query",
      "ossie_concept_query_json",
      "ossie_concept_compile",
    ]) {
      await refuse(
        `${endpoint} User referent-only`,
        () =>
          db.rows(`SELECT * FROM ${endpoint}(?,?,?,?)`, [
            "User",
            ["user_count"],
            [],
            [],
          ]),
        "UNSUPPORTED_CAPABILITY",
      );
      for (const dimension of [
        "observations.user_id",
        "observations.amount_usd",
        "observations.classification_value",
      ])
        await refuse(
          `${endpoint} rejects ${dimension} group`,
          () =>
            db.rows(`SELECT * FROM ${endpoint}(?,?,?,?)`, [
              "QualifiedObservation",
              ["user_count"],
              [dimension],
              [],
            ]),
          "UNSUPPORTED_CAPABILITY",
        );
      await refuse(
        `${endpoint} rejects computed date group`,
        () =>
          db.rows(`SELECT * FROM ${endpoint}(?,?,?,?)`, [
            "QualifiedObservation",
            ["user_count"],
            ["observations.ds + 1"],
            [],
          ]),
        "MAPPING_INVALID",
      );
    }
    await refuse(
      "unsupported entity navigation filter",
      () =>
        db.rows("SELECT * FROM ossie_objects(?,?)", [
          "DailyObservation",
          ["User.id = 'A'"],
        ]),
      "INVALID_REQUEST",
    );
    await db.run(
      "PREPARE saved_obs AS SELECT * FROM ossie_concept_query('QualifiedObservation',['observation_count','user_count','amount_value_count','amount_sum','mean_observation_amount'],[],[])",
    );
    await db.run(
      "PREPARE saved_discovery AS SELECT concept, captured_observation_rows, captured_user_count FROM ossie_concepts_v2() ORDER BY concept",
    );
    const saved = await state();
    await load(bundle(20));
    await aggregate(
      "threshold20 changes qualification only",
      20,
      () => true,
      [],
    );
    const [twentyCapability] = await db.records(
      "SELECT * FROM ossie_population_capabilities()",
    );
    check("rule reload retains source/core/mapping bytes", () => {
      for (const field of ["content_sha256", "core_sha256", "mapping_sha256"])
        assert.equal(twentyCapability[field], caps[field]);
    });
    const twenty = await state();
    assert.equal(twenty[1], saved[1]);
    assert.notEqual(twenty[2], saved[2]);
    equalAggregate(
      await db.one("EXECUTE saved_obs"),
      oracle(input.filter((r) => r.classification >= 10)),
    );
    checks.push("prepared query keeps old qualified snapshot");
    await db.run(
      "UPDATE observations SET classification_value=0,amount_usd=999",
    );
    equalAggregate(
      (await db.rows(QUERY, ["QualifiedObservation", METRICS, [], []]))[0],
      oracle(input.filter((r) => r.classification >= 20)),
    );
    checks.push("source mutation does not change frozen query");
    await baseline();
    await load();
    // Identity checks must see the entire captured batch, including nonqualifying rows.
    for (const [label, mutation] of [
      [
        "identical duplicate below rule",
        "INSERT INTO observations SELECT * FROM observations WHERE observation_id='o1'",
      ],
      [
        "conflicting duplicate below rule",
        "INSERT INTO observations SELECT 'conflict',user_id,ds,product,999,0 FROM observations WHERE observation_id='o1'",
      ],
      [
        "empty User",
        "UPDATE observations SET user_id='' WHERE observation_id='o1'",
      ],
      [
        "null User",
        "UPDATE observations SET user_id=NULL WHERE observation_id='o1'",
      ],
      [
        "empty product",
        "UPDATE observations SET product='' WHERE observation_id='o1'",
      ],
      [
        "null product",
        "UPDATE observations SET product=NULL WHERE observation_id='o1'",
      ],
      [
        "null DATE",
        "UPDATE observations SET ds=NULL WHERE observation_id='o1'",
      ],
      [
        "infinite DATE",
        "UPDATE observations SET ds=DATE 'infinity' WHERE observation_id='o1'",
      ],
      [
        "negative infinite DATE",
        "UPDATE observations SET ds=DATE '-infinity' WHERE observation_id='o1'",
      ],
    ]) {
      await baseline();
      await db.run(mutation);
      await refuse(label, () => load(), "IDENTITY_VIOLATION");
    }
    for (const [label, ddl, code] of [
      [
        "collated User",
        TABLE.replace("user_id VARCHAR", "user_id VARCHAR COLLATE NOCASE"),
        "SNAPSHOT_REJECTED",
      ],
      [
        "collated product",
        TABLE.replace("product VARCHAR", "product VARCHAR COLLATE NOCASE"),
        "SNAPSHOT_REJECTED",
      ],
      [
        "TIMESTAMP DATE coercion",
        TABLE.replace("ds DATE", "ds TIMESTAMP"),
        "SNAPSHOT_REJECTED",
      ],
      [
        "numeric User coercion",
        TABLE.replace("user_id VARCHAR", "user_id INTEGER"),
        "INVALID_REQUEST",
      ],
    ]) {
      await db.run("DROP TABLE observations");
      await db.run(ddl);
      await refuse(label, () => load(), code);
    }
    await baseline();
    await temporary("ossie-observation-definitions-", async (directory) => {
      const original = ["core", "ontology", "mapping"].map((kind) =>
        JSON.parse(readFileSync(join(FIXTURES, `${kind}.json`), "utf8")),
      );
      async function changed(
        label: string,
        change: (documents: any[]) => void,
        code: string,
      ) {
        const documents = structuredClone(original);
        change(documents);
        const manifest: any = { profile: "daily-observation-population-v1" };
        for (let i = 0; i < 3; i++) {
          const kind = ["core", "ontology", "mapping"][i] as
            | "core"
            | "ontology"
            | "mapping";
          const path = join(directory, `${kind}.json`);
          writeFileSync(path, JSON.stringify(documents[i]));
          assert(
            validate(kind, documents[i]).accepted,
            `${label}: schema acceptance is separate from capability`,
          );
          manifest[kind] = {
            path,
            name: documents[i].name,
            sha256: digest(path),
          };
        }
        const path = join(directory, "bundle.json");
        writeFileSync(path, JSON.stringify(manifest));
        await refuse(label, () => load(path), code);
      }
      for (const expression of [
        "SUM(DISTINCT observations.amount_usd)",
        "AVG(DISTINCT observations.amount_usd)",
        "COUNT(DISTINCT observations.amount_usd)",
        "SUM(observations.amount_usd)/NULLIF(COUNT(DISTINCT observations.user_id),0)",
        "COUNT(observations.product)",
      ])
        await changed(
          `metric shape ${expression}`,
          ([core]) => {
            core.metrics[3].expression.dialects[0].expression = expression;
          },
          "UNSUPPORTED_CAPABILITY",
        );
      await changed(
        "computed identity",
        ([core]) => {
          core.datasets[0].fields[0].expression.dialects[0].expression =
            "CAST(user_id AS VARCHAR)";
        },
        "IDENTITY_VIOLATION",
      );
      await changed(
        "computed amount hidden under field",
        ([core]) => {
          core.datasets[0].fields[3].expression.dialects[0].expression =
            "amount_usd + 1";
        },
        "UNSUPPORTED_CAPABILITY",
      );
      await changed(
        "primary key omits product",
        ([core]) => {
          core.datasets[0].primary_key = ["user_id", "ds"];
        },
        "IDENTITY_VIOLATION",
      );
      await changed(
        "User mapping mismatch",
        ([, , mapping]) => {
          mapping.concept_mappings[0].object_mappings[0].referent_mappings[0].expression =
            "observations.product";
        },
        "MAPPING_INVALID",
      );
      await changed(
        "User link mismatch",
        ([, , mapping]) => {
          mapping.concept_mappings[0].link_mappings[0].children[0].object_mapping.expression =
            "observations.product";
        },
        "MAPPING_INVALID",
      );
      await changed(
        "nested User mismatch",
        ([, , mapping]) => {
          mapping.concept_mappings[1].object_mappings[0].referent_mappings[0].referent_mappings[0].expression =
            "observations.product";
        },
        "MAPPING_INVALID",
      );
      await changed(
        "observation scalar link override",
        ([, , mapping]) => {
          mapping.concept_mappings[1].link_mappings[0].children[2].object_mapping.expression =
            "observations.user_id";
        },
        "MAPPING_INVALID",
      );
      await changed(
        "unsupported formal OR rule",
        ([, onto]) => {
          onto.ontology[2].derived_by = [
            "DailyObservation.classification_value >= 10 OR DailyObservation.amount > 0",
          ];
        },
        "UNSUPPORTED_CAPABILITY",
      );
      await changed(
        "unsupported formal amount rule",
        ([, onto]) => {
          onto.ontology[2].derived_by = ["DailyObservation.amount >= 10"];
        },
        "UNSUPPORTED_CAPABILITY",
      );
      await changed(
        "unsupported noninteger threshold",
        ([, onto]) => {
          onto.ontology[2].derived_by = [
            "DailyObservation.classification_value >= 10.5",
          ];
        },
        "UNSUPPORTED_CAPABILITY",
      );
    });
    // Binary string identity cases, component framing, finite date boundaries and repeated users.
    await db.run("DELETE FROM observations");
    const identityRows = [
      ["A", "X"],
      ["a", "X"],
      ["A ", "X"],
      ["é", "X"],
      ["é", "X"],
      ["A\u0000tail", "X"],
      ["a|2026-02-01|b", "c"],
      ["a", "b|2026-02-01|c"],
      ["A", "X "],
      ["A", "x"],
      ["A", "é"],
      ["A", "é"],
    ];
    const collision = identityRows
      .slice(6, 8)
      .map(([user, product]) => [user, "2026-02-01", product]);
    assert.equal(collision[0].join("|"), collision[1].join("|"));
    assert.notEqual(JSON.stringify(collision[0]), JSON.stringify(collision[1]));
    for (const [user, product] of identityRows)
      await db.run(
        "INSERT INTO observations VALUES ('case',?,DATE '2026-02-01',?,1,10)",
        [user, product],
      );
    await db.run(
      "INSERT INTO observations VALUES ('min','A',DATE '0001-01-01','X',1,10),('max','A',DATE '9999-12-31','X',1,10)",
    );
    await db.run("SET GLOBAL default_collation='nocase'");
    await load();
    const special = await db.one(QUERY, [
      "QualifiedObservation",
      METRICS,
      [],
      [],
    ]);
    check("binary identities and framed tuple collisions stay distinct", () =>
      assert.deepEqual(special.slice(0, 4), [
        identityRows.length + 2,
        new Set(identityRows.map(([user]) => user)).size,
        identityRows.length + 2,
        "14.00",
      ]),
    );
    check("caller global nocase remains unchanged", () => {});
    assert.deepEqual(
      await db.one("SELECT current_setting('default_collation')"),
      ["nocase"],
    );
    const other = await db.instance.connect();
    try {
      assert.deepEqual(
        (
          await other.runAndReadAll(
            "SELECT current_setting('default_collation')",
          )
        ).getRows(),
        [["nocase"]],
      );
    } finally {
      other.disconnectSync();
    }
    assert.deepEqual(
      (
        await db.one(QUERY, [
          "QualifiedObservation",
          METRICS,
          [],
          ["observations.user_id = 'A'"],
        ])
      ).slice(0, 4),
      [7, 1, 7, "7.00"],
    );
    checks.push(
      "internal identity filter stays case-sensitive under global nocase",
    );
    const productGroups = await db.rows(QUERY, [
      "QualifiedObservation",
      ["observation_count", "user_count"],
      ["observations.product"],
      [],
    ]);
    const expectedGroups = new Map<string, string[]>();
    for (const [user, product] of [...identityRows, ["A", "X"], ["A", "X"]])
      expectedGroups.set(product, [
        ...(expectedGroups.get(product) ?? []),
        user,
      ]);
    assert.equal(productGroups.length, expectedGroups.size);
    for (const [product, count, users] of productGroups) {
      const expected = expectedGroups.get(String(product));
      assert(expected);
      assert.deepEqual(
        [count, users],
        [expected.length, new Set(expected).size],
      );
    }
    checks.push("internal product groups stay binary under global nocase");
    await db.run(
      "PREPARE saved_binary AS SELECT * FROM ossie_concept_query('QualifiedObservation',['observation_count','user_count','amount_value_count','amount_sum','mean_observation_amount'],[],[])",
    );
    await db.run(
      "INSERT INTO observations VALUES ('binary_duplicate','A',DATE '2026-02-01','X',1,10)",
    );
    await refuse(
      "true tuple duplicate under global nocase",
      () => load(),
      "IDENTITY_VIOLATION",
    );
    await db.run(
      "DELETE FROM observations WHERE observation_id='binary_duplicate'",
    );
    await db.run("SET GLOBAL default_collation='noaccent'");
    assert.deepEqual(await db.one("EXECUTE saved_binary"), special);
    assert.deepEqual(
      await db.one(QUERY, ["QualifiedObservation", METRICS, [], []]),
      special,
    );
    checks.push("prepared and fresh queries retain binary after global change");
    await db.run("SET GLOBAL default_collation=''");
    await db.run(
      "INSERT INTO observations VALUES ('null-rule','Z',DATE '2026-02-01','X',1,NULL)",
    );
    await load();
    assert.deepEqual(
      (await db.one(QUERY, ["QualifiedObservation", METRICS, [], []])).slice(
        0,
        4,
      ),
      special.slice(0, 4),
    );
    checks.push("NULL qualification is excluded");
    await db.run("DELETE FROM observations");
    await load();
    equalAggregate(
      await db.one(QUERY, ["QualifiedObservation", METRICS, [], []]),
      [0, 0, 0, null, null],
    );
    checks.push("empty captured batch publishes zero counts");
    // Old APIs remain usable and previously bound observation objects remain attached to old Bundle.
    await db.run(
      "CREATE TABLE dws_gaap_di(ds DATE,product VARCHAR,region VARCHAR,user_type VARCHAR,user_id VARCHAR,gaap_val DECIMAL(18,2),ytd_gaap DECIMAL(18,2))",
    );
    await db.run(
      `INSERT INTO dws_gaap_di SELECT * FROM read_csv(${sqlString(join(ROOT, "docs/research/ossie-req-001/gaap-boundary.csv"))},header=true)`,
    );
    await load(join(ROOT, "test/fixtures/population/bundle-10.json"));
    assert.equal((await db.rows("SELECT * FROM ossie_concepts()")).length, 2);
    const adapted = await db.records("SELECT * FROM ossie_concepts_v2()");
    assert.equal(adapted.length, 2);
    for (const row of adapted)
      assert.equal(JSON.parse(String(row.identity_components_json)).length, 1);
    assert.equal((await db.rows("EXECUTE saved_discovery")).length, 3);
    equalAggregate(
      await db.one("EXECUTE saved_obs"),
      oracle(input.filter((r) => r.classification >= 10)),
    );
    checks.push(
      "old scalar profile and prepared observation discovery/query coexist",
    );
    await baseline();
    await load();
    const final = await state();
    const files = [
      ...filesUnder(join(ROOT, "src")).filter((p) => /\.(cpp|hpp)$/.test(p)),
      ...filesUnder(FIXTURES),
      CSV,
      join(ROOT, "scripts/verify_observation_artifact.ts"),
      join(ROOT, "scripts/check_fixed_schema.ts"),
    ];
    return {
      passed: true,
      engine,
      artifact_sha256: digest(artifact),
      artifact_loaded_in_stock_node_bindings: true,
      checks,
      refusals,
      results,
      plan,
      concepts,
      final_snapshot: final,
      source_hashes: Object.fromEntries(
        files.map((path) => [relative(ROOT, path), digest(path)]),
      ),
      scope:
        "Local observation SQL profile and legacy regression; no new MCP or live Agent acceptance",
    };
  } finally {
    db.close();
  }
}
if (import.meta.main)
  emitReport(await verifyObservation(resolve(process.argv[2]!)));
