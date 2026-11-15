-- Bounded population demo; run from repository root:
-- duckdb -bail -init examples/population_server.sql
-- The operator owns this setup. Agents cannot import, reload or replace definitions.
INSTALL ossie FROM community;
LOAD ossie;
INSTALL duckdb_mcp FROM community;
LOAD duckdb_mcp;

-- BEGIN OPERATOR SETUP
CREATE TABLE dws_gaap_di(ds DATE, product VARCHAR, region VARCHAR, user_type VARCHAR,
                        user_id VARCHAR, gaap_val DECIMAL(18,2), ytd_gaap DECIMAL(18,2));
COPY dws_gaap_di FROM 'test/fixtures/population-data/gaap-original-475.csv' (HEADER);
CALL ossie_bundle_load('test/fixtures/population/bundle-10.json');
-- END OPERATOR SETUP

-- Every request is one JSON object with a "kind" key. The extension rejects invalid requests with the
-- versioned population error contract and never falls back to an unrestricted query.

PRAGMA mcp_publish_query('SELECT * FROM ossie_concepts()', 'ossie://population/concepts', 'json', 0);
PRAGMA mcp_publish_query('SELECT * FROM ossie_program()', 'ossie://population/program', 'json', 0);

PRAGMA mcp_publish_tool(
    'population_query',
    'Execute one bounded population request. request_json is a String encoding exactly one JSON object. kind "objects" lists objects of an exact discovered concept: {"kind":"objects","concept":C,"filters":[predicate,...],"include_trace":true}; filters only narrow the formal rule, use [] for none. kind "metrics" aggregates a discovered concept: {"kind":"metrics","concept":C,"metrics":[name,...],"dimensions":[name,...],"filters":[...],"include_trace":true}; read the program resource (concepts, vocabulary, capabilities) first. Result is a single result_json carrier: parse it as ossie-population-result-v1 with bound column types and row arrays. Integer/Decimal cells are exact strings, IEEE float cells are approximate strings with explicit non-finite values; NULL stays null. Check snapshot hashes even for empty results. Groups are sparse, unsorted and unpaged; combined requests bind separately.',
    'SELECT * FROM ossie_population_json($request_json)',
    '{"request_json":{"type":"string","description":"JSON-encoded request object with kind, concept and exact name lists"}}',
    '["request_json"]',
    'json'
);
PRAGMA mcp_publish_tool(
    'population_compile',
    'Inspect diagnostic SQL and the bound definition/snapshot hashes without executing. Same request_json grammar as population_query. Diagnostic SQL needs an internal AST dependency and cannot be replayed as ordinary SQL. A separate call after operator reload may bind a different snapshot; compare the execution envelope hashes, including empty results.',
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
