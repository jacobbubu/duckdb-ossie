# Daily observation population

`daily-observation-population-v1` is an opt-in local SQL profile for repeated
Users observed on multiple dates and products. A User has a raw String identity;
an observation has the typed `(user_id, DATE, product)` tuple. The profile keeps
all valid observations and counts distinct Users within the requested observation
scope. It does not implement period-end, every-day qualification, carry-forward,
user-level averages or general entity navigation.

This is a project implementation profile for the fixed Ossie `0.2.0.dev0` snapshot
and `OSSIE_SQL_2026`. The profile names, capability restrictions and SQL metadata
versions are project contracts, not additional upstream standards.

## Definitions and load

The schema-valid example is in `test/fixtures/population-observation`. It declares
one dataset, five untransformed fields (String User id, Date, String product,
Decimal amount, Integer qualification value), three distinct concepts and two
root mappings. Concept, relationship, field and metric names may differ; roles,
mapping correspondence and expanded metric expression shapes determine support.

The referenced User has only its OneToOne String id. Observation identity nests
that User id referent beside date/product referents. The User.id link belongs to
its own root link tree; it does not create a third layer under Observation.user.
The derived observation extends only the observation root and has one bound
Integer qualification field `>=` a non-null integer constant. Requests apply
that rule and then narrower observation filters; NULL qualification is excluded.

```sql
CREATE TABLE observations(
  observation_id VARCHAR, user_id VARCHAR, ds DATE, product VARCHAR,
  amount_usd DECIMAL(18,2), classification_value INTEGER);
INSERT INTO observations SELECT * FROM read_csv(
  'docs/research/ossie-req-001/multi-period-research/observations.csv', header=true);
CALL ossie_bundle_load('test/fixtures/population-observation/bundle-10.json');
```

`observation_id` is diagnostic source data; it does not decide tuple identity.
Source tables must be committed native tables with a fully qualified name and
no generated columns. The existing capture bounds apply: 10,000 rows, 256
physical columns and 16 MiB canonical values. Oversized inputs are refused,
never truncated. Capture and later queries use the frozen typed batch.

The whole batch is validated before publication, independently of qualification
or request filters. User/product must be non-null and nonempty; DATE must be
non-null and finite. Identical and conflicting duplicate tuples both refuse.
Repeated User ids across dates/products are valid. The distinct captured User
set is counted only after tuple validation; observations are not deduplicated.

Identity fields must be plain physical VARCHAR/DATE/VARCHAR columns. Casts,
computed identity and collated captured VARCHAR columns refuse. Equality keeps
case, trailing spaces, Unicode representation and embedded zero bytes; there is
no normalization or concatenated identity. Each private observation Engine session
pins binary comparison, even when the database default is nocase/noaccent.
Caller/global settings remain unchanged. Comparisons added in outer SQL around
the API still follow that caller's settings; the profile contract covers its
internal request filters, grouping and metrics. Failed or cancelled loads retain the
previous bundle, and already prepared queries retain their bound snapshot.

## Queries and denominators

The registered metric set must contain exactly these five aggregate shapes;
changing their names cannot authorize another formula. Core fields cannot hide
calculations behind these shapes.

| Shape | Meaning in the requested qualifying observation scope |
|---|---|
| `COUNT(user_id)` | Observation count; identity preflight makes User id non-null |
| `COUNT(DISTINCT user_id)` | Users with at least one qualifying observation in scope |
| `COUNT(amount)` | Observations with a non-null amount |
| `SUM(amount)` | Sum of non-null observation amounts |
| `AVG(amount)` | Amount sum divided by non-null amount observations |

The fixture uses synthetic additive USD amounts. These are not GAAP or cumulative
YTD amounts. The User mean-of-means, average User total and total divided by all
Users are separate measures and are unsupported in this slice.

```sql
SELECT * FROM ossie_concept_query(
  'QualifiedObservation',
  ['observation_count','user_count','amount_value_count',
   'amount_sum','mean_observation_amount'], [], []);
-- 6 observations, 3 Users, 5 non-null amounts, USD 31.00, USD 6.2

SELECT * FROM ossie_concept_query(
  'QualifiedObservation', ['user_count'], ['observations.ds'], []);
```

Only date/product logical fields may group results. Scalar filters reference
bound observation fields/attributes; general User navigation is unsupported.
User is referent-only: its direct object/metric/compile requests return
`UNSUPPORTED_CAPABILITY`. Empty ungrouped requests return zero counts and NULL
SUM/AVG; empty grouped requests have no rows. NULL-only amount groups retain
observation/User counts but have zero amount count and NULL SUM/AVG.

Objects return the five ordinary logical columns, including all typed identity
components, without synthesized `entity_id`/`record_id`. The optional typed JSON
adapters retain `ossie-population-result-v1`. Both old profiles retain their
existing object columns and plan-v1 behavior.

## Structured discovery and plan

```sql
SELECT * FROM ossie_concepts_v2();
SELECT * FROM ossie_population_capabilities();
SELECT plan_json FROM ossie_concept_compile(
  'QualifiedObservation', ['user_count'], [], []);
```

`ossie_concepts_v2()` returns concept, nullable parent/rule,
`identity_components_json`, nullable `referenced_entity_identity_json`,
`objects_queryable`, `metrics_queryable`, `captured_observation_rows`,
`captured_user_count`, profile/source and the existing definition/snapshot hashes.
Each component includes its identity relationship, dataset, logical field and
actual bound type; the observation User component explains its nested User.id
referent. The User
row is not queryable; observation rows expose their referenced User identity.
The captured counts are 8 observations/4 Users for every discovery row in this
fixture: they describe the raw batch, not the 6/3 qualifying query population.

The same function adapts old profiles to a single identity component. Legacy
`ossie_concepts()` keeps its old schema for the two old profiles and refuses this
compound profile with guidance to use v2. Metrics/fields discovery retain their
v1 shapes; field presence alone does not authorize a grouping request.

The additive project API is `2.1.0`. Capabilities identify the active concept
function/schema, permitted group fields and User/mean scope. The new profile's
`ossie-population-plan-v2` explains the observation tuple (`entity_equals_record`
true) separately from the referenced User (false), and states the internal binary
string comparison. Old profiles still produce
`ossie-population-plan-v1`. Both plans describe generated ASTs, not executed or
physical plans; typed result evidence establishes actual execution.

## Validation and limits

The pinned local schemas are checked by `scripts/check_fixed_schema.ts` with
Ajv Draft2020 and format validation. Upstream `$ref` URLs resolve only to locally
registered byte-hash-verified schemas; no remote schema loader is installed.
Run the following from the repository root, choosing a new report path:

```sh
bun scripts/check_fixed_schema.ts \
  --core test/fixtures/population-observation/core.json \
  --ontology test/fixtures/population-observation/ontology.json \
  --mapping test/fixtures/population-observation/mapping.json \
  --out /tmp/observation-schema-report.json
bun scripts/verify_observation_artifact.ts \
  build/release/extension/ossie/ossie.duckdb_extension \
  --output /tmp/observation-artifact-report.json
```

JSON Schema acceptance is a tooling gate; the C++ loader separately enforces its
bounded profile, actual source types and data identities. Schema acceptance does
not prove business binding, calculation or execution. The schema validator is
not embedded in the extension and does not broaden production capabilities.
[Ajv's official Draft2020 documentation](https://ajv.js.org/json-schema.html)
explains the separate `Ajv2020` validator used here.

The local artifact verifier independently derives tuples, distinct User sets and
bigint-cent sums from the research CSV, tests typed output and normalized
refusals, and records source/artifact hashes. Native controls cover atomic
publication, concurrent readers, post-capture source mutation, cancellation and
prepared snapshots. Existing P0 checks separately protect both legacy profiles,
including their current MCP example. CI repeats observation artifact checks on
its existing Linux/macOS matrix; this local run does not establish those remote
results. New-profile MCP wiring and four isolated no-model preparations are now locally
verified; see [the MCP/Agent preparation guide](agent-observation-acceptance.md).
A real observation Agent trial remains unrun.
No pagination, automatic calendar completion or arbitrary ontology execution is
added.
