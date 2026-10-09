#include "ossie/compile.hpp"
#include "ossie/parser.hpp"
#include "ossie/state.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/query_result.hpp"
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
int checks = 0;
void Check(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
	checks++;
}
struct JoinThread {
	std::thread &thread;
	~JoinThread() {
		if (thread.joinable()) {
			thread.join();
		}
	}
};
duckdb::shared_ptr<duckdb::ossie::Model> ReadModel(const std::string &path) {
	std::ifstream file(path);
	if (!file) {
		throw std::runtime_error("missing model fixture: " + path);
	}
	std::stringstream text;
	text << file.rdbuf();
	return duckdb::make_shared_ptr<duckdb::ossie::Model>(
	    duckdb::ossie::ParseModel(text.str(), duckdb::ossie::RebindMap()));
}
std::string Compile(const duckdb::ossie::OssieSnapshot &snapshot, bool filter) {
	duckdb::ossie::CompileOptions options;
	options.allow_filter_functions = snapshot.allow_filter_functions;
	return duckdb::ossie::CompileToSQL(
	    *snapshot.model, {"total"}, {},
	    filter ? duckdb::vector<duckdb::string> {"ABS(facts.amount) > 0"} : duckdb::vector<duckdb::string>(), options);
}
void ControlledSnapshot(const std::string &root) {
	duckdb::ossie::OssieState state;
	Check(!state.GetSnapshot().model && !state.GetSnapshot().allow_filter_functions, "initial empty snapshot");
	auto first = ReadModel(root + "test/fixtures/state_snapshot/a.json");
	auto second = ReadModel(root + "test/fixtures/state_snapshot/b.json");
	duckdb::weak_ptr<const duckdb::ossie::Model> old = first;
	state.SetModel(first, false);
	std::mutex barrier;
	std::condition_variable changed;
	bool captured = false, published = false;
	std::exception_ptr reader_error;
	std::thread reader([&] {
		try {
			auto snapshot = state.GetSnapshot();
			{
				std::unique_lock<std::mutex> guard(barrier);
				captured = true;
				changed.notify_all();
				if (!changed.wait_for(guard, std::chrono::seconds(5), [&] { return published; })) {
					throw std::runtime_error("writer barrier timed out");
				}
			}
			Check(snapshot.model->name == "A" && !snapshot.allow_filter_functions, "old model/policy paired");
			Check(!old.expired(), "captured reader retains replaced model");
			Check(Compile(snapshot, false).find("facts_a") != std::string::npos, "old model source retained");
			bool refused = false;
			try {
				Compile(snapshot, true);
			} catch (const duckdb::InvalidInputException &error) {
				refused = std::string(error.what()).find("allow_filter_functions") != std::string::npos;
			}
			Check(refused, "A policy refuses function filter after B publication");
		} catch (...) {
			reader_error = std::current_exception();
		}
	});
	JoinThread join {reader};
	{
		std::unique_lock<std::mutex> guard(barrier);
		if (!changed.wait_for(guard, std::chrono::seconds(5), [&] { return captured; })) {
			throw std::runtime_error("reader barrier timed out");
		}
		state.SetModel(second, true);
		first.reset();
		published = true;
		changed.notify_all();
	}
	reader.join();
	if (reader_error) {
		std::rethrow_exception(reader_error);
	}
	Check(old.expired(), "replaced model released after last reader");
	auto current = state.GetSnapshot();
	Check(current.model->name == "B" && current.allow_filter_functions, "new model/policy paired");
	auto sql = Compile(current, true);
	Check(sql.find("facts_b") != std::string::npos && sql.find("* 2") != std::string::npos &&
	          sql.find("abs(") != std::string::npos,
	      "B model and policy jointly determine compiled behavior");
}
std::string Quote(const std::string &value) {
	return "'" + duckdb::StringUtil::Replace(value, "'", "''") + "'";
}
duckdb::unique_ptr<duckdb::QueryResult> SQL(duckdb::Connection &connection, const std::string &text) {
	auto result = connection.Query(text);
	if (result->HasError()) {
		throw std::runtime_error(result->GetError());
	}
	return result;
}
std::string Scalar(duckdb::Connection &connection, const std::string &text) {
	return SQL(connection, text)->Fetch()->GetValue(0, 0).ToString();
}
void ConnectionAndTiming(const std::string &root) {
	duckdb::DuckDB database(nullptr);
	duckdb::Connection first(database), second(database);
	SQL(first, "LOAD ossie");
	Check(first.Query("SELECT * FROM ossie_query(['total'])")->HasError(), "query without model refused");
	SQL(first, "PREPARE deferred AS SELECT ossie_compile(['total'], [], [])");
	Check(first.Query("EXECUTE deferred")->HasError(), "scalar executes without model and is refused");
	SQL(first, "CREATE TABLE facts_a AS SELECT 1 amount UNION ALL SELECT 2");
	SQL(first, "CREATE TABLE facts_b AS SELECT 10 amount UNION ALL SELECT 20");
	SQL(first, "CALL ossie_load(" + Quote(root + "test/fixtures/state_snapshot/a.json") + ")");
	SQL(first, "PREPARE bound AS SELECT * FROM ossie_query(['total'])");
	SQL(first, "PREPARE policy AS SELECT ossie_compile(['total'], [], ['ABS(facts.amount) > 0'])");
	Check(Scalar(first, "EXECUTE deferred").find("facts_a") != std::string::npos, "scalar execution reads A");
	Check(first.Query("EXECUTE policy")->HasError(), "prepared scalar A policy refuses filter");
	SQL(second,
	    "CALL ossie_load(" + Quote(root + "test/fixtures/state_snapshot/b.json") + ", allow_filter_functions => true)");
	Check(Scalar(first, "EXECUTE deferred").find("facts_b") != std::string::npos, "prepared scalar execution reads B");
	Check(Scalar(first, "EXECUTE policy").find("abs(") != std::string::npos, "prepared scalar reads B policy");
	Check(Scalar(first, "EXECUTE bound") == "3", "prepared table query retains A binding");
	Check(Scalar(first, "SELECT * FROM ossie_query(['total'], [], ['ABS(facts.amount) > 0'])") == "60",
	      "new cross-connection query reads B model and policy");
	Check(first.Query("CALL ossie_load('does-not-exist.json')")->HasError(), "failed load refused");
	auto snapshot = duckdb::ossie::OssieState::Get(*first.context).GetSnapshot();
	Check(snapshot.model->name == "B" && snapshot.allow_filter_functions, "failed load retains paired B state");
	Check(Scalar(second, "SELECT * FROM ossie_query(['total'], [], ['ABS(facts.amount) > 0'])") == "60",
	      "failed load leaves B computation unchanged");
}
} // namespace
int main(int argc, char **argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("usage: ossie_state_test PROJECT_ROOT");
		}
		std::string root = argv[1];
		if (root.empty() || root.back() != '/') {
			root += '/';
		}
		ControlledSnapshot(root);
		ConnectionAndTiming(root);
		std::cout << "PASS: " << checks
		          << " checks; controlled publication, lifetime, compile behavior, cross-connection timing\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << "\n";
		return 1;
	}
}
