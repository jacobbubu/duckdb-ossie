-- Every-calendar-day User set demo; operator setup, separate from observation_server.sql.
-- duckdb -bail -init examples/daily_user_server.sql
INSTALL ossie FROM community;
LOAD ossie;
INSTALL duckdb_mcp FROM community;
LOAD duckdb_mcp;

-- BEGIN OPERATOR SETUP
CREATE TABLE observations(observation_id VARCHAR, user_id VARCHAR, ds DATE,
                          product VARCHAR, amount_usd DECIMAL(18,2), classification_value INTEGER);
COPY observations FROM 'docs/research/ossie-req-001/multi-period-research/every-day/observations.csv' (HEADER);
CALL ossie_bundle_load('test/fixtures/population-observation/bundle-10.json');
-- END OPERATOR SETUP

PRAGMA mcp_publish_query('SELECT * FROM ossie_concepts_v2()', 'ossie://population/concepts', 'json', 0);
PRAGMA mcp_publish_query('SELECT * FROM ossie_population_metrics()', 'ossie://population/metrics', 'json', 0);
PRAGMA mcp_publish_query('SELECT * FROM ossie_population_fields()', 'ossie://population/fields', 'json', 0);
PRAGMA mcp_publish_query('SELECT * FROM ossie_population_capabilities()', 'ossie://population/capabilities', 'json', 0);
-- Native capabilities describe SQL. This resource describes this server's actual tool exposure.
PRAGMA mcp_publish_query(
    'SELECT ''ossie-daily-mcp-surface-v1'' AS schema_version, ''["population_daily_users","population_daily_compile"]'' AS tools_json, false AS generic_objects_tool, false AS generic_metrics_tool, false AS reload_tool, ''request_json contains the entire scope; unknown outer arguments ignored; object request may be coerced; clients must send Strings'' AS argument_boundary',
    'ossie://population/daily-surface', 'json', 0
);

PRAGMA mcp_publish_tool(
    'population_daily_users',
    'Return the complete set of raw User IDs with at least one TRUE qualified observation on every requested natural calendar day, inclusive endpoints. Discover the exact qualified concept and current formal rule first. Product may change by day; FALSE/NULL does not cancel TRUE, missing has no witness. request_json is a JSON-encoded object with exactly start_date, end_date, products. Dates YYYY-MM-DD, max366 days; products is {"mode":"all"} or {"mode":"subset","values":["X"]}, max64 raw nonempty unique strings/max256 UTF-8 bytes each. Request max16KiB, internal full result max1MiB; failure never truncates or substitutes count-only. Parse single result_json carrier as ossie-daily-user-set-result-v1. Count is integer text; raw IDs ordered by UTF-8 bytes; no paging. Compare scope, formal rule and snapshot hashes. Unknown outer arguments are ignored; upstream may coerce non-string values, so clients must send Strings. Scope belongs only inside request_json. This tool does not expose generic objects/metrics, states, historical rules, as-of, windows or arbitrary SQL.',
    'SELECT * FROM ossie_daily_users_json($concept, $request_json)',
    '{"concept":{"type":"string","description":"Exact current qualified observation concept"},"request_json":{"type":"string","description":"JSON-encoded complete date/product request; all scope belongs here"}}',
    '["concept","request_json"]', 'json'
);
PRAGMA mcp_publish_tool(
    'population_daily_compile',
    'Explain the same daily User request without computing members. Parse single plan_json carrier as ossie-daily-user-set-plan-v1: actual bound observation projection plus C++ date/product/distinct-user-day/calendar-count/sort/complete-JSON logical steps. Internal configuration and input Prepare occur, not projection execution/fetch; no physical plan, final output binding, replayable SQL or execution-success guarantee. Compile may succeed while execution exceeds its result budget. Separate calls after operator reload can bind different snapshots; compare hashes. Same exact request_json contract as population_daily_users, max16KiB; complete plan max1MiB. Unknown outer arguments are ignored; upstream may coerce non-string values, so clients must send Strings. All scope belongs in request_json. This server exposes only the two daily tools, with no import/reload/arbitrary SQL.',
    'SELECT * FROM ossie_daily_users_compile($concept, $request_json)',
    '{"concept":{"type":"string"},"request_json":{"type":"string"}}',
    '["concept","request_json"]', 'json'
);
PRAGMA mcp_server_start('stdio', 'localhost', 0, '{
    "enable_execute_tool": false,
    "enable_query_tool": false,
    "enable_export_tool": false,
    "enable_describe_tool": false,
    "enable_list_tables_tool": false,
    "enable_database_info_tool": false
}');
