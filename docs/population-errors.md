# Population error contract

Population API 1.1.0 adds a project error schema, `ossie-population-error-v1`,
for recoverable errors inside public population bind/load/execute/scan callbacks
and `ossie_population_strings(VARCHAR)`. Successful object, query, compile,
concept and vocabulary columns are unchanged. This is not an Ossie or MCP standard.

The SQL exception message contains `ossie_population_error_v1: ` followed by one
JSON object. DuckDB can add its exception-type prefix and SQL location afterward;
MCP can carry the message in JSON-RPC `error.message` or `result.isError` content.
Inspect both MCP error carriers, decode their string contents first, and parse
the framed object rather than the whole reply or the entire SQL diagnostic.
`scripts/population_error.ts` demonstrates quote-aware framing. Error strings
are diagnostics from a known failure carrier, not proof of trusted origin.

| Field | Meaning |
|---|---|
| `schema_version` | `ossie-population-error-v1` |
| `code` | Stable coarse category from the stage; see below |
| `object` | Relevant definition, reference, request target, list element or operation |
| `stage` | Actual failure stage where known; fallback boundary otherwise |
| `reason` | Diagnostic reason, including native engine detail when applicable |
| `missing_capability` | Exact unmet profile constraint for capability refusals; otherwise null |
| `exception_type` | DuckDB exception classification exposed by this callback |
| `source_exception_type` | Original engine classification when translated; otherwise null |
| `legacy_message` | Previous human diagnostic shape, retained for transition |

Stages/codes: `capability` → `UNSUPPORTED_CAPABILITY`, `identity` →
`IDENTITY_VIOLATION`, `constraint` → `CONSTRAINT_VIOLATION`, `execute` → `EXECUTION_FAILED`, `load` → `LOAD_FAILED`,
`snapshot` → `SNAPSHOT_REJECTED`, `mapping` → `MAPPING_INVALID`, `rule` →
`RULE_REJECTED`, `parse` → `DEFINITION_INVALID`, `hash` →
`DEFINITION_HASH_MISMATCH`; remaining request/binding stages → `INVALID_REQUEST`.
API2.2.0 additionally maps `resource` → `RESOURCE_LIMIT_EXCEEDED` for declared
[daily User set operation](population-daily-users.md) budgets. The opt-in
[relation profile](research/ossie-req-001/p1-relation-load-controls/contract.md)
also uses this code for complete program discovery JSON, and adds the constraint
stage above. Existing stage mappings remain unchanged. Consumers must preserve unknown string codes as
failures; the reference decoder has no closed code enum. External closed-enum
clients need compatibility review. Result-budget failure publishes no partial
success, and cancellation/OOM remains native rather than this new category.

Codes do not infer root cause by searching text. Reasons and legacy messages are
not stable machine enums. The old `missing_capability=bounded population profile`
text is only a legacy placeholder; consumers use the nullable JSON field.

An existing structured error keeps its object/stage and does not acquire another
envelope at an outer load boundary. Server-side forwarding requires the dedicated
DuckDB ExtraInfo payload and a valid schema, rather than recognizing user-supplied
text. Unicode, quotes, backslashes, controls and apparent envelope text remain
escaped data. Engine errors retain ExtraInfo such as source locations.

Own validation failures remain INVALID_INPUT. Existing Run/Prepare/parser/load
paths that previously translated errors to INVALID_INPUT retain that behavior,
and expose the source classification separately. Newly wrapped core errors retain
their original classification. Exceptions affect explicit transactions, so the
contract does not silently classify everything as an invalid argument. Unknown
standard exceptions and cancellation/internal/fatal/OOM errors propagate natively;
failure decoding preserves the original exception. Pending-query cancellation
still interrupts and drains nested tasks before their context is destroyed.

`capabilities_json.uniform_errors=true` is scoped by the added `error_contract`
object, which names the schema and lists excluded boundaries:

- SQL parsing, function arity, argument conversion and output binding outside
  callbacks, including duplicate output column names.
- MCP protocol/unknown tools and missing parameters rejected before the callback.
- Cancellation, internal/fatal/OOM and unknown native system failures.
- The separate core APIs, which retain their existing errors.

These errors must not be relabeled as population validation merely because they
occur during a population call. Compile still does not certify final output binding.

The MCP example now calls `ossie_population_strings` instead of an example-only
JSON macro. SQL NULL, JSON null, malformed/trailing JSON, wrong top-level types
and non-string members fail with stage `arguments`; a member error identifies
its zero-based `list[n]`. Valid `[]` remains empty explicitly. String bytes,
including decoded NUL, are preserved with length-aware reads; expression/name
validation happens downstream. Native MCP arrays remain accepted when the adapter
serializes them as valid JSON string arrays. This does not make the MCP adapter a
full JSON Schema validator. No failure becomes an unrestricted query.

The stock error verifier checks loaded/unloaded calls, core resolution, parser,
AST, request limits, runtime conversion, definition/hash/identity failures, failed
publication, escaped inputs, external bindings and explicit transaction recovery.
The production native capture runner checks structured native missing-file loads,
unstructured unknown system failures and native cancellation with task draining.
Actual MCP checks parse callback errors while retaining external missing-parameter
and unknown-tool refusals. None of these checks certifies lossless numeric transport,
unbounded message sizes, autonomous Agent reasoning or remote CI.
