-- Daily observation population demo; run from repository root:
-- duckdb -bail -init examples/observation_server.sql
-- The operator owns this setup. Agents cannot import, reload or replace definitions.
INSTALL ossie FROM community;
LOAD ossie;
INSTALL duckdb_mcp FROM community;
LOAD duckdb_mcp;

-- BEGIN OPERATOR SETUP
CREATE TABLE observations(observation_id VARCHAR, user_id VARCHAR, ds DATE,
                          product VARCHAR, amount_usd DECIMAL(18,2), classification_value INTEGER);
COPY observations FROM 'docs/research/ossie-req-001/multi-period-research/observations.csv' (HEADER);
CALL ossie_bundle_load('test/fixtures/population-observation/bundle-10.json');
-- END OPERATOR SETUP

-- The extension helper rejects invalid/null lists with the versioned population error contract.
-- It never stringifies non-string elements or falls back to an unrestricted query.

PRAGMA mcp_publish_query('SELECT * FROM ossie_concepts_v2()', 'ossie://population/concepts', 'json', 0);
PRAGMA mcp_publish_query('SELECT * FROM ossie_population_metrics()', 'ossie://population/metrics', 'json', 0);
PRAGMA mcp_publish_query('SELECT * FROM ossie_population_fields()', 'ossie://population/fields', 'json', 0);
PRAGMA mcp_publish_query('SELECT * FROM ossie_population_capabilities()', 'ossie://population/capabilities', 'json', 0);

PRAGMA mcp_publish_tool(
    'population_objects',
    'List observations in an exact discovered queryable concept. Read concepts-v2: User is referent-only and cannot be queried. Identity is the ordered typed User/date/product tuple; no synthetic record id. filters is a JSON-encoded array of predicate strings; use "[]" for none. Filters only narrow the formal rule. Result is a single result_json carrier: parse it as ossie-population-result-v1 with bound column types and row arrays. Numeric cells are strings, NULL stays null. Results are unsorted and unpaged; even empty results carry snapshot hashes.',
    'SELECT * FROM ossie_objects_json($concept, ossie_population_strings($filters), include_trace => true)',
    '{"concept":{"type":"string","description":"Exact concept name from concepts resource"},"filters":{"type":"string","description":"JSON-encoded array of predicate strings"}}',
    '["concept","filters"]',
    'json'
);
PRAGMA mcp_publish_tool(
    'population_query',
    'Aggregate a discovered queryable observation concept. Read concepts-v2, metrics, fields and capabilities first. User is referent-only. Only discovered date/product fields may group. Distinct User count means at least one qualifying observation within the requested scope; it cannot be added across groups. Observation AVG divides by nonnull observation amounts, not Users. Only registered metric expressions are supported. Lists are JSON-encoded string arrays. Result is a single result_json carrier: parse ossie-population-result-v1 columns and row arrays. Integer/Decimal cells are exact strings, IEEE float cells are approximate strings with explicit non-finite values; NULL stays null. Check snapshot hashes even for empty results. Groups are sparse, unsorted and unpaged; combined requests bind separately.',
    'SELECT * FROM ossie_concept_query_json($concept, ossie_population_strings($metrics), ossie_population_strings($dimensions), ossie_population_strings($filters), include_trace => true)',
    '{"concept":{"type":"string"},"metrics":{"type":"string","description":"JSON-encoded array of exact metric names"},"dimensions":{"type":"string","description":"JSON-encoded array of exact dimension names"},"filters":{"type":"string","description":"JSON-encoded array of predicate strings"}}',
    '["concept","metrics","dimensions","filters"]',
    'json'
);
PRAGMA mcp_publish_tool(
    'population_compile',
    'Inspect diagnostic SQL, plan-v2 and the bound definition/snapshot hashes without executing. Observation tuple identity differs from referenced User identity. User is referent-only. Same list encoding as population_query; empty metrics selects objects and requires empty dimensions. Diagnostic SQL needs an internal AST dependency and cannot be replayed as ordinary SQL. A separate call after operator reload may bind a different snapshot; compare the execution envelope hashes, including empty results.',
    'SELECT * FROM ossie_concept_compile($concept, ossie_population_strings($metrics), ossie_population_strings($dimensions), ossie_population_strings($filters), include_trace => true)',
    '{"concept":{"type":"string"},"metrics":{"type":"string"},"dimensions":{"type":"string"},"filters":{"type":"string"}}',
    '["concept","metrics","dimensions","filters"]',
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
