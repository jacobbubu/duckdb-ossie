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
COPY dws_gaap_di FROM 'docs/research/ossie-req-001/gaap-original-475.csv' (HEADER);
CALL ossie_bundle_load('test/fixtures/population/bundle-10.json');
-- END OPERATOR SETUP

-- Require explicit lists. Never turn invalid/null input into an unrestricted query.
-- JSON-to-VARCHAR conversion alone would silently stringify numeric/object elements.
CREATE MACRO ossie_mcp_strings(value) AS
    CASE WHEN value IS NULL THEN error('population MCP: list argument required')
         WHEN json_type(value::JSON) <> 'ARRAY'
              THEN error('population MCP: JSON array of strings required')
         ELSE list_transform(json_extract(value::JSON, '$[*]'), lambda item:
              CASE WHEN json_type(item) = 'VARCHAR' THEN json_extract_string(item, '$')
                   ELSE error('population MCP: JSON array of strings required') END)
    END;

PRAGMA mcp_publish_query('SELECT * FROM ossie_concepts()', 'ossie://population/concepts', 'json', 0);

PRAGMA mcp_publish_tool(
    'population_objects',
    'List objects in an exact discovered concept. filters is a JSON-encoded array of predicate strings; use "[]" for none. Filters only narrow the formal rule. Results are unsorted, unpaged and carry snapshot trace when nonempty.',
    'SELECT * FROM ossie_objects($concept, ossie_mcp_strings($filters), include_trace => true)',
    '{"concept":{"type":"string","description":"Exact concept name from concepts resource"},"filters":{"type":"string","description":"JSON-encoded array of predicate strings"}}',
    '["concept","filters"]',
    'json'
);
PRAGMA mcp_publish_tool(
    'population_query',
    'Aggregate a discovered concept using operator-approved metric and dimension names. metrics, dimensions and filters are JSON-encoded string arrays. Use "[]" for no dimensions or filters. Nonempty results carry snapshot trace; sparse groups have no implicit calendar or order. Demo metrics: record_count, gaap_value_count, gaap_sum, mean_gaap; dimensions: dws_gaap_di.ds, dws_gaap_di.region, dws_gaap_di.user_type.',
    'SELECT * FROM ossie_concept_query($concept, ossie_mcp_strings($metrics), ossie_mcp_strings($dimensions), ossie_mcp_strings($filters), include_trace => true)',
    '{"concept":{"type":"string"},"metrics":{"type":"string","description":"JSON-encoded array of exact metric names"},"dimensions":{"type":"string","description":"JSON-encoded array of exact dimension names"},"filters":{"type":"string","description":"JSON-encoded array of predicate strings"}}',
    '["concept","metrics","dimensions","filters"]',
    'json'
);
PRAGMA mcp_publish_tool(
    'population_compile',
    'Inspect diagnostic SQL and the bound definition/snapshot hashes without executing. Same list encoding as population_query; empty metrics selects objects and requires empty dimensions. Diagnostic SQL needs an internal AST dependency and cannot be replayed as ordinary SQL. A separate call after operator reload may bind a different snapshot; check execution trace. Empty query results carry no row trace.',
    'SELECT * FROM ossie_concept_compile($concept, ossie_mcp_strings($metrics), ossie_mcp_strings($dimensions), ossie_mcp_strings($filters))',
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
