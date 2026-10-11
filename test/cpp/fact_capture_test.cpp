#include "ossie/population_testing.hpp"
#include "ossie/parser.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/materialized_query_result.hpp"
#include "duckdb/main/prepared_statement.hpp"
#include "duckdb/common/exception.hpp"
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace {
using namespace duckdb;
using namespace duckdb::ossie;
int checks = 0;
void Check(bool ok, const string &why) {
	if (!ok)
		throw std::runtime_error(why);
	checks++;
	std::cout << "PASS: " << why << '\n';
}
string Read(const string &path) {
	std::ifstream file(path);
	if (!file)
		throw std::runtime_error("missing fixture " + path);
	std::stringstream out;
	out << file.rdbuf();
	return out.str();
}
string Literal(const string &s) {
	string out = "'";
	for (auto c : s) {
		out += c;
		if (c == '\'')
			out += c;
	}
	return out + "'";
}
unique_ptr<MaterializedQueryResult> Query(Connection &c, const string &s) {
	auto r = c.Query(s);
	if (r->HasError())
		throw std::runtime_error(r->GetError());
	return r;
}
unique_ptr<MaterializedQueryResult> Execute(PreparedStatement &p) {
	vector<Value> parameters;
	auto r = p.Execute(parameters, false);
	if (r->HasError())
		throw std::runtime_error(r->GetError());
	return unique_ptr_cast<QueryResult, MaterializedQueryResult>(std::move(r));
}
unique_ptr<PreparedStatement> Prepare(Connection &c, bool window = false) {
	auto p = c.Prepare(PopulationFactStatementForTesting(*c.context, window));
	if (p->HasError())
		throw std::runtime_error(p->GetError());
	return p;
}
void Baseline(Connection &c) {
	auto p = Prepare(c);
	auto r = Execute(*p);
	Check(r->RowCount() == 3 && r->GetValue(0, 0).ToString() == "A" && r->GetValue(0, 1).ToString() == "B" &&
	          r->GetValue(0, 2).ToString() == "C",
	      "actual frozen common domain A/B/C retains all-NULL C and excludes D/E");
	Check(r->GetValue(1, 0).ToString() == "290.00" && r->GetValue(2, 0).ToString() == "375.00" &&
	          r->GetValue(3, 0).ToString() == "85.00",
	      "actual dual frozen collections produce independent cents oracle 29000/37500/8500");
	Check(r->GetValue(1, 2).IsNull() && r->GetValue(4, 2).ToString() == "1" && r->GetValue(5, 2).ToString() == "0",
	      "frozen NULL amount and COUNT(User)/COUNT(value) distinction preserved");
	auto w = Prepare(c, true);
	auto rows = Execute(*w);
	Check(rows->RowCount() == 8 && rows->GetValue(0, 2).ToString() == "A" &&
	          rows->GetValue(2, 2).ToString() == "200.00" && rows->GetValue(3, 2).ToString() == "250.00",
	      "actual frozen sparse-date LAG and ROWS sum preserve January predecessor");
	Check(rows->types[2] == LogicalType::DECIMAL(18, 2) && rows->types[3] == LogicalType::DECIMAL(38, 2),
	      "actual LAG and SUM window types differ and bind independently");
}
class Observer : public PopulationLoadObserver {
public:
	explicit Observer(std::function<void(PopulationLoadCheckpoint)> action) : action(std::move(action)) {
	}
	void Observe(PopulationLoadCheckpoint p) override {
		action(p);
	}
	std::function<void(PopulationLoadCheckpoint)> action;
};
class FactObserver : public PopulationFactObserver {
public:
	explicit FactObserver(std::function<void(PopulationFactCheckpoint)> fn) : fn(std::move(fn)) {
	}
	void Observe(PopulationFactCheckpoint checkpoint) override {
		fn(checkpoint);
	}
	std::function<void(PopulationFactCheckpoint)> fn;
};
struct Gate {
	std::mutex lock;
	std::condition_variable cv;
	bool arrived = false, released = false;
	void Block() {
		std::unique_lock<std::mutex> g(lock);
		arrived = true;
		cv.notify_all();
		if (!cv.wait_for(g, std::chrono::seconds(10), [&] { return released; }))
			throw std::runtime_error("gate release timeout");
	}
	void Wait() {
		std::unique_lock<std::mutex> g(lock);
		if (!cv.wait_for(g, std::chrono::seconds(10), [&] { return arrived; }))
			throw std::runtime_error("gate arrival timeout");
	}
	void Release() {
		std::lock_guard<std::mutex> g(lock);
		released = true;
		cv.notify_all();
	}
};
struct Attempt {
	Connection &c;
	Gate gate;
	unique_ptr<MaterializedQueryResult> result;
	std::exception_ptr error;
	std::thread thread;
	Attempt(Connection &c, const string &load, PopulationLoadCheckpoint checkpoint) : c(c) {
		c.context->registered_state->Insert(PopulationLoadObserver::KEY,
		                                    make_shared_ptr<Observer>([this, checkpoint](PopulationLoadCheckpoint p) {
			                                    if (p == checkpoint)
				                                    gate.Block();
		                                    }));
		thread = std::thread([this, load] {
			try {
				result = this->c.Query(load);
			} catch (...) {
				error = std::current_exception();
			}
		});
	}
	void Finish() {
		gate.Release();
		if (thread.joinable())
			thread.join();
		c.context->registered_state->Remove(PopulationLoadObserver::KEY);
		if (error)
			std::rethrow_exception(error);
	}
	~Attempt() {
		gate.Release();
		if (thread.joinable())
			thread.join();
		c.context->registered_state->Remove(PopulationLoadObserver::KEY);
	}
};
void Run(const string &root, int threads) {
	DuckDB db(nullptr);
	Connection loader(db), writer(db), reader(db);
	Query(loader, "SET threads=" + std::to_string(threads));
	Query(reader, "SET SESSION default_collation='binary'");
	auto fixtures = root + "test/fixtures/population-fact-program/";
	auto load = "CALL ossie_bundle_load(" + Literal(fixtures + "bundle.json") + ")";
	bool old_parse_refused = false;
	try {
		ParseModel(Read(fixtures + "core.json"), RebindMap());
	} catch (const InvalidInputException &) {
		old_parse_refused = true;
	}
	Check(old_parse_refused, "default core parser still refuses bare metric references");
	Check(
	    ParseModel(Read(fixtures + "core.json"), RebindMap(), MetricReferencePolicy::DECLARED_METRICS).metrics.size() ==
	        9,
	    "explicit fact-only parser accepts known metric references and window metadata");
	Query(writer, Read(fixtures + "rows.sql"));
	auto initial = Query(loader, load);
	Check(initial->GetValue(0, 0).ToString() == "multi-fact-user-program-v1" &&
	          initial->GetValue(3, 0).ToString() == "18",
	      "production loader publishes parent over two independent captures");
	auto original_hash = initial->GetValue(2, 0).ToString();
	initial.reset();
	Baseline(reader);
	Check(reader.Query(PopulationFactStatementForTesting(*reader.context, false)->ToString())->HasError(),
	      "rendered SQL without frozen AST dependencies is refused");

	const auto users_request = string("{\"domain\":\"common\",\"input_scope\":\"entire_capture\",\"metrics\":["
	                                  "\"balance\"],\"cohort\":{\"operation\":\"all\"}}");
	const auto windows_request =
	    string("{\"input_scope\":\"entire_capture\",\"windows\":[\"previous\",\"rolling\"],\"display\":{\"mode\":"
	           "\"date_range\",\"start_date\":\"2026-02-01\",\"end_date\":\"2026-02-01\"}}");
	const auto users_sql = "SELECT * FROM ossie_fact_users_json(" + Literal(users_request) + ")";
	const auto windows_sql = "SELECT * FROM ossie_fact_windows_json(" + Literal(windows_request) + ")";
	auto public_users = reader.Prepare(users_sql), public_window = reader.Prepare(windows_sql);
	Check(!public_users->HasError() && !public_window->HasError(),
	      "public prepared JSON binds both actual typed projections");
	reader.context->registered_state->Insert(
	    PopulationFactObserver::KEY,
	    make_shared_ptr<FactObserver>([&](PopulationFactCheckpoint) { throw std::runtime_error("compile executed"); }));
	auto compile = Query(reader, "SELECT * FROM ossie_fact_users_compile(" + Literal(users_request) + ")");
	Check(compile->GetValue(0, 0).ToString().find("\"counts_computed\":false") != string::npos,
	      "public compile performs no execution or count checkpoints");
	reader.context->registered_state->Remove(PopulationFactObserver::KEY);
	for (auto checkpoint : {PopulationFactCheckpoint::AFTER_DOMAIN, PopulationFactCheckpoint::BEFORE_SORT,
	                        PopulationFactCheckpoint::BEFORE_ENCODE}) {
		reader.context->registered_state->Insert(PopulationFactObserver::KEY,
		                                         make_shared_ptr<FactObserver>([&](PopulationFactCheckpoint p) {
			                                         if (p == checkpoint)
				                                         reader.Interrupt();
		                                         }));
		auto canceled = reader.Query(users_sql);
		Check(canceled->HasError() && canceled->GetErrorObject().Type() == ExceptionType::INTERRUPT,
		      "public query cancellation after count/sort/encode returns native INTERRUPT, no partial JSON");
		reader.context->registered_state->Remove(PopulationFactObserver::KEY);
		Check(Query(reader, users_sql)->RowCount() == 1, "complete public query succeeds after canceled execution");
	}
	auto retained = Prepare(reader), retained_window = Prepare(reader, true);
	auto old_life = PopulationFactLifetimeForTesting(*reader.context);
	Check(old_life.size() == 3 && old_life[0]() && old_life[1]() && old_life[2](),
	      "native weak probes observe live parent and two children");
	for (auto &sql : vector<string> {"SELECT * FROM ossie_concepts()", "SELECT * FROM ossie_population_metrics()",
	                                 "SELECT * FROM ossie_objects('User',[])", "SELECT * FROM ossie_relation_program()",
	                                 "SELECT * FROM ossie_daily_users_json('User','{}')"})
		Check(reader.Query(sql)->HasError(), "old profile API refuses fact program: " + sql);
	Query(writer, "UPDATE revenue SET value=1 WHERE who='A';UPDATE cost SET value=2 WHERE who='A'");
	auto refreshed = Query(loader, load);
	Check(refreshed->GetValue(2, 0).ToString() != original_hash, "paired parent hash changes when sources change");
	refreshed.reset();
	Check(Execute(*retained)->GetValue(1, 0).ToString() == "290.00" &&
	          Execute(*retained_window)->GetValue(2, 2).ToString() == "200.00",
	      "prepared old aggregate and single-source window survive reload with original values");
	Query(writer, "DROP TABLE revenue;DROP TABLE cost");
	Check(Execute(*retained)->GetValue(2, 0).ToString() == "375.00" &&
	          Execute(*retained_window)->GetValue(3, 2).ToString() == "250.00",
	      "both prepared paths use frozen AST dependencies after source deletion");

	Query(writer, "CREATE MACRO count_star() AS 42::BIGINT;CREATE MACRO \"-\"(a,b) AS 99::DECIMAL(38,2);CREATE MACRO "
	              "sum(x) AS 97::DECIMAL(38,2)");
	auto old_users = Execute(*public_users)->GetValue(0, 0).ToString();
	auto old_window = Execute(*public_window)->GetValue(0, 0).ToString();
	Check(old_users.find("[\"A\",\"290.00\"]") != string::npos &&
	          old_users.find("\"common_user_count\":\"3\"") != string::npos &&
	          old_window.find("[\"A\",\"2026-02-01\",\"200.00\",\"250.00\"]") != string::npos,
	      "public prepared results preserve old paired snapshot after reload and source deletion");
	Query(writer, "DROP MACRO count_star;DROP MACRO \"-\";DROP MACRO sum");
	Check(old_users.find("\"common_user_count\":\"3\"") != string::npos &&
	          old_users.find("[\"A\",\"290.00\"]") != string::npos,
	      "public prepared results remain stable when same-name persistent macros are created after bind");
	public_users.reset();
	public_window.reset();
	retained.reset();
	Check(old_life[0]() && old_life[1]() && old_life[2](),
	      "single-source window retains old parent and unused second child");
	retained_window.reset();
	Query(reader, "SELECT 1");
	Check(!old_life[0]() && !old_life[1]() && !old_life[2](),
	      "releasing prepared statements releases old parent and children without ownership cycle");
	Query(writer, Read(fixtures + "rows.sql"));
	Query(loader, load);
	{
		Attempt attempt(loader, load, PopulationLoadCheckpoint::AFTER_FACT_CAPTURE);
		attempt.gate.Wait();
		Query(writer, "BEGIN;UPDATE revenue SET value=1 WHERE who='A';UPDATE cost SET value=2 WHERE who='A';COMMIT");
		attempt.Finish();
		Check(attempt.result && !attempt.result->HasError() &&
		          attempt.result->GetValue(2, 0).ToString() == original_hash,
		      "concurrent two-table write between captures does not mix source transaction versions");
	}
	Baseline(reader);
	{
		Attempt attempt(loader, load, PopulationLoadCheckpoint::AFTER_FACT_CAPTURE);
		attempt.gate.Wait();
		loader.Interrupt();
		attempt.Finish();
		Check(attempt.result && attempt.result->HasError() &&
		          attempt.result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
		      "second-capture interruption preserves native INTERRUPT class");
	}
	Baseline(reader);
	{
		Attempt attempt(loader, load, PopulationLoadCheckpoint::BEFORE_PUBLISH);
		attempt.gate.Wait();
		Baseline(reader);
		loader.Interrupt();
		attempt.Finish();
		Check(attempt.result && attempt.result->HasError() &&
		          attempt.result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
		      "complete new pair interrupted before publication preserves old parent");
	}
	Baseline(reader);
	Query(writer, "DROP TABLE cost");
	Check(loader.Query(load)->HasError(), "second capture failure refuses incomplete parent publication");
	Baseline(reader);
	Query(writer, "DROP TABLE revenue");
	Query(writer, Read(fixtures + "rows.sql"));
	for (auto &bad : vector<string> {
	         "INSERT INTO cost VALUES ('A','2026-01-01','X',10)", "INSERT INTO revenue VALUES ('A','2026-01-01',999)",
	         "INSERT INTO cost VALUES ('E','2026-02-01','X',99)",
	         "INSERT INTO revenue VALUES (NULL,DATE '2026-01-01',1)", "INSERT INTO revenue VALUES ('Z',NULL,1)",
	         "INSERT INTO revenue VALUES ('Z',DATE 'infinity',1)",
	         "INSERT INTO revenue VALUES ('',DATE '2026-01-01',1)",
	         "INSERT INTO cost VALUES ('Z','2026-01-01','',1)"}) {
		Query(writer, "BEGIN");
		Query(writer, bad);
		Query(writer, "COMMIT");
		Check(loader.Query(load)->HasError(), "full capture preflight rejects invalid or duplicate identity: " + bad);
		Query(writer, "DROP TABLE revenue;DROP TABLE cost");
		Query(writer, Read(fixtures + "rows.sql"));
		Baseline(reader);
	}
	Query(writer,
	      "INSERT INTO cost SELECT 'LIMIT'||i::VARCHAR,DATE '2026-01-01','X',1::DECIMAL(18,2) FROM range(10001)t(i)");
	Check(loader.Query(load)->HasError(), "over10000 rows in second fact refused without truncation");
	Baseline(reader);
	Query(writer, "DROP TABLE revenue;DROP TABLE cost");
	Query(writer, Read(fixtures + "rows.sql"));
	Query(writer, "ALTER TABLE cost ALTER value TYPE DOUBLE");
	Check(loader.Query(load)->HasError(), "actual floating amount refused despite declared Decimal label");
	Baseline(reader);
	Query(writer, "DROP TABLE revenue;DROP TABLE cost");
	Query(writer,
	      "CREATE TABLE revenue AS SELECT repeat('u',4500)||i::VARCHAR who, DATE '2026-01-01' AS day, 1::DECIMAL(18,2) "
	      "AS value FROM range(2000)t(i); CREATE TABLE cost AS SELECT who,day,'X'::VARCHAR item,value FROM revenue");
	Check(loader.Query(load)->HasError(),
	      "two individually under-budget captures refused when combined canonical bytes exceed16MiB");
	Baseline(reader);
	Query(writer, "DROP TABLE revenue;DROP TABLE cost");
	Query(writer, Read(fixtures + "rows.sql"));
	Query(writer, "DELETE FROM revenue;DELETE FROM cost");
	auto empty = Query(loader, load);
	Check(empty->GetValue(3, 0).ToString() == "0" && Execute(*Prepare(reader))->RowCount() == 0 &&
	          Execute(*Prepare(reader, true))->RowCount() == 0,
	      "empty captures preserve metadata, source types and empty full domains");
	auto empty_hash = empty->GetValue(2, 0).ToString();
	empty.reset();
	Query(writer, "ALTER TABLE cost ADD COLUMN unused INTEGER");
	auto metadata_changed = Query(loader, load);
	Check(metadata_changed->GetValue(2, 0).ToString() != empty_hash,
	      "empty capture physical metadata contributes to paired parent hash");
	metadata_changed.reset();
	Query(writer, "DROP TABLE revenue;DROP TABLE cost");
	Query(writer, Read(fixtures + "rows.sql"));
	Query(writer, "INSERT INTO revenue VALUES ('a','2026-01-01',1);INSERT INTO cost VALUES ('a','2026-01-01','X',1)");
	Query(loader, "SET default_collation='nocase';SET default_order='DESC'");
	Query(loader, load);
	auto binary = Execute(*Prepare(reader));
	Check(binary->RowCount() == 4 && binary->GetValue(0, 0).ToString() == "A" &&
	          binary->GetValue(0, 3).ToString() == "a",
	      "load preflight and explicit binary execution preserve raw case-distinct User identities");

	Query(reader, "SET SESSION default_collation='nocase';SET SESSION default_order='DESC'");
	auto raw_public = Query(reader, users_sql)->GetValue(0, 0).ToString();
	Check(raw_public.find("[\"A\",\"290.00\"]") != string::npos &&
	          raw_public.find("[\"a\",\"0.00\"]") != string::npos &&
	          raw_public.find("\"common_user_count\":\"4\"") != string::npos,
	      "public private binary execution/count preserves A/a under caller nocase");
	Query(reader, "SET SESSION default_collation='binary'");
	Check(Execute(*Prepare(reader, true))->GetValue(2, 2).ToString() == "200.00",
	      "window direction remains ascending under caller descending default");
}
} // namespace
int main(int argc, char **argv) {
	try {
		if (argc != 2)
			throw std::runtime_error("ROOT argument required");
		Run(argv[1], 4);
		Run(argv[1], 1);
		std::cout << "PASS fact capture " << checks << " checks; threads4/1\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
