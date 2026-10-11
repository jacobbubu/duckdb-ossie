-- Independent state surface. Operator owns capture; Agent has two read-only tools.
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
PRAGMA mcp_publish_query('SELECT * FROM ossie_population_capabilities()', 'ossie://population/capabilities', 'json', 0);
PRAGMA mcp_publish_query('SELECT * FROM ossie_daily_states_program()', 'ossie://population/state-program', 'json', 0);
PRAGMA mcp_publish_query(
 'SELECT ''ossie-daily-state-mcp-surface-v1'' AS schema_version, ''["population_daily_states","population_daily_states_compile"]'' AS tools_json, false AS reload_tool, false AS arbitrary_sql_tool, ''scope only inside request_json; unknown outer arguments ignored; upstream may coerce objects; clients send Strings'' AS argument_boundary',
 'ossie://population/state-surface','json',0
);
PRAGMA mcp_publish_tool(
 'population_daily_states',
 'Return complete User/day TRUE/FALSE/UNKNOWN states and all adjacent natural-day comparisons including unchanged. Discover state-program and exact qualification concept first. User domain is all distinct Users in the entire frozen observation capture; date/product scope affects observations only. Any TRUE wins; nonempty all FALSE is FALSE; missing, all NULL and FALSE+NULL are UNKNOWN. No carry or historical rule inference. request_json exactly start_date,end_date,products,user_domain=captured_users,transitions=within_request_days. Dates inclusive finite YYYY-MM-DD max366 days; products explicit all or nonempty subset max64 unique raw strings/max256 UTF-8 bytes each. Request max16KiB; states plus comparisons max10000; complete internal JSON max1MiB. No paging or truncation. Parse one result_json carrier as ossie-daily-states-result-v1, state_api_version1.0.0; counts are integer text, totals unit User/day. Compare request, current frozen rule and six hashes. Unknown outer arguments are ignored; upstream may coerce values; clients send Strings. All scope belongs inside request_json. No registry domain, request-external predecessor, all-products qualification, historical validity, reload or arbitrary SQL.',
 'SELECT * FROM ossie_daily_states_json($concept,$request_json)',
 '{"concept":{"type":"string"},"request_json":{"type":"string"}}',
 '["concept","request_json"]','json'
);
PRAGMA mcp_publish_tool(
 'population_daily_states_compile',
 'Explain the same complete state request without scanning or computing states/comparisons. Parse one plan_json carrier as ossie-daily-states-plan-v1, state_api_version1.0.0. Load User count is capture metadata; actual full BOOLEAN projection preserves FALSE/NULL, C++ reduction includes full domain/calendar/scope/adjacent pairs. No physical plan, replayable SQL or execution-result-size guarantee. Same exact request_json contract and budgets as population_daily_states. Unknown outer arguments are ignored; upstream may coerce values; clients send Strings. Compare request, rule and six hashes across calls; reload can change separate call bindings. This server exposes only two state tools, with no import, reload or arbitrary SQL.',
 'SELECT * FROM ossie_daily_states_compile($concept,$request_json)',
 '{"concept":{"type":"string"},"request_json":{"type":"string"}}',
 '["concept","request_json"]','json'
);
PRAGMA mcp_server_start('stdio','localhost',0,'{"enable_execute_tool":false,"enable_query_tool":false,"enable_export_tool":false,"enable_describe_tool":false,"enable_list_tables_tool":false,"enable_database_info_tool":false}');
