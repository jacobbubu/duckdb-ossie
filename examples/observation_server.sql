-- Daily observation population demo; run from repository root:
-- duckdb -bail -init examples/observation_server.sql
-- The operator owns this setup. Agents cannot import, reload or replace definitions.
-- The registry serves the 0.3.0 build, which has no ossie_bundle_load, so this script fails with it.
-- To run it against this repository's build, see "Running this repository's build" in README.md.
INSTALL ossie FROM community;
LOAD ossie;
INSTALL duckdb_mcp FROM community;
LOAD duckdb_mcp;

-- BEGIN OPERATOR SETUP
CREATE TABLE observations(observation_id VARCHAR, user_id VARCHAR, ds DATE,
                          product VARCHAR, amount_usd DECIMAL(18,2), classification_value INTEGER);
COPY observations FROM 'test/fixtures/population-data/observations.csv' (HEADER);
CALL ossie_bundle_load('test/fixtures/population-observation/bundle-10.json');
-- END OPERATOR SETUP

-- Every request is one JSON object with a "kind" key. The extension rejects invalid requests with the
-- versioned population error contract and never falls back to an unrestricted query.

PRAGMA mcp_publish_query('SELECT * FROM ossie_program()', 'ossie://population/program', 'json', 0);

PRAGMA mcp_publish_tool(
    'population_query',
    'Execute one bounded observation request. request_json is a String encoding exactly one JSON object. Read the program resource first: its concepts list says which concepts are queryable; User is referent-only and cannot be queried. Identity is the ordered typed User/date/product tuple; no synthetic record id. kind "objects" lists observations: {"kind":"objects","concept":C,"filters":[predicate,...],"include_trace":true}; filters only narrow the formal rule, use [] for none. kind "metrics" aggregates: {"kind":"metrics","concept":C,"metrics":[name,...],"dimensions":[name,...],"filters":[...],"include_trace":true}; only discovered date/product fields may group. Distinct User count means at least one qualifying observation within the requested scope; it cannot be added across groups. Observation AVG divides by nonnull observation amounts, not Users. Only registered metric expressions are supported. Result is a single result_json carrier: parse ossie-population-result-v1 columns and row arrays. Integer/Decimal cells are exact strings, IEEE float cells are approximate strings with explicit non-finite values; NULL stays null. Check snapshot hashes even for empty results. Groups are sparse, unsorted and unpaged; combined requests bind separately.',
    'SELECT * FROM ossie_population_json($request_json)',
    '{"request_json":{"type":"string","description":"JSON-encoded request object with kind, concept and exact name lists"}}',
    '["request_json"]',
    'json'
);
PRAGMA mcp_publish_tool(
    'population_compile',
    'Inspect diagnostic SQL, plan-v2 and the bound definition/snapshot hashes without executing. Observation tuple identity differs from referenced User identity. User is referent-only. Same request_json grammar as population_query. Diagnostic SQL needs an internal AST dependency and cannot be replayed as ordinary SQL. A separate call after operator reload may bind a different snapshot; compare the execution envelope hashes, including empty results.',
    'SELECT * FROM ossie_population_compile($request_json)',
    '{"request_json":{"type":"string"}}',
    '["request_json"]',
    'json'
);
PRAGMA mcp_server_start('stdio', 'localhost', 0, '{
    "enable_execute_tool": false,
    "enable_query_tool": false,
    "enable_export_tool": false,
    "enable_describe_tool": false,
    "enable_list_tables_tool": false,
    "enable_database_info_tool": false
}');
