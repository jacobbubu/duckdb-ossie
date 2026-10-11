-- Declared relationship complete-link demo; operator setup, separate from observation_server.sql.
-- duckdb -bail -init examples/relation_server.sql
INSTALL ossie FROM community;
LOAD ossie;
INSTALL duckdb_mcp FROM community;
LOAD duckdb_mcp;

-- BEGIN OPERATOR SETUP
CREATE TABLE observations(observation_id VARCHAR, user_id VARCHAR, ds DATE,
                          product VARCHAR, amount_usd DECIMAL(18,2), classification_value INTEGER);
COPY observations FROM 'docs/research/ossie-req-001/multi-period-research/every-day/observations.csv' (HEADER);
CALL ossie_bundle_load('test/fixtures/population-relation-program/bundle.json');
-- END OPERATOR SETUP

PRAGMA mcp_publish_query('SELECT * FROM ossie_relation_program()', 'ossie://relations/program', 'json', 0);
-- Native discovery describes SQL; this separate resource and tools/list describe live exposure.
PRAGMA mcp_publish_query(
 'SELECT ''ossie-relation-mcp-surface-v1'' AS schema_version, ''["population_relation_compile","population_relation_objects"]'' AS tools_json, false AS generic_objects_tool, false AS reload_tool, false AS arbitrary_sql_tool, ''Unknown outer arguments are ignored; clients must send relationship Strings'' AS argument_boundary',
 'ossie://relations/surface', 'json', 0);
PRAGMA mcp_publish_tool(
 'population_relation_objects',
 'Return complete source User/date/product plus target User links for one discovered declared relationship. Discover program rules, identity, dependencies and snapshot first. Entire captured root population, selected relationship TRUE predicate only; no extra old qualified concept or implicit date/product filter. Multi-rule union; preserve full tuple, never substitute distinct User or count-only. Parse result_json as ossie-relation-result-v1. Link count is integer text. User/product/target ordered by raw UTF-8 bytes, DATE chronologically; DATE text is DuckDB finite DATE including (BC) and years beyond9999. At most10000 links and1MiB complete internal JSON including metadata; no paging or truncation, no peak-memory/outer-message limit guarantee. Unknown outer arguments are ignored; upstream may coerce non-string values, so clients must send Strings. This tool has only relationship, with no date/product scope, generic objects, metrics, states, historical validity, windows, import/reload or arbitrary SQL. Compare canonical relationship, rules, scope, identity and six snapshot hashes.',
 'SELECT * FROM ossie_relation_objects_json($relationship)',
 '{"relationship":{"type":"string","description":"Declared derived relationship from program discovery"}}',
 '["relationship"]', 'json');
PRAGMA mcp_publish_tool(
 'population_relation_compile',
 'Bind and explain the same complete relationship link projection without executing or calculating members. Parse plan_json as ossie-relation-plan-v1, executed=false and members_computed=false. Four-column projection types are bound, not arbitrary outer SQL or execution success. Logical ordering/encoding steps are descriptions, not a physical plan; private frozen AST SQL is not replayable. Entire captured scope and declared TRUE relationship predicate; max1MiB complete JSON. Execution may exceed result budget even when compile succeeds. Separate calls after operator reload can bind different snapshots; compare identity, rule and hashes. Unknown outer arguments are ignored; upstream may coerce non-string values, so clients must send Strings. No date/product scope, import/reload or arbitrary SQL.',
 'SELECT * FROM ossie_relation_compile($relationship)',
 '{"relationship":{"type":"string"}}', '["relationship"]', 'json');
PRAGMA mcp_server_start('stdio', 'localhost', 0, '{
    "enable_execute_tool": false,
    "enable_query_tool": false,
    "enable_export_tool": false,
    "enable_describe_tool": false,
    "enable_list_tables_tool": false,
    "enable_database_info_tool": false
}');
