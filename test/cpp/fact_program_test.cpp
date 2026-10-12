#include "ossie/fact_program.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/query_result.hpp"
#include "duckdb/main/prepared_statement.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
#include "duckdb/common/exception.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace duckdb;
using namespace duckdb::ossie;
int checks = 0;
void Check(bool ok, const string &message) {
	if (!ok)
		throw std::runtime_error(message);
	checks++;
	std::cout << "PASS: " << message << '\n';
}
template <class F>
void Refuse(F action, const string &message) {
	bool refused = false;
	try {
		action();
	} catch (const InvalidInputException &) {
		refused = true;
	}
	Check(refused, message);
}
FactProgramInput Input() {
	FactProgramInput input;
	input.user_concept = "User";
	input.user_identity_relationship = "id";
	input.sources = {{"revenue",
	                  "RevenueObservation",
	                  "User",
	                  "id",
	                  "user_id",
	                  "ds",
	                  "",
	                  "amount",
	                  "USD",
	                  {"user_id", "ds"},
	                  {{"user_id", "who", LogicalType::VARCHAR},
	                   {"ds", "day", LogicalType::DATE},
	                   {"amount", "value", LogicalType::DECIMAL(18, 2)}}},
	                 {"cost",
	                  "CostObservation",
	                  "User",
	                  "id",
	                  "user_id",
	                  "ds",
	                  "product",
	                  "amount",
	                  "USD",
	                  {"user_id", "ds", "product"},
	                  {{"user_id", "who", LogicalType::VARCHAR},
	                   {"ds", "day", LogicalType::DATE},
	                   {"product", "item", LogicalType::VARCHAR},
	                   {"amount", "value", LogicalType::DECIMAL(18, 2)}}}};
	input.metrics = {{"balance", "income - expense"},
	                 {"income", "SUM(revenue.amount)"},
	                 {"expense", "SUM(cost.amount)"},
	                 {"revenue_rows", "COUNT(revenue.user_id)"},
	                 {"revenue_values", "COUNT(revenue.amount)"},
	                 {"cost_rows", "COUNT(cost.user_id)"},
	                 {"cost_values", "COUNT(cost.amount)"}};
	input.windows = {{"previous", "LAG(revenue.amount,1,NULL) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds)"},
	                 {"rolling", "SUM(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds ROWS "
	                             "BETWEEN 1 PRECEDING AND CURRENT ROW)"}};
	return input;
}
vector<unique_ptr<TableRef>> Sources(bool reversed = false) {
	vector<unique_ptr<TableRef>> out;
	for (auto &name : vector<string> {reversed ? "c" : "r", reversed ? "r" : "c"}) {
		auto table = make_uniq<BaseTableRef>();
		table->table_name = name;
		out.push_back(std::move(table));
	}
	return out;
}
unique_ptr<MaterializedQueryResult> Query(Connection &engine, const string &sql) {
	auto result = engine.Query(sql);
	if (result->HasError())
		throw std::runtime_error(result->GetError() + "\n" + sql);
	return result;
}
FactCohort Predicate(const string &sql) {
	return {"predicate", sql, {}};
}
FactCohort Set(const string &op, FactCohort a, FactCohort b) {
	return {op, "", {std::move(a), std::move(b)}};
}
string Users(Connection &engine, const FactProgram &program, const FactCohort &cohort, bool reversed = false) {
	auto query = CompileFactUsers(program, {"income", "expense", "balance"}, "common", cohort, Sources(reversed));
	auto result = Query(engine, query->ToString());
	string users;
	for (idx_t row = 0; row < result->RowCount(); row++)
		users += result->GetValue(0, row).ToString();
	return users;
}
void Run() {
	auto input = Input();
	auto program = BindFactProgram(input);
	Check(program.metrics.size() == 7 && program.aggregates.size() == 6 && program.windows.size() == 2,
	      "typed source components, forward metric dependencies and bounded windows bound");
	DuckDB database(nullptr);
	Connection engine(database);
	Query(engine, "CREATE TABLE r(who VARCHAR,day DATE,value DECIMAL(18,2)); "
	              "INSERT INTO r VALUES ('A','2026-01-01',100),('A','2026-01-31',200),('A','2026-02-01',50),"
	              "('A','2026-02-28',25),('B','2026-01-01',80),('B','2026-02-01',0),('C','2026-02-01',NULL),"
	              "('D','2026-02-01',40); CREATE TABLE c(who VARCHAR,day DATE,item VARCHAR,value DECIMAL(18,2)); "
	              "INSERT INTO c VALUES ('A','2026-01-01','X',10),('A','2026-01-01','Y',20),"
	              "('A','2026-01-31','X',40),('A','2026-02-01','X',5),('A','2026-02-01','Y',7),"
	              "('A','2026-02-28','X',3),('B','2026-01-01','X',30),('B','2026-02-01','X',0),"
	              "('C','2026-02-01','X',NULL),('E','2026-02-01','X',9)");
	auto users = CompileFactUsers(
	    program, {"income", "expense", "balance", "revenue_rows", "revenue_values", "cost_rows", "cost_values"},
	    "common", {"all", "", {}}, Sources());
	auto actual = Query(engine, users->ToString());
	Check(actual->RowCount() == 3 && actual->GetValue(0, 0).ToString() == "A" &&
	          actual->GetValue(0, 1).ToString() == "B" && actual->GetValue(0, 2).ToString() == "C",
	      "common domain uses row presence, retaining all-NULL C and excluding one-sided D/E");
	Check(actual->GetValue(1, 0).ToString() == "375.00" && actual->GetValue(2, 0).ToString() == "85.00" &&
	          actual->GetValue(3, 0).ToString() == "290.00",
	      "separately reduced amounts match independent cents totals 37500/8500/29000");
	Check(actual->GetValue(1, 1).ToString() == "80.00" && actual->GetValue(2, 1).ToString() == "30.00" &&
	          actual->GetValue(3, 1).ToString() == "50.00",
	      "second member independently totals 8000/3000/5000 cents");
	Check(actual->GetValue(1, 2).IsNull() && actual->GetValue(2, 2).IsNull() && actual->GetValue(3, 2).IsNull() &&
	          actual->GetValue(4, 2).GetValue<int64_t>() == 1 && actual->GetValue(5, 2).GetValue<int64_t>() == 0,
	      "SUM all-NULL stays NULL; COUNT(User) counts row, COUNT(amount) excludes NULL");
	Check(actual->types[1] == LogicalType::DECIMAL(38, 2) && actual->types[3] == LogicalType::DECIMAL(38, 2) &&
	          actual->types[4] == LogicalType::BIGINT,
	      "actual DuckDB reduction and arithmetic bind money/scale and count types");
	auto fanout = Query(engine, "SELECT sum(r.value) FROM r JOIN c ON r.who=c.who AND r.day=c.day WHERE r.who='A'");
	Check(fanout->GetValue(0, 0).ToString() == "525.00",
	      "raw fact join negative control inflates the same revenue to 525");
	auto a = Predicate("income >= 100"), b = Predicate("expense < 80");
	Check(Users(engine, program, a) == "A" && Users(engine, program, b) == "B",
	      "atomic TRUE cohorts retain exact identities, not only counts");
	Check(Users(engine, program, Set("intersection", a, b)).empty() &&
	          Users(engine, program, Set("union", a, b)) == "AB" &&
	          Users(engine, program, Set("difference", a, b)) == "A",
	      "intersection/union/difference operate on explicit TRUE sets");
	Query(engine, "INSERT INTO r VALUES ('N','2026-02-01',150); INSERT INTO c VALUES ('N','2026-02-01','X',NULL)");
	Check(Users(engine, program, Set("difference", a, b)) == "AN",
	      "A minus B retains TRUE A when B predicate is UNKNOWN");
	auto not_nullable =
	    Query(engine, "SELECT who FROM (SELECT who,sum(value) amount FROM c GROUP BY who) WHERE NOT(amount<80)");
	Check(not_nullable->RowCount() == 1 && not_nullable->GetValue(0, 0).ToString() == "A",
	      "plain nullable NOT negative control incorrectly drops N");
	auto start = Value::DATE(2026, 2, 1), end = Value::DATE(2026, 2, 28);
	auto windows = Query(
	    engine,
	    CompileFactWindows(program, {"previous", "rolling"}, "entire_capture", start, end, Sources())->ToString());
	Check(windows->RowCount() == 6 && windows->GetValue(2, 0).ToString() == "200.00" &&
	          windows->GetValue(3, 0).ToString() == "250.00" && windows->GetValue(2, 1).ToString() == "50.00" &&
	          windows->GetValue(3, 1).ToString() == "75.00",
	      "display filter runs after sparse LAG and two-row SUM, preserving January input");
	Check(windows->GetValue(0, 2).ToString() == "B" && windows->GetValue(2, 2).ToString() == "80.00" &&
	          windows->GetValue(3, 2).ToString() == "80.00" && windows->GetValue(3, 3).IsNull(),
	      "zero is observed money; all-NULL row window is NULL");
	auto early = Query(engine, "SELECT lag(value,1,NULL) OVER(PARTITION BY who ORDER BY day) FROM r WHERE day BETWEEN "
	                           "DATE '2026-02-01' AND DATE '2026-02-28' ORDER BY who,day");
	Check(early->GetValue(0, 0).IsNull(), "filter-before-window negative control loses January predecessor");
	Query(engine, "SET default_order='DESC'");
	auto descending_session = Query(
	    engine,
	    CompileFactWindows(program, {"previous", "rolling"}, "entire_capture", start, end, Sources())->ToString());
	Check(descending_session->GetValue(0, 0).ToString() == "A" &&
	          descending_session->GetValue(2, 0).ToString() == "200.00" &&
	          descending_session->GetValue(3, 0).ToString() == "250.00",
	      "window direction and output order independent of descending session default");
	Query(engine, "RESET default_order");
	auto zero_input = Input();
	zero_input.windows[1].sql = "SUM(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds ROWS "
	                            "BETWEEN 0 PRECEDING AND CURRENT ROW)";
	auto zero_program = BindFactProgram(zero_input);
	auto zero = Query(
	    engine, CompileFactWindows(zero_program, {"rolling"}, "entire_capture", start, end, Sources())->ToString());
	Check(zero->GetValue(2, 0).ToString() == "50.00" && zero->GetValue(2, 3).IsNull(),
	      "zero preceding frame includes current row only and preserves all-NULL SUM");
	Query(engine,
	      "CREATE TABLE long_r(who,day,value) AS SELECT 'L'::VARCHAR, DATE '2020-01-01'+i::INTEGER, 1::DECIMAL(18,2) "
	      "FROM range(400) t(i)");
	auto maximum_input = Input();
	maximum_input.windows[1].sql = "SUM(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds ROWS "
	                               "BETWEEN 366 PRECEDING AND CURRENT ROW)";
	auto maximum_program = BindFactProgram(maximum_input);
	auto long_sources = Sources();
	long_sources[0]->Cast<BaseTableRef>().table_name = "long_r";
	auto maximum = Query(engine, CompileFactWindows(maximum_program, {"rolling"}, "entire_capture", Value(), Value(),
	                                                std::move(long_sources))
	                                 ->ToString());
	Check(maximum->RowCount() == 400 && maximum->GetValue(2, 365).ToString() == "366.00" &&
	          maximum->GetValue(2, 366).ToString() == "367.00" && maximum->GetValue(2, 399).ToString() == "367.00",
	      "366 preceding boundary means 367 observed rows, without calendar filling");
	Query(engine, "INSERT INTO r VALUES "
	              "('Z','2026-01-01',NULL),('Z','2026-01-03',2),('Z','2026-01-05',NULL),('Z','2026-01-09',3)");
	auto null_window = Query(
	    engine, CompileFactWindows(program, {"previous", "rolling"}, "entire_capture", Value(), Value(), Sources())
	                ->ToString());
	auto n = null_window->RowCount();
	Check(null_window->GetValue(0, n - 4).ToString() == "Z" && null_window->GetValue(2, n - 4).IsNull() &&
	          null_window->GetValue(2, n - 3).IsNull() && null_window->GetValue(2, n - 2).ToString() == "2.00" &&
	          null_window->GetValue(2, n - 1).IsNull() && null_window->GetValue(3, n - 1).ToString() == "3.00",
	      "LAG does not skip NULL; absent predecessor and NULL predecessor both produce explicit NULL");
	Query(engine, "CREATE TABLE empty_c AS SELECT * FROM c WHERE FALSE");
	auto single_input = Input();
	single_input.metrics = {{"income", "SUM(revenue.amount)"}};
	single_input.windows.clear();
	auto single_program = BindFactProgram(single_input);
	auto single_result =
	    Query(engine, CompileFactUsers(single_program, {"income"}, "common", {"all", "", {}}, Sources())->ToString());
	Check(single_result->RowCount() == 4 && single_result->GetValue(0, 2).ToString() == "C" &&
	          single_result->GetValue(1, 2).IsNull(),
	      "fact with no requested aggregates still contributes its row-presence domain");
	auto empty_sources = Sources();
	empty_sources[1]->Cast<BaseTableRef>().table_name = "empty_c";
	Check(Query(engine,
	            CompileFactUsers(program, {"income"}, "common", {"all", "", {}}, std::move(empty_sources))->ToString())
	              ->RowCount() == 0,
	      "common domain empty when other fact empty even selecting only revenue metric");
	for (auto &reserved : vector<string> {"user_id", "USER_ID", "ds"}) {
		auto reserved_input = Input();
		reserved_input.metrics[0].name = reserved;
		Refuse([&] { BindFactProgram(reserved_input); }, "metric output identity collision refused: " + reserved);
		reserved_input = Input();
		reserved_input.windows[0].name = reserved;
		Refuse([&] { BindFactProgram(reserved_input); }, "window output identity collision refused: " + reserved);
	}
	std::reverse(input.metrics.begin(), input.metrics.end());
	Check(Users(engine, BindFactProgram(input), a) == "AN", "declaration reorder preserves exact memberships");
	std::reverse(input.sources.begin(), input.sources.end());
	Check(Users(engine, BindFactProgram(input), a, true) == "AN",
	      "explicit swapped source ASTs preserve logical fact binding");
	for (auto &sql : vector<string> {"AVG(revenue.amount)",
	                                 "COUNT(*)",
	                                 "COUNT(DISTINCT revenue.user_id)",
	                                 "MIN(revenue.amount)",
	                                 "MAX(revenue.amount)",
	                                 "COALESCE(income,0)",
	                                 "SUM(revenue.amount-cost.amount)",
	                                 "SUM(SUM(revenue.amount))",
	                                 "SUM(cost.user_id)",
	                                 "COUNT(cost.product)",
	                                 "SUM(revenue.missing)",
	                                 "SUM(amount)",
	                                 "SUM(revenue.amount) FILTER(WHERE true)",
	                                 "SUM(DISTINCT revenue.amount)",
	                                 "SUM(revenue.amount ORDER BY revenue.ds)",
	                                 "system.main.sum(revenue.amount)",
	                                 "income/expense",
	                                 "income + revenue_rows",
	                                 "CAST(income AS DOUBLE)",
	                                 "(SELECT 1)",
	                                 "$1",
	                                 "1"}) {
		auto bad = Input();
		bad.metrics[0].sql = sql;
		Refuse([&] { BindFactProgram(bad); }, "reject metric boundary: " + sql);
	}
	for (auto &sql : vector<string> {
	         "LAG(revenue.amount,2,NULL) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds)",
	         "LAG(revenue.amount,1,0) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds)",
	         "LAG(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds)",
	         "LEAD(revenue.amount,1,NULL) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds)",
	         "ROW_NUMBER() OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds)",
	         "FIRST_VALUE(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds)",
	         "AVG(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds ROWS BETWEEN 1 PRECEDING AND "
	         "CURRENT ROW)",
	         "SUM(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds ROWS BETWEEN UNBOUNDED "
	         "PRECEDING AND CURRENT ROW)",
	         "SUM(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds ROWS BETWEEN 1 PRECEDING AND 1 "
	         "FOLLOWING)",
	         "LAG(revenue.amount,1,NULL IGNORE NULLS) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds)",
	         "LAG(revenue.amount,1,NULL) OVER(ORDER BY revenue.ds)",
	         "LAG(revenue.amount,1,NULL) OVER(PARTITION BY revenue.user_id ORDER BY revenue.amount)",
	         "SUM(cost.amount) OVER(PARTITION BY cost.user_id ORDER BY cost.ds ROWS BETWEEN 1 PRECEDING "
	         "AND CURRENT ROW)",
	         "SUM(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds DESC ROWS BETWEEN "
	         "1 PRECEDING AND CURRENT ROW)",
	         "SUM(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds ROWS BETWEEN 367 "
	         "PRECEDING AND CURRENT ROW)",
	         "SUM(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds RANGE BETWEEN 1 "
	         "PRECEDING AND CURRENT ROW)"}) {
		auto bad = Input();
		bad.windows[0].sql = sql;
		Refuse([&] { BindFactProgram(bad); }, "reject window boundary: " + sql);
	}
	auto bad = Input();
	bad.sources[1].currency = "EUR";
	Refuse([&] { BindFactProgram(bad); }, "different currencies cannot be subtracted");
	bad = Input();
	bad.sources[0].fields[2].type = LogicalType::DOUBLE;
	Refuse([&] { BindFactProgram(bad); }, "actual amount type must be exact DECIMAL18,2");
	bad = Input();
	bad.sources[1].target_identity_relationship = "another_id";
	Refuse([&] { BindFactProgram(bad); }, "same field spelling cannot substitute for common declared User identity");
	bad = Input();
	bad.sources[1].identity_fields.pop_back();
	Refuse([&] { BindFactProgram(bad); }, "full product-grain identity required");
	bad = Input();
	bad.metrics[1].sql = "balance";
	Refuse([&] { BindFactProgram(bad); }, "indirect cycle refused");
	bad = Input();
	bad.metrics[1].name = "BALANCE";
	Refuse([&] { BindFactProgram(bad); }, "case insensitive duplicate declarations refused");
	bad = Input();
	bad.metrics.clear();
	bad.windows.clear();
	for (int i = 0; i < 17; i++)
		bad.metrics.push_back({"m" + to_string(i), i ? "m" + to_string(i - 1) : "SUM(revenue.amount)"});
	Refuse([&] { BindFactProgram(bad); }, "seventeen-level dependency refused with cached upstream");
	bad.metrics.pop_back();
	Check(BindFactProgram(bad).metrics.size() == 16, "sixteen-level dependency boundary accepted");
	bad.metrics.clear();
	for (int i = 0; i < 12; i++)
		bad.metrics.push_back(
		    {"m" + to_string(i), i ? "m" + to_string(i - 1) + " + m" + to_string(i - 1) : "SUM(revenue.amount)"});
	Refuse([&] { BindFactProgram(bad); }, "exponential expansion rejected before oversized reference copy");
	bad = Input();
	bad.metrics[0].sql = string(16385, ' ');
	Refuse([&] { BindFactProgram(bad); }, "byte budget before parsing");
	bad = Input();
	bad.metrics[1].sql = "SUM(\"revenue\".amount)";
	Refuse([&] { BindFactProgram(bad); }, "quoted logical identifiers refused");
	bad = Input();
	bad.sources[0].fields.resize(257);
	Refuse([&] { BindFactProgram(bad); }, "source field budget validated before source metadata copy");
	bad = Input();
	bad.sources[0].fields[0].physical_column = string(257, 'x');
	Refuse([&] { BindFactProgram(bad); }, "physical column bytes validated before source metadata copy");
	for (auto &sql : vector<string> {"income>=1.001", "revenue_rows>=1.5", "income>=NULL", "income>=1e100",
	                                 "unknown>=1", "income>=expense"})
		Refuse([&] { CompileFactUsers(program, {"income"}, "common", Predicate(sql), Sources()); },
		       "reject cohort boundary: " + sql);
	Refuse([&] { CompileFactUsers(program, {"income"}, "union", a, Sources()); }, "unsupported fact domain refused");
	Refuse(
	    [&] {
		    CompileFactUsers(program, {"income", "INCOME"}, "common", a, Sources());
	    },
	    "duplicate selected metric refused");
	Refuse([&] { CompileFactWindows(program, {"previous"}, "display_range", start, end, Sources()); },
	       "pre-window input filtering refused");
	Refuse([&] { CompileFactWindows(program, {"previous"}, "entire_capture", end, start, Sources()); },
	       "reversed display dates refused");
	auto null_sources = Sources();
	null_sources[0].reset();
	Refuse([&] { CompileFactUsers(program, {"income"}, "common", a, std::move(null_sources)); },
	       "missing explicit source AST refused");
	Query(engine, "INSERT INTO r VALUES ('MIN',DATE '1970-01-01'+(-2147483648)::INTEGER,1.01)");
	auto minimum = Value::DATE(date_t(std::numeric_limits<int32_t>::min()));
	auto minimum_tree =
	    CompileFactWindows(program, {"previous", "rolling"}, "entire_capture", minimum, minimum, Sources());
	auto minimum_prepared = engine.Prepare(minimum_tree->Copy());
	Check(!minimum_prepared->HasError(), "finite INT32_MIN typed window tree binds without date text cast");
	auto minimum_result = minimum_prepared->Execute();
	Check(!minimum_result->HasError(), "finite INT32_MIN typed tree executes");
	auto minimum_chunk = minimum_result->Fetch();
	Check(minimum_chunk && minimum_chunk->size() == 1 && minimum_chunk->GetValue(0, 0).ToString() == "MIN" &&
	          minimum_chunk->GetValue(1, 0).GetValue<date_t>().days == std::numeric_limits<int32_t>::min() &&
	          minimum_chunk->GetValue(2, 0).IsNull() && minimum_chunk->GetValue(3, 0).ToString() == "1.01",
	      "complete minimum-date row and exact sparse window values preserved");
	Check(!minimum_result->Fetch() && !minimum_result->HasError(), "minimum-date stream drains without error");
	Check(engine.Query(minimum_tree->ToString())->HasError(),
	      "rendered date text fails independently; typed plan is not promised replayable");
}
} // namespace
int main() {
	try {
		Run();
		std::cout << "PASS fact program " << checks << " checks\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
