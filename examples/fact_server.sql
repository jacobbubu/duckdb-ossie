-- Independent bounded dual-fact demo. Operator setup precedes the exposed registry.
-- duckdb -bail -init examples/fact_server.sql
INSTALL ossie FROM community;
LOAD ossie;
INSTALL duckdb_mcp FROM community;
LOAD duckdb_mcp;

-- BEGIN OPERATOR SETUP
.read test/fixtures/population-fact-program/rows.sql
CALL ossie_bundle_load('test/fixtures/population-fact-program/bundle.json');
CREATE TABLE ossie_fact_request_contract AS
SELECT content AS contract_json FROM read_text('examples/fact_users_request_contract.json');
-- END OPERATOR SETUP

PRAGMA mcp_publish_query('SELECT * FROM ossie_fact_program()', 'ossie://facts/program', 'json', 0);
PRAGMA mcp_publish_query(
 'SELECT ''ossie-fact-mcp-surface-v1'' AS schema_version, ''["population_fact_users","population_fact_users_compile","population_fact_windows","population_fact_windows_compile"]'' AS tools_json, false AS generic_objects_tool, false AS reload_tool, false AS arbitrary_sql_tool, ''Unknown outer arguments are ignored; clients must send request_json Strings'' AS argument_boundary',
 'ossie://facts/surface', 'json', 0);
PRAGMA mcp_publish_query(
 'SELECT contract_json FROM ossie_fact_request_contract',
 'ossie://facts/request-contract', 'json', 0);
PRAGMA mcp_publish_tool(
 'population_fact_users',
 'Return complete common User rows for discovered regular metrics and a bounded TRUE cohort set. Discover program, units, identity and six snapshot bindings first. request_json must explicitly contain domain=common, input_scope=entire_capture, metrics and cohort. Reduce each frozen fact separately before combining; common_user_count is the row-presence domain before cohort, selected_user_count after cohort, both integer text. Parse result_json as ossie-fact-users-result-v1. Money is exact decimal text, COUNT is integer text, NULL remains null. User ordering uses raw UTF8 bytes. At most10000 rows and1MiB complete inner JSON; no paging or truncation, no peak memory or outer message bound. Unknown outer arguments are ignored; upstream may coerce non-string values, so clients must send Strings. Inner unknown/duplicate fields are refused. No input/date/product filtering, raw fan-out, arbitrary SQL, reload or history/state inference. request_json is a String encoding one JSON object: {"domain":"common","input_scope":"entire_capture","metrics":[regular_metric_name],"cohort":C}. C is exactly {"operation":"all"}, {"operation":"predicate","predicate":P}, or {"operation":OP,"children":[C,C]}; OP is "intersection", "union" or "difference". Exact fields only; no unknown/duplicate inner keys, NUL, implicit defaults or object-valued outer request_json. Children are exactly two ordered nested cohorts; difference is left set minus right TRUE members, so UNKNOWN on the right is not excluded. Each predicate is one unaliased comparison of an unqualified, unquoted declared regular metric on the left and exact nonNULL numeric constant on the right; comparison kinds =, <>, >, >=, <, <=. Count needs an integral BIGINT constant; currency needs exact DECIMAL(38,2), decimal scale at most2; no DOUBLE, field/window references, functions/casts, metric-to-metric, arithmetic or AND/OR predicates. A predicate metric need not be selected for output. metrics selects1..32 regular names, case-insensitive and no case duplicates, each at most128 UTF8 bytes. Root-inclusive depth<=16, total nodes<=64, original request_json UTF8 bytes<=16384; bounded nesting supported, no self-reference. Read ossie://facts/request-contract for structural schema, full semantics and hypothetical examples, and ossie://facts/program for actual declarations/units/snapshot bindings. A structural schema alone does not bind metrics/predicates or enforce byte/tree budgets.',
 'SELECT * FROM ossie_fact_users_json($request_json)',
 '{"request_json":{"type":"string","description":"Strict explicit bounded common User request JSON"}}', '["request_json"]', 'json');
PRAGMA mcp_publish_tool(
 'population_fact_users_compile',
 'Bind and explain the same common User request without executing or calculating members/counts. Discover program first. Parse plan_json as ossie-fact-users-plan-v1; executed, members_computed, counts_computed are false. Separate aggregation and independent denominator are logical steps, not a physical plan; diagnostic SQL is not replayable. Compare request, canonical selections, units, identity and six bindings against discovery and any result. Execution may exceed the complete1MiB result limit even when compile succeeds. Unknown outer arguments are ignored; upstream may coerce non-string values, so clients must send Strings. Inner request strict; no input filters, arbitrary SQL or reload. request_json is a String encoding one JSON object: {"domain":"common","input_scope":"entire_capture","metrics":[regular_metric_name],"cohort":C}. C is exactly {"operation":"all"}, {"operation":"predicate","predicate":P}, or {"operation":OP,"children":[C,C]}; OP is "intersection", "union" or "difference". Exact fields only; no unknown/duplicate inner keys, NUL, implicit defaults or object-valued outer request_json. Children are exactly two ordered nested cohorts; difference is left set minus right TRUE members, so UNKNOWN on the right is not excluded. Each predicate is one unaliased comparison of an unqualified, unquoted declared regular metric on the left and exact nonNULL numeric constant on the right; comparison kinds =, <>, >, >=, <, <=. Count needs an integral BIGINT constant; currency needs exact DECIMAL(38,2), decimal scale at most2; no DOUBLE, field/window references, functions/casts, metric-to-metric, arithmetic or AND/OR predicates. A predicate metric need not be selected for output. metrics selects1..32 regular names, case-insensitive and no case duplicates, each at most128 UTF8 bytes. Root-inclusive depth<=16, total nodes<=64, original request_json UTF8 bytes<=16384; bounded nesting supported, no self-reference. Read ossie://facts/request-contract for structural schema, full semantics and hypothetical examples, and ossie://facts/program for actual declarations/units/snapshot bindings. A structural schema alone does not bind metrics/predicates or enforce byte/tree budgets.',
 'SELECT * FROM ossie_fact_users_compile($request_json)',
 '{"request_json":{"type":"string"}}', '["request_json"]', 'json');
PRAGMA mcp_publish_tool(
 'population_fact_windows',
 'Return complete discovered windows from one frozen User/date fact. Discover program first. request_json explicitly contains input_scope=entire_capture, windows and display (all or canonical finite DATE range). Compute over whole capture before display filtering. ROWS counts observations rather than calendar days, LAG includes NULL; no filling or carry-forward. Parse result_json as ossie-fact-windows-result-v1. Exact decimal text/NULL; User raw UTF8 bytes then DATE chronological ordering. Full finite DuckDB DATE text includes BC and years beyond9999. At most10000 rows and1MiB complete inner JSON; no paging/truncation or peak memory/outer message guarantee. Unknown outer arguments are ignored; upstream may coerce non-string values, so clients must send Strings. Inner request strict; no common User restriction, input filters, history/state inference, reload or arbitrary SQL.',
 'SELECT * FROM ossie_fact_windows_json($request_json)',
 '{"request_json":{"type":"string","description":"Strict whole-capture window request with display scope"}}', '["request_json"]', 'json');
PRAGMA mcp_publish_tool(
 'population_fact_windows_compile',
 'Bind and explain the same window/display request without execution or members/counts. Parse plan_json as ossie-fact-windows-plan-v1; executed, members_computed, counts_computed false. Whole-capture windows precede display DATE filtering. Logical operators and diagnostic SQL are not a physical plan or replayable SQL; compare discovery, request, units, source grain and six bindings. Compilation success does not guarantee result budget. Unknown outer arguments are ignored; upstream may coerce non-string values, so clients must send Strings. No input filter, common-domain restriction, reload or arbitrary SQL.',
 'SELECT * FROM ossie_fact_windows_compile($request_json)',
 '{"request_json":{"type":"string"}}', '["request_json"]', 'json');
PRAGMA mcp_server_start('stdio', 'localhost', 0, '{
    "enable_execute_tool": false,
    "enable_query_tool": false,
    "enable_export_tool": false,
    "enable_describe_tool": false,
    "enable_list_tables_tool": false,
    "enable_database_info_tool": false
}');
