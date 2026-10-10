#include "ossie/population_testing.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/materialized_query_result.hpp"
#include "duckdb/main/prepared_statement.hpp"
#include "duckdb.hpp"
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace {
using namespace duckdb;
using namespace duckdb::ossie;
int checks = 0;
void Check(bool condition, const string &reason) {
	if (!condition)
		throw std::runtime_error(reason);
	checks++;
}
unique_ptr<MaterializedQueryResult> Query(Connection &connection, const string &sql) {
	auto result = connection.Query(sql);
	if (result->HasError())
		throw std::runtime_error(sql + ": " + result->GetError());
	return result;
}
string Scalar(Connection &connection, const string &sql) {
	auto result = Query(connection, sql);
	Check(result->RowCount() == 1 && result->ColumnCount() == 1, "scalar shape");
	return result->GetValue(0, 0).ToString();
}
string Read(const string &path) {
	std::ifstream file(path);
	if (!file)
		throw std::runtime_error("missing fixture " + path);
	std::stringstream contents;
	contents << file.rdbuf();
	return contents.str();
}
string Literal(const string &text) {
	string result = "'";
	for (auto c : text) {
		result += c;
		if (c == '\'')
			result += c;
	}
	return result + "'";
}
struct Gate {
	std::mutex lock;
	std::condition_variable changed;
	bool arrived = false, released = false;
	void Block() {
		std::unique_lock<std::mutex> guard(lock);
		arrived = true;
		changed.notify_all();
		if (!changed.wait_for(guard, std::chrono::seconds(5), [&] { return released; }))
			throw std::runtime_error("capture gate release timed out");
	}
	void Wait() {
		std::unique_lock<std::mutex> guard(lock);
		if (!changed.wait_for(guard, std::chrono::seconds(5), [&] { return arrived; }))
			throw std::runtime_error("capture gate arrival timed out");
	}
	void Release() {
		std::lock_guard<std::mutex> guard(lock);
		released = true;
		changed.notify_all();
	}
};
class Observer : public PopulationLoadObserver {
public:
	explicit Observer(std::function<void(PopulationLoadCheckpoint)> action) : action(std::move(action)) {
	}
	void Observe(PopulationLoadCheckpoint checkpoint) override {
		action(checkpoint);
	}
	std::function<void(PopulationLoadCheckpoint)> action;
};
struct Attempt {
	Connection &connection;
	Gate gate;
	unique_ptr<MaterializedQueryResult> result;
	std::exception_ptr failure;
	std::thread worker;
	Attempt(Connection &connection, const string &load, PopulationLoadCheckpoint checkpoint) : connection(connection) {
		connection.context->registered_state->Insert(
		    PopulationLoadObserver::KEY,
		    make_shared_ptr<Observer>([this, checkpoint](PopulationLoadCheckpoint reached) {
			    if (checkpoint == reached)
				    gate.Block();
		    }));
		try {
			worker = std::thread([this, load] {
				try {
					result = this->connection.Query(load);
				} catch (...) {
					failure = std::current_exception();
				}
			});
		} catch (...) {
			connection.context->registered_state->Remove(PopulationLoadObserver::KEY);
			throw;
		}
	}
	void Finish() {
		gate.Release();
		if (worker.joinable())
			worker.join();
		connection.context->registered_state->Remove(PopulationLoadObserver::KEY);
		if (failure)
			std::rethrow_exception(failure);
	}
	~Attempt() {
		gate.Release();
		if (worker.joinable())
			worker.join();
		connection.context->registered_state->Remove(PopulationLoadObserver::KEY);
	}
};
const string TOTAL = "SELECT amount_sum FROM ossie_concept_query('User',['amount_sum'],[],[])";
const string MEMBERS = "SELECT string_agg(entity_id,',' ORDER BY entity_id) FROM ossie_objects('ValidUser',[])";
const string SNAPSHOT = "SELECT snapshot_id FROM ossie_concepts() WHERE concept='User'";
const string HASH = "SELECT content_sha256 FROM ossie_concepts() WHERE concept='User'";
const string META = "SELECT execution_target FROM ossie_concept_compile('User',['amount_sum'],[],[])";
void Controls(const string &root, int threads) {
	// Read all required inputs before starting any thread; missing fixtures are fatal.
	auto rows = Read(root + "test/fixtures/safe-join-research/rows.sql");
	auto manifest = root + "test/fixtures/joined-population/bundle.json";
	Read(manifest);
	DuckDB database(nullptr);
	Connection loader(database), writer(database), reader(database);
	Query(reader, "SET threads=" + std::to_string(threads));
	Check(Scalar(reader, "SELECT current_setting('threads')") == std::to_string(threads),
	      "configured scheduler threads");
	Check(Scalar(reader, "SELECT library_version || ':' || source_id FROM pragma_version()") == "v1.5.6:069cc9f9b5",
	      "pinned native engine identity");
	auto load = "CALL ossie_bundle_load(" + Literal(manifest) + ")";
	Query(writer, rows);
	Query(loader, load);
	auto old_hash = Scalar(reader, HASH);
	auto old_metadata = Scalar(reader, META);
	Check(Scalar(reader, TOTAL) == "55.01", "baseline total");
	Check(Scalar(reader, MEMBERS) == "f1,f2,f5,f6", "baseline identities");
	auto prepared = reader.Prepare(TOTAL);
	Check(!prepared->HasError(), "prepared old request");
	{
		Attempt attempt(loader, load, PopulationLoadCheckpoint::AFTER_FACT_CAPTURE);
		attempt.gate.Wait();
		// A single writer transaction commits distinguishable versions of both tables.
		Query(writer, "BEGIN; UPDATE r6_fact SET amount=109.01 WHERE id='f1'; "
		              "UPDATE r6_dimension SET label='Changed',eligible=false WHERE id=1; COMMIT;");
		Check(Scalar(writer, "SELECT amount FROM r6_fact WHERE id='f1'") == "109.01", "writer fact committed");
		Check(Scalar(writer, "SELECT label FROM r6_dimension WHERE id=1") == "Changed", "writer dimension committed");
		Check(Scalar(reader, TOTAL) == "55.01", "old publication visible while load blocked");
		attempt.Finish();
		Check(attempt.result && !attempt.result->HasError(), "captured transaction load succeeds");
	}
	Check(Scalar(reader, HASH) == old_hash, "both complete input hashes remain old");
	Check(Scalar(reader, META) == old_metadata, "input metadata and provenance remain old");
	Check(Scalar(reader, TOTAL) == "55.01", "captured old fact value");
	Check(Scalar(reader, MEMBERS) == "f1,f2,f5,f6", "captured old dimension predicate");
	Check(Scalar(reader, "SELECT count(*) FROM ossie_objects('User',[]) WHERE \"dimension.label\"='Changed'") == "0",
	      "captured dimension label is old");
	Check(Scalar(reader, "SELECT \"fact.amount\" FROM ossie_objects('User',[]) WHERE entity_id='f1'") == "10.01",
	      "captured object fact value is old");
	Query(loader, load);
	Check(Scalar(reader, HASH) != old_hash, "unobserved reload sees new inputs");
	Check(Scalar(reader, META) != old_metadata, "reload input evidence changes");
	Check(Scalar(reader, TOTAL) == "154.01", "reload new total");
	Check(Scalar(reader, MEMBERS) == "f5,f6", "reload new dimension predicate");
	Check(Scalar(reader, "SELECT count(*) FROM ossie_objects('User',[]) WHERE \"dimension.label\"='Changed'") == "2",
	      "reload sees new label");
	auto original = prepared->Execute();
	Check(!original->HasError() && original->Fetch()->GetValue(0, 0).ToString() == "55.01", "prepared old snapshot");
	auto before = Scalar(reader, SNAPSHOT), hash_before = Scalar(reader, HASH);
	for (auto checkpoint : {PopulationLoadCheckpoint::BEFORE_PUBLISH, PopulationLoadCheckpoint::AFTER_FACT_CAPTURE}) {
		Attempt attempt(loader, load, checkpoint);
		attempt.gate.Wait();
		loader.Interrupt();
		attempt.Finish();
		Check(attempt.result && attempt.result->HasError(), "controlled capture/prepublication cancellation fails");
		Check(attempt.result->GetError().find("Interrupt") != string::npos, "explicit cancellation error");
		Check(attempt.result->GetErrorObject().Type() == ExceptionType::INTERRUPT &&
		          attempt.result->GetError().find("ossie_population_error_v1:") == string::npos,
		      "interrupt keeps native type without population envelope");
		Check(Scalar(reader, SNAPSHOT) == before && Scalar(reader, HASH) == hash_before,
		      "cancellation retains identity/hash");
		Check(Scalar(reader, TOTAL) == "154.01" && Scalar(reader, MEMBERS) == "f5,f6", "cancel retains values/members");
	}
	original = prepared->Execute();
	Check(!original->HasError() && original->Fetch()->GetValue(0, 0).ToString() == "55.01",
	      "prepared old snapshot remains executable after both cancellation phases");
	loader.context->registered_state->Insert(PopulationLoadObserver::KEY,
	                                         make_shared_ptr<Observer>([](PopulationLoadCheckpoint point) {
		                                         if (point == PopulationLoadCheckpoint::AFTER_FACT_CAPTURE)
			                                         throw std::runtime_error("observer injected failure");
	                                         }));
	auto failed = loader.Query(load);
	loader.context->registered_state->Remove(PopulationLoadObserver::KEY);
	Check(failed->HasError() && failed->GetError().find("observer injected failure") != string::npos,
	      "observer exception propagated");
	Check(Scalar(reader, SNAPSHOT) == before && Scalar(reader, HASH) == hash_before, "callback failure retains batch");
	loader.context->registered_state->Insert(
	    PopulationLoadObserver::KEY, make_shared_ptr<Observer>([](PopulationLoadCheckpoint point) {
		    if (point == PopulationLoadCheckpoint::AFTER_FACT_CAPTURE)
			    throw std::system_error(std::make_error_code(std::errc::invalid_argument),
			                            "controlled unknown native failure");
	    }));
	failed = loader.Query(load);
	loader.context->registered_state->Remove(PopulationLoadObserver::KEY);
	Check(failed->HasError() && failed->GetError().find("controlled unknown native failure") != string::npos,
	      "unknown system failure propagated");
	Check(failed->GetError().find("ossie_population_error_v1:") == string::npos,
	      "unknown system failure is not downgraded to population input error");
	Check(Scalar(reader, SNAPSHOT) == before && Scalar(reader, HASH) == hash_before,
	      "unknown system failure retains frozen batch");
	failed = loader.Query("CALL ossie_bundle_load('" + root + "test/fixtures/population/missing.json')");
	Check(failed->HasError() && failed->GetError().find("ossie-population-error-v1") != string::npos,
	      "native missing-file load has structured envelope");
	Check(Scalar(reader, SNAPSHOT) == before, "native missing-file failure retains frozen batch");
	Query(loader, load);
	Check(Scalar(reader, SNAPSHOT) != before && Scalar(reader, HASH) == hash_before,
	      "observer removed; reload succeeds");
	Check(Scalar(reader, TOTAL) == "154.01", "final valid snapshot query");
}
void ObservationControls(const string &root, int threads) {
	DuckDB database(nullptr);
	Connection loader(database), writer(database), reader(database);
	Query(reader, "SET threads=" + std::to_string(threads));
	Query(writer, "CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd "
	              "DECIMAL(18,2),classification_value INTEGER)");
	Query(writer, "INSERT INTO observations SELECT * FROM read_csv(" +
	                  Literal(root + "docs/research/ossie-req-001/multi-period-research/observations.csv") +
	                  ",header=true)");
	auto load10 =
	    "CALL ossie_bundle_load(" + Literal(root + "test/fixtures/population-observation/bundle-10.json") + ")";
	auto load20 =
	    "CALL ossie_bundle_load(" + Literal(root + "test/fixtures/population-observation/bundle-20.json") + ")";
	const string total = "SELECT amount_sum FROM ossie_concept_query('QualifiedObservation',['amount_sum'],[],[])";
	const string snapshot = "SELECT snapshot_id FROM ossie_concepts_v2() WHERE concept='DailyObservation'";
	Query(writer, "SET GLOBAL default_collation='nocase'");
	Query(loader, load10);
	Check(Scalar(reader, "SELECT current_setting('default_collation')") == "nocase",
	      "observation private engine leaves global collation unchanged");
	auto before = Scalar(reader, snapshot);
	auto prepared = reader.Prepare(total);
	Check(!prepared->HasError(), "observation prepared bind");
	Check(Scalar(reader, total) == "31.00", "observation initial sum");
	Query(writer, "UPDATE observations SET amount_usd=11 WHERE observation_id='o4'");
	{
		Attempt attempt(loader, load20, PopulationLoadCheckpoint::BEFORE_PUBLISH);
		attempt.gate.Wait();
		Check(Scalar(reader, snapshot) == before && Scalar(reader, total) == "31.00",
		      "observation readers keep old bundle while load waits");
		Check(Scalar(reader, "SELECT captured_user_count FROM ossie_concepts_v2() WHERE concept='User'") == "4",
		      "observation old captured User set");
		Query(writer, "UPDATE observations SET amount_usd=777 WHERE observation_id='o4'");
		attempt.Finish();
		Check(attempt.result && !attempt.result->HasError(), "observation valid candidate publishes");
	}
	Check(Scalar(reader, total) == "18.00", "observation source mutation after freeze is excluded");
	Check(Scalar(reader, snapshot) != before, "observation atomic publication changes snapshot");
	auto original = prepared->Execute();
	Check(!original->HasError() && original->Fetch()->GetValue(0, 0).ToString() == "31.00",
	      "observation prepared old bundle remains executable");
	before = Scalar(reader, snapshot);
	{
		Attempt attempt(loader, load10, PopulationLoadCheckpoint::BEFORE_PUBLISH);
		attempt.gate.Wait();
		loader.Interrupt();
		attempt.Finish();
		Check(attempt.result && attempt.result->HasError() &&
		          attempt.result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
		      "observation cancellation keeps native interrupt");
	}
	Check(Scalar(reader, snapshot) == before && Scalar(reader, total) == "18.00",
	      "observation cancellation retains old complete bundle");
	Query(writer, "INSERT INTO observations SELECT * FROM observations WHERE observation_id='o1'");
	auto failed = loader.Query(load10);
	Check(failed->HasError() && failed->GetError().find("duplicate observation tuple") != string::npos,
	      "native observation duplicate preflight refuses");
	Check(Scalar(reader, snapshot) == before && Scalar(reader, total) == "18.00",
	      "native failed tuple preflight retains old bundle");
}

} // namespace
int main(int argc, char **argv) {
	try {
		if (argc != 2)
			throw std::runtime_error("usage: ossie_population_capture_test PROJECT_ROOT");
		string root = argv[1];
		if (root.empty() || root.back() != '/')
			root += '/';
		Controls(root, 4);
		Controls(root, 1);
		ObservationControls(root, 4);
		ObservationControls(root, 1);
		std::cout
		    << "PASS: " << checks
		    << " checks; deterministic production capture, capture/prepublish cancel, callback failure; threads=4/1\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << "\n";
		return 1;
	}
}
