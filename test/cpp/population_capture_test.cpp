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
	// library_version is derived from git tags, which shallow checkouts lack; the commit is the pin.
	auto source_id = Scalar(reader, "SELECT source_id FROM pragma_version()");
	Check(source_id.size() >= 7 && string("069cc9f9b5be802405797faecc284961b07c70ef").rfind(source_id, 0) == 0,
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
class DailyCounter : public PopulationDailyObserver {
public:
	void Observe(PopulationDailyCheckpoint) override {
		count++;
	}
	idx_t count = 0;
};
class DailyObserver : public PopulationDailyObserver {
public:
	DailyObserver(Gate &gate, PopulationDailyCheckpoint checkpoint) : gate(gate), checkpoint(checkpoint) {
	}
	void Observe(PopulationDailyCheckpoint reached) override {
		if (reached == checkpoint) {
			gate.Block();
		}
	}
	Gate &gate;
	PopulationDailyCheckpoint checkpoint;
};
void DailyCancel(Connection &connection, const string &sql, PopulationDailyCheckpoint checkpoint) {
	Gate gate;
	unique_ptr<MaterializedQueryResult> result;
	connection.context->registered_state->Insert(PopulationDailyObserver::KEY,
	                                             make_shared_ptr<DailyObserver>(gate, checkpoint));
	std::exception_ptr failure;
	std::thread worker([&]() {
		try {
			result = connection.Query(sql);
		} catch (...) {
			failure = std::current_exception();
		}
	});
	try {
		gate.Wait();
		connection.Interrupt();
		gate.Release();
		worker.join();
	} catch (...) {
		gate.Release();
		if (worker.joinable()) {
			worker.join();
		}
		connection.context->registered_state->Remove(PopulationDailyObserver::KEY);
		throw;
	}
	connection.context->registered_state->Remove(PopulationDailyObserver::KEY);
	if (failure) {
		std::rethrow_exception(failure);
	}
	Check(result && result->HasError() && result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
	      "daily query cancellation preserves native interrupt");
}
class DailyStateCounter : public PopulationDailyStateObserver {
public:
	void Observe(PopulationDailyStateCheckpoint) override {
		count++;
	}
	idx_t count = 0;
};
class DailyStateObserver : public PopulationDailyStateObserver {
public:
	DailyStateObserver(Gate &gate, PopulationDailyStateCheckpoint checkpoint) : gate(gate), checkpoint(checkpoint) {
	}
	void Observe(PopulationDailyStateCheckpoint reached) override {
		if (reached == checkpoint) {
			gate.Block();
		}
	}
	Gate &gate;
	PopulationDailyStateCheckpoint checkpoint;
};
void DailyStateCancel(Connection &connection, const string &sql, PopulationDailyStateCheckpoint checkpoint) {
	Gate gate;
	unique_ptr<MaterializedQueryResult> result;
	connection.context->registered_state->Insert(PopulationDailyStateObserver::KEY,
	                                             make_shared_ptr<DailyStateObserver>(gate, checkpoint));
	std::exception_ptr failure;
	std::thread worker([&]() {
		try {
			result = connection.Query(sql);
		} catch (...) {
			failure = std::current_exception();
		}
	});
	try {
		gate.Wait();
		connection.Interrupt();
		gate.Release();
		worker.join();
	} catch (...) {
		gate.Release();
		if (worker.joinable()) {
			worker.join();
		}
		connection.context->registered_state->Remove(PopulationDailyStateObserver::KEY);
		throw;
	}
	connection.context->registered_state->Remove(PopulationDailyStateObserver::KEY);
	if (failure) {
		std::rethrow_exception(failure);
	}
	Check(result && result->HasError() && result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
	      "daily state cancellation preserves native interrupt");
}
class RelationCounter : public PopulationRelationObserver {
public:
	void Observe(PopulationRelationCheckpoint) override {
		count++;
	}
	idx_t count = 0;
};
class RelationObserver : public PopulationRelationObserver {
public:
	RelationObserver(Gate &gate, PopulationRelationCheckpoint checkpoint) : gate(gate), checkpoint(checkpoint) {
	}
	void Observe(PopulationRelationCheckpoint reached) override {
		if (reached == checkpoint) {
			gate.Block();
		}
	}
	Gate &gate;
	PopulationRelationCheckpoint checkpoint;
};
void RelationCancel(Connection &connection, const string &sql, PopulationRelationCheckpoint checkpoint) {
	Gate gate;
	unique_ptr<MaterializedQueryResult> result;
	connection.context->registered_state->Insert(PopulationRelationObserver::KEY,
	                                             make_shared_ptr<RelationObserver>(gate, checkpoint));
	std::exception_ptr failure;
	std::thread worker([&]() {
		try {
			result = connection.Query(sql);
		} catch (...) {
			failure = std::current_exception();
		}
	});
	try {
		gate.Wait();
		connection.Interrupt();
		gate.Release();
		worker.join();
	} catch (...) {
		gate.Release();
		if (worker.joinable()) {
			worker.join();
		}
		connection.context->registered_state->Remove(PopulationRelationObserver::KEY);
		throw;
	}
	connection.context->registered_state->Remove(PopulationRelationObserver::KEY);
	if (failure) {
		std::rethrow_exception(failure);
	}
	Check(result && result->HasError() && result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
	      "relation query cancellation preserves native interrupt");
}
void ObservationControls(const string &root, int threads) {
	DuckDB database(nullptr);
	Connection loader(database), writer(database), reader(database);
	Query(reader, "SET threads=" + std::to_string(threads));
	Query(writer, "CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd "
	              "DECIMAL(18,2),classification_value INTEGER)");
	Query(writer, "INSERT INTO observations SELECT * FROM read_csv(" +
	                  Literal(root + "test/fixtures/population-data/observations.csv") + ",header=true)");
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
	const string daily =
	    "SELECT result_json FROM ossie_daily_users_json('QualifiedObservation', "
	    "'{\"start_date\":\"2026-01-31\",\"end_date\":\"2026-02-01\",\"products\":{\"mode\":\"all\"}}')";
	auto daily_before = Scalar(reader, daily);
	Check(daily_before.find("\"members\":[\"B\"]") != string::npos, "native daily membership");
	const string daily_plan = "SELECT plan_json" + daily.substr(daily.find(" FROM "));
	const string plan_sql = StringUtil::Replace(daily_plan, "ossie_daily_users_json", "ossie_daily_users_compile");
	auto counter = make_shared_ptr<DailyCounter>();
	reader.context->registered_state->Insert(PopulationDailyObserver::KEY, counter);
	auto plan_before = Scalar(reader, plan_sql);
	Check(counter->count == 0, "daily compile does not enter execution or member encoding checkpoints");
	Check(Scalar(reader, daily) == daily_before && counter->count == 2,
	      "daily execute reaches both counter checkpoints");
	reader.context->registered_state->Remove(PopulationDailyObserver::KEY);
	auto plan_prepared = reader.Prepare(plan_sql);
	Check(!plan_prepared->HasError(), "daily plan prepared frozen binding");
	auto daily_prepared = reader.Prepare(daily);
	Check(!daily_prepared->HasError(), "daily prepared binds frozen bundle");
	for (auto checkpoint : {PopulationDailyCheckpoint::BEFORE_EXECUTE, PopulationDailyCheckpoint::BEFORE_ENCODE}) {
		DailyCancel(reader, daily, checkpoint);
		Check(Scalar(reader, daily) == daily_before, "daily native cancellation retains complete bundle and recovers");
	}
	const string states = "SELECT result_json FROM ossie_daily_states_json('QualifiedObservation', "
	                      "'{\"start_date\":\"2026-01-31\",\"end_date\":\"2026-02-01\",\"products\":{\"mode\":\"all\"},"
	                      "\"user_domain\":\"captured_users\",\"transitions\":\"within_request_days\"}')";
	auto states_before = Scalar(reader, states);
	Check(states_before.find("\"user_count\":\"4\"") != string::npos &&
	          states_before.find("\"state_count\":\"8\"") != string::npos,
	      "daily states use entire captured User domain");
	const string states_plan_sql = StringUtil::Replace("SELECT plan_json" + states.substr(states.find(" FROM ")),
	                                                   "ossie_daily_states_json", "ossie_daily_states_compile");
	auto states_counter = make_shared_ptr<DailyStateCounter>();
	reader.context->registered_state->Insert(PopulationDailyStateObserver::KEY, states_counter);
	auto states_plan_before = Scalar(reader, states_plan_sql);
	Check(states_counter->count == 0, "daily state compile does not enter execution checkpoints");
	Check(Scalar(reader, states) == states_before && states_counter->count == 4,
	      "daily state execution reaches four cancellation checkpoints");
	reader.context->registered_state->Remove(PopulationDailyStateObserver::KEY);
	auto states_prepared = reader.Prepare(states);
	auto states_plan_prepared = reader.Prepare(states_plan_sql);
	Check(!states_prepared->HasError() && !states_plan_prepared->HasError(), "prepared states and plan frozen bind");
	for (auto checkpoint :
	     {PopulationDailyStateCheckpoint::BEFORE_EXECUTE, PopulationDailyStateCheckpoint::AFTER_DOMAIN,
	      PopulationDailyStateCheckpoint::BEFORE_EXPAND, PopulationDailyStateCheckpoint::BEFORE_FINISH}) {
		DailyStateCancel(reader, states, checkpoint);
		Check(Scalar(reader, states) == states_before, "daily state cancellation preserves snapshot and recovers");
	}

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
	auto daily_old = daily_prepared->Execute();
	Check(!daily_old->HasError() && daily_old->Fetch()->GetValue(0, 0).ToString() == daily_before,
	      "daily prepared retains old rule and all trace across reload");
	Check(Scalar(reader, daily) != daily_before, "fresh daily binds new complete rule and trace");
	auto plan_old = plan_prepared->Execute();
	Check(!plan_old->HasError() && plan_old->Fetch()->GetValue(0, 0).ToString() == plan_before,
	      "prepared daily plan retains old rule and all trace across reload");
	Check(Scalar(reader, plan_sql) != plan_before, "fresh daily plan binds new rule and trace");
	auto states_old = states_prepared->Execute();
	Check(!states_old->HasError() && states_old->Fetch()->GetValue(0, 0).ToString() == states_before,
	      "prepared states retain old complete rule and domain across reload");
	auto states_plan_old = states_plan_prepared->Execute();
	Check(!states_plan_old->HasError() && states_plan_old->Fetch()->GetValue(0, 0).ToString() == states_plan_before,
	      "prepared state plan retains old snapshot across reload");
	Check(Scalar(reader, states) != states_before && Scalar(reader, states_plan_sql) != states_plan_before,
	      "fresh states and plan bind newly published rule and snapshot");

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
	Check(Scalar(reader, daily).find("\"members\":[\"B\"]") != string::npos,
	      "daily failed whole-batch tuple reload retains usable prior batch");
}

class LegacyBindObserver : public PopulationLegacyBindObserver {
public:
	explicit LegacyBindObserver(Gate &gate_p) : gate(gate_p) {
	}
	void Observe() override {
		gate.Block();
	}
	Gate &gate;
};

void RelationBindControls(const string &root, int threads) {
	DuckDB database(nullptr);
	Connection loader(database), reader(database);
	Query(reader, "SET threads=" + std::to_string(threads));
	Query(loader, "CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd "
	              "DECIMAL(18,2),classification_value INTEGER)");
	Query(loader,
	      "INSERT INTO observations VALUES('a','A','2026-01-01','X',1,10),('b','B','2026-02-01','X',NULL,NULL)");
	auto old_load =
	    "CALL ossie_bundle_load(" + Literal(root + "test/fixtures/population-observation/bundle-10.json") + ")";
	auto new_load =
	    "CALL ossie_bundle_load(" + Literal(root + "test/fixtures/population-relation-program/bundle.json") + ")";
	for (bool old_first : {true, false}) {
		Query(loader, old_first ? old_load : new_load);
		Gate gate;
		reader.context->registered_state->Insert(PopulationLegacyBindObserver::KEY,
		                                         make_shared_ptr<LegacyBindObserver>(gate));
		unique_ptr<MaterializedQueryResult> result;
		std::exception_ptr failure;
		std::thread worker([&] {
			try {
				result = reader.Query("SELECT count(*) FROM ossie_objects('QualifiedObservation',[])");
			} catch (...) {
				failure = std::current_exception();
			}
		});
		try {
			gate.Wait();
			Query(loader, old_first ? new_load : old_load);
			gate.Release();
			worker.join();
			reader.context->registered_state->Remove(PopulationLegacyBindObserver::KEY);
		} catch (...) {
			gate.Release();
			if (worker.joinable()) {
				worker.join();
			}
			reader.context->registered_state->Remove(PopulationLegacyBindObserver::KEY);
			throw;
		}
		if (failure) {
			std::rethrow_exception(failure);
		}
		if (old_first) {
			Check(result && !result->HasError() && result->GetValue(0, 0).GetValue<int64_t>() == 1,
			      "legacy bind and gate use same old snapshot across concurrent relation publication");
			auto fresh = reader.Query("SELECT * FROM ossie_objects('QualifiedObservation',[])");
			Check(fresh->HasError() && fresh->GetError().find("UNSUPPORTED_CAPABILITY") != string::npos,
			      "fresh legacy bind refuses published relation profile");
		} else {
			Check(result && result->HasError() && result->GetError().find("UNSUPPORTED_CAPABILITY") != string::npos,
			      "captured relation profile remains refused across concurrent old-profile publication");
			Check(Scalar(reader, "SELECT count(*) FROM ossie_objects('QualifiedObservation',[])") == "1",
			      "fresh legacy bind recovers on newly published old profile");
		}
	}
	Query(loader, old_load);
	auto old_snapshot = Scalar(reader, "SELECT snapshot_id FROM ossie_concepts_v2() WHERE concept='DailyObservation'");
	{
		Attempt attempt(loader, new_load, PopulationLoadCheckpoint::BEFORE_PUBLISH);
		attempt.gate.Wait();
		Check(Scalar(reader, "SELECT snapshot_id FROM ossie_concepts_v2() WHERE concept='DailyObservation'") ==
		          old_snapshot,
		      "validated relation candidate remains unpublished at prepublish checkpoint");
		loader.Interrupt();
		attempt.Finish();
		Check(attempt.result && attempt.result->HasError() &&
		          attempt.result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
		      "relation prepublish cancellation preserves native interrupt");
	}
	Check(Scalar(reader, "SELECT snapshot_id FROM ossie_concepts_v2() WHERE concept='DailyObservation'") ==
	          old_snapshot,
	      "cancelled relation candidate preserves old complete profile");
	Query(loader, new_load);
	Check(Scalar(reader,
	             "SELECT contains(program_json,'\"load_constraints_validated\":true') FROM ossie_relation_program()") ==
	          "true",
	      "relation load recovers after cancellation");
	const string objects = "SELECT result_json FROM ossie_relation_objects_json('audited_user')";
	const string plan = "SELECT plan_json FROM ossie_relation_compile('audited_user')";
	auto before = Scalar(reader, objects);
	auto counter = make_shared_ptr<RelationCounter>();
	reader.context->registered_state->Insert(PopulationRelationObserver::KEY, counter);
	Check(Scalar(reader, plan).find("\"members_computed\":false") != string::npos && counter->count == 0,
	      "relation compile enters no execution/sort/encoding checkpoints");
	Check(Scalar(reader, objects) == before && counter->count == 3, "relation execution reaches all three checkpoints");
	reader.context->registered_state->Remove(PopulationRelationObserver::KEY);
	for (auto checkpoint : {PopulationRelationCheckpoint::BEFORE_EXECUTE, PopulationRelationCheckpoint::BEFORE_SORT,
	                        PopulationRelationCheckpoint::BEFORE_ENCODE}) {
		RelationCancel(reader, objects, checkpoint);
		Check(Scalar(reader, objects) == before, "relation cancel retains complete frozen links and recovers");
	}
}

} // namespace
class HistoryObserver : public PopulationHistoryObserver {
public:
	explicit HistoryObserver(std::function<void(PopulationHistoryCheckpoint)> action) : action(std::move(action)) {
	}
	void Observe(PopulationHistoryCheckpoint point) override {
		action(point);
	}
	std::function<void(PopulationHistoryCheckpoint)> action;
};

void HistoryControls(const string &root, int threads) {
	DuckDB database(nullptr);
	Connection loader(database), writer(database), reader(database);
	Query(reader, "SET threads=" + to_string(threads));
	Query(writer, "CREATE TABLE observations(user_id VARCHAR,ds DATE,product VARCHAR,observation_id VARCHAR DEFAULT "
	              "'o',amount_usd DECIMAL(18,2) DEFAULT 1.00,classification_value INTEGER DEFAULT 10)");
	Query(writer, "CREATE TABLE history(record_id VARCHAR,user_id VARCHAR,team_id VARCHAR,starts DATE,ends DATE)");
	Query(writer, "INSERT INTO observations(user_id,ds,product) VALUES ('u',DATE '2024-02-28','X')");
	Query(writer, "INSERT INTO history VALUES ('a','u','T',DATE '2024-02-28',DATE '2024-03-01')");
	auto load =
	    "CALL ossie_bundle_load(" + Literal(root + "test/fixtures/population-history-program/bundle.json") + ")";
	const string program = "SELECT program_json FROM ossie_history_program()";
	Query(writer, "SET GLOBAL default_collation='nocase'");
	Query(loader, load);
	auto before = Scalar(reader, program);
	const auto original_program = before;
	Check(before.find("\"query_supported\":true") != string::npos, "history honest supported query declaration");
	auto prepared = reader.Prepare(program);
	Check(!prepared->HasError(), "history program prepared parent bind");
	const string links_sql = "SELECT result_json FROM "
	                         "ossie_history_links_json('team_at_observation','{\"dates\":{\"mode\":\"all\"},"
	                         "\"products\":{\"mode\":\"all\"}}')";
	const string plan_sql = StringUtil::Replace(StringUtil::Replace(links_sql, "result_json", "plan_json"),
	                                            "ossie_history_links_json", "ossie_history_links_compile");
	int probes = 0;
	reader.context->registered_state->Insert(
	    PopulationHistoryObserver::KEY,
	    make_shared_ptr<HistoryObserver>([&](PopulationHistoryCheckpoint) { probes++; }));
	auto original_plan = Scalar(reader, plan_sql);
	Check(probes == 0, "history compile does not scan/match/encode result");
	auto original_links = Scalar(reader, links_sql);
	Check(probes == 4, "history actual execution reaches four checkpoints");
	Check(original_links.find("\"links\":[[\"u\",\"2024-02-28\",\"X\",\"T\"]]") != string::npos,
	      "history native full link identity");
	reader.context->registered_state->Remove(PopulationHistoryObserver::KEY);
	auto prepared_links = reader.Prepare(links_sql), prepared_plan = reader.Prepare(plan_sql);
	Check(!prepared_links->HasError() && !prepared_plan->HasError(), "history links/plan prepared binding");
	for (auto point : {PopulationHistoryCheckpoint::BEFORE_EXECUTE, PopulationHistoryCheckpoint::BEFORE_SORT,
	                   PopulationHistoryCheckpoint::BEFORE_ENCODE, PopulationHistoryCheckpoint::BEFORE_FINISH}) {
		reader.context->registered_state->Insert(PopulationHistoryObserver::KEY,
		                                         make_shared_ptr<HistoryObserver>([&](PopulationHistoryCheckpoint at) {
			                                         if (at == point)
				                                         reader.Interrupt();
		                                         }));
		auto cancelled = reader.Query(links_sql);
		reader.context->registered_state->Remove(PopulationHistoryObserver::KEY);
		Check(cancelled->HasError() && cancelled->GetErrorObject().Type() == ExceptionType::INTERRUPT,
		      "history native execute interrupt preserved");
		Check(Scalar(reader, links_sql) == original_links, "history interrupted execution recovery same parent");
	}
	for (auto point :
	     {PopulationLoadCheckpoint::AFTER_HISTORY_OBSERVATION_CAPTURE, PopulationLoadCheckpoint::BEFORE_PUBLISH}) {
		Attempt attempt(loader, load, point);
		attempt.gate.Wait();
		Check(Scalar(reader, program) == before, "history blocked load not published");
		loader.Interrupt();
		attempt.Finish();
		Check(attempt.result && attempt.result->HasError(), "history interrupt error");
		Check(attempt.result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
		      "history native interrupt classification");
		Check(Scalar(reader, program) == before, "history cancelled replacement preserves parent");
		Query(loader, load);
		before = Scalar(reader, program);
	}
	const string hash =
	    "SELECT regexp_extract(program_json,'\"content_sha256\":\"([^\"]+)\"',1) FROM ossie_history_program()";
	auto original_hash = Scalar(reader, hash);
	{
		Attempt attempt(loader, load, PopulationLoadCheckpoint::AFTER_HISTORY_OBSERVATION_CAPTURE);
		attempt.gate.Wait();
		Query(writer, "BEGIN TRANSACTION");
		Query(writer, "UPDATE observations SET product='Y'");
		Query(writer, "UPDATE history SET team_id='NEW'");
		Query(writer, "COMMIT");
		attempt.Finish();
		Check(attempt.result && !attempt.result->HasError(), "history coherent concurrent load finishes");
		Check(Scalar(reader, hash) == original_hash, "both sources share old transaction snapshot");
	}
	Query(loader, load);
	Check(Scalar(reader, hash) != original_hash, "explicit history reload sees both committed changes");
	auto old_load =
	    "CALL ossie_bundle_load(" + Literal(root + "test/fixtures/population-observation/bundle-10.json") + ")";
	for (bool old_first : {true, false}) {
		Query(loader, old_first ? old_load : load);
		Gate gate;
		reader.context->registered_state->Insert(PopulationLegacyBindObserver::KEY,
		                                         make_shared_ptr<LegacyBindObserver>(gate));
		unique_ptr<MaterializedQueryResult> result;
		std::exception_ptr failure;
		std::thread worker([&] {
			try {
				result = reader.Query("SELECT count(*) FROM ossie_objects('QualifiedObservation',[])");
			} catch (...) {
				failure = std::current_exception();
			}
		});
		try {
			gate.Wait();
			Query(loader, old_first ? load : old_load);
			gate.Release();
			worker.join();
			reader.context->registered_state->Remove(PopulationLegacyBindObserver::KEY);
		} catch (...) {
			gate.Release();
			if (worker.joinable())
				worker.join();
			reader.context->registered_state->Remove(PopulationLegacyBindObserver::KEY);
			throw;
		}
		if (failure)
			std::rethrow_exception(failure);
		if (old_first) {
			Check(result && !result->HasError() && result->GetValue(0, 0).GetValue<int64_t>() == 1,
			      "legacy binding keeps captured old population through history publication");
			auto fresh = reader.Query("SELECT * FROM ossie_population_capabilities()");
			Check(fresh->HasError() && fresh->GetError().find("UNSUPPORTED_CAPABILITY") != string::npos,
			      "fresh legacy discovery rejects history safely");
		} else {
			Check(result && result->HasError() && result->GetError().find("UNSUPPORTED_CAPABILITY") != string::npos,
			      "captured history remains rejected after old-profile publication");
			Check(Scalar(reader, "SELECT count(*) FROM ossie_objects('QualifiedObservation',[])") == "1",
			      "fresh legacy query recovers on old population");
		}
	}
	Query(loader, load);
	Query(writer, "DROP TABLE history");
	Query(writer, "DROP TABLE observations");
	auto saved = prepared->Execute();
	Check(!saved->HasError(), "prepared history program after both drops");
	auto saved_chunk = saved->Fetch();
	Check(saved_chunk && saved_chunk->size() == 1 && saved_chunk->ColumnCount() == 1,
	      "history prepared result is one complete metadata row");
	Check(!saved_chunk->GetValue(0, 0).IsNull() && saved_chunk->GetValue(0, 0).ToString() == original_program,
	      "old prepared parent metadata survives reload exactly");
	Check(!saved->Fetch() && !saved->HasError(), "history prepared stream exhausted without error");
	for (auto item :
	     {std::make_pair(prepared_links.get(), original_links), std::make_pair(prepared_plan.get(), original_plan)}) {
		auto old_result = item.first->Execute();
		Check(!old_result->HasError(), "history prepared links/plan after both source drops");
		auto old_chunk = old_result->Fetch();
		Check(old_chunk && old_chunk->size() == 1 && old_chunk->ColumnCount() == 1 &&
		          !old_chunk->GetValue(0, 0).IsNull() && old_chunk->GetValue(0, 0).ToString() == item.second,
		      "history prepared complete links/plan exactly retains old parent");
		Check(!old_result->Fetch() && !old_result->HasError(), "history prepared links/plan stream exhausted");
	}
	Check(Scalar(reader, links_sql) != original_links && Scalar(reader, plan_sql) != original_plan,
	      "history fresh links/plan binds new parent after reload/drop");
	Check(Scalar(reader, program) != original_program, "current history parent differs from old prepared metadata");
	Check(Scalar(reader, "SELECT current_setting('default_collation')") == "nocase",
	      "private history engine does not mutate caller collation");
}

void ConceptRequiresControls(const string &root, int threads) {
	for (bool joined : {false, true}) {
		auto directory = root + "test/fixtures/concept-requires-" + (joined ? "joined/" : "single/");
		auto rows = Read(directory + "rows.sql");
		auto manifest = directory + "bundle.json";
		Read(manifest);
		DuckDB database(nullptr);
		Connection loader(database), writer(database), reader(database);
		Query(reader, "SET threads=" + std::to_string(threads));
		Query(writer, rows);
		auto load = "CALL ossie_bundle_load(" + Literal(manifest) + ")";
		auto concept = joined ? string("Shipment") : string("Device");
		auto child = joined ? string("ReadyShipment") : string("ActiveDevice");
		auto snapshot = "SELECT snapshot_id FROM ossie_concepts() WHERE concept=" + Literal(concept);
		auto members =
		    "SELECT string_agg(entity_id,',' ORDER BY entity_id) FROM ossie_objects(" + Literal(child) + ",[])";
		Query(loader, load);
		Check(Scalar(reader, members) == (joined ? "s1,s2" : "d1"), "requires keeps complete child identities");
		Check(Scalar(reader, "SELECT count(*) FROM ossie_objects(" + Literal(concept) + ",[])") == (joined ? "4" : "3"),
		      "requires validates rather than filtering root objects");
		auto original = Scalar(reader, snapshot);
		auto prepared = reader.Prepare(members);
		Check(!prepared->HasError(), "prepare requires-enabled old snapshot");
		Query(writer, joined ? "UPDATE req_batches SET expires=DATE '2024-12-31' WHERE batch_id=30"
		                     : "UPDATE req_devices SET capacity=-1 WHERE asset_id='d3'");
		auto failed = loader.Query(load);
		Check(failed->HasError() && failed->GetError().find("CONSTRAINT_VIOLATION") != string::npos &&
		          failed->GetError().find(concept) != string::npos,
		      "root constraint cannot hide a non-child violation");
		Check(Scalar(reader, snapshot) == original, "constraint failure preserves old snapshot");
		Query(writer, joined ? "UPDATE req_batches SET expires=DATE '2025-12-31' WHERE batch_id=30"
		                     : "UPDATE req_devices SET capacity=0 WHERE asset_id='d3'");
		Query(writer, joined ? "UPDATE req_shipments SET ready=TRUE WHERE shipment_id='s3'"
		                     : "UPDATE req_devices SET active=TRUE WHERE asset_id='d2'");
		failed = loader.Query(load);
		Check(failed->HasError() && failed->GetError().find("CONSTRAINT_VIOLATION") != string::npos &&
		          failed->GetError().find(child) != string::npos,
		      "child constraint rejects explicitly forbidden NULL");
		Check(Scalar(reader, snapshot) == original, "child constraint failure preserves old snapshot");
		Query(writer, joined ? "UPDATE req_shipments SET ready=FALSE WHERE shipment_id='s3'"
		                     : "UPDATE req_devices SET active=FALSE WHERE asset_id='d2'");
		{
			Attempt attempt(loader, load, PopulationLoadCheckpoint::BEFORE_PUBLISH);
			attempt.gate.Wait();
			loader.Interrupt();
			attempt.Finish();
			Check(attempt.result && attempt.result->HasError() &&
			          attempt.result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
			      "requires prepublication cancellation retains native INTERRUPT");
			Check(Scalar(reader, snapshot) == original, "requires cancellation does not publish");
		}
		Query(writer, joined ? "UPDATE req_shipments SET ready=FALSE" : "UPDATE req_devices SET active=FALSE");
		Query(loader, load);
		Check(Scalar(reader, "SELECT count(*) FROM ossie_objects(" + Literal(child) + ",[])") == "0",
		      "empty child satisfies valid universal constraints without fabricating members");
		Query(writer, joined ? "DROP TABLE req_shipments; DROP TABLE req_batches" : "DROP TABLE req_devices");
		auto old = prepared->Execute();
		Check(!old->HasError() && old->Fetch()->GetValue(0, 0).ToString() == (joined ? "s1,s2" : "d1"),
		      "prepared requires snapshot survives reload and source removal");
	}
}

void StringJoinControls(const string &root, int threads) {
	for (bool dimension_value : {false, true}) {
		auto directory = root + "test/fixtures/string-join-" + (dimension_value ? "dimension/" : "fact/");
		auto manifest = directory + "bundle.json";
		Read(manifest);
		DuckDB database(nullptr);
		Connection loader(database), writer(database), reader(database);
		Query(reader, "SET threads=" + std::to_string(threads));
		if (dimension_value) {
			Query(writer, "CREATE TABLE sj_b_f(shipment_id VARCHAR,batch_id VARCHAR,ready BOOLEAN,expires DATE);"
			              "CREATE TABLE sj_b_d(batch_id VARCHAR,expires DATE,label VARCHAR);"
			              "INSERT INTO sj_b_f VALUES('s1','1',true,DATE '1900-01-01'),('s2','01',false,NULL),"
			              "('s3','n'||chr(0)||'x',false,NULL),('s4','n',false,NULL);"
			              "INSERT INTO sj_b_d VALUES('1',DATE '2026-01-01','one'),('01',DATE '2025-01-01','zero-one'),"
			              "('n'||chr(0)||'x',NULL,'nul'),('n',DATE '2025-01-01','prefix'),('',NULL,'unused')");
		} else {
			Query(writer, "CREATE TABLE sj_a_f(asset_id VARCHAR,join_key VARCHAR,capacity INTEGER,active BOOLEAN);"
			              "CREATE TABLE sj_a_d(lookup_key VARCHAR,label VARCHAR,capacity INTEGER);"
			              "INSERT INTO sj_a_f VALUES('s1','1',1,true),('s2','01',2,false),"
			              "('s3','n'||chr(0)||'x',NULL,false),('s4','n',4,false);"
			              "INSERT INTO sj_a_d VALUES('1','one',-999),('01','zero-one',-999),"
			              "('n'||chr(0)||'x','nul',-999),('n','prefix',-999),('','unused',-999)");
		}
		auto concept = dimension_value ? string("Shipment") : string("Device");
		auto child = dimension_value ? string("ReadyShipment") : string("ActiveDevice");
		auto fact = dimension_value ? string("sj_b_f") : string("sj_a_f");
		auto dim = dimension_value ? string("sj_b_d") : string("sj_a_d");
		auto fk = dimension_value ? string("batch_id") : string("join_key");
		auto label = dimension_value ? string("batches.label") : string("lookup.label");
		auto load = "CALL ossie_bundle_load(" + Literal(manifest) + ")";
		auto snapshot = "SELECT snapshot_id FROM ossie_population_capabilities()";
		auto full = "SELECT string_agg(entity_id || ':' || \"" + label +
		            "\",',' ORDER BY entity_id) FROM ossie_objects(" + Literal(concept) + ",[])";
		const string answer = "s1:one,s2:zero-one,s3:nul,s4:prefix";
		Query(loader, load);
		Check(Scalar(reader, full) == answer,
		      "String keys retain complete byte-sensitive identity association including NUL");
		Check(Scalar(reader, "SELECT api_version FROM ossie_population_capabilities()") == "2.4.0",
		      "String capability explicitly upgrades API");
		Check(Scalar(reader, "SELECT string_agg(entity_id,',' ORDER BY entity_id) FROM ossie_objects(" +
		                         Literal(child) + ",[])") == "s1",
		      "String join keeps independent Integer/DATE child constraints");
		auto original = Scalar(reader, snapshot);
		auto prepared = reader.Prepare(full);
		Check(!prepared->HasError(), "prepare String full association");
		Query(writer, dimension_value ? "INSERT INTO sj_b_d VALUES('',NULL,'duplicate')"
		                              : "INSERT INTO sj_a_d VALUES('','duplicate',0)");
		auto failed = loader.Query(load);
		Check(failed->HasError() && failed->GetError().find("IDENTITY_VIOLATION") != string::npos,
		      "String unused dimension duplicates rejected");
		Check(Scalar(reader, snapshot) == original, "String duplicate failure retains old snapshot");
		Query(writer, "DELETE FROM " + dim + " WHERE label='duplicate'");
		Query(writer, "UPDATE " + fact + " SET " + fk + "='missing' WHERE " + fk + "='01'");
		failed = loader.Query(load);
		Check(failed->HasError() && failed->GetError().find("IDENTITY_VIOLATION") != string::npos,
		      "String inactive fact missing key cannot hide behind filter");
		Check(Scalar(reader, snapshot) == original, "String missing key failure retains old snapshot");
		Query(writer, "UPDATE " + fact + " SET " + fk + "='01' WHERE " + fk + "='missing'");
		{
			Attempt attempt(loader, load, PopulationLoadCheckpoint::BEFORE_PUBLISH);
			attempt.gate.Wait();
			loader.Interrupt();
			attempt.Finish();
			Check(attempt.result && attempt.result->HasError() &&
			          attempt.result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
			      "String prepublication cancellation retains native INTERRUPT");
			Check(Scalar(reader, snapshot) == original, "String cancellation retains old snapshot");
		}
		Query(writer, "UPDATE " + dim + " SET label='changed'");
		Query(loader, load);
		Check(Scalar(reader, full) != answer, "String successful reload uses new source values");
		Query(writer, "DROP TABLE " + fact + "; DROP TABLE " + dim);
		auto old = prepared->Execute();
		Check(!old->HasError() && old->Fetch()->GetValue(0, 0).ToString() == answer,
		      "String prepared full association survives reload/source removal");
	}
}

void DirectDerivedControls(const string &root, int threads) {
	for (bool joined : {false, true}) {
		DuckDB database(nullptr);
		Connection loader(database), writer(database), reader(database);
		Query(reader, "SET threads=" + std::to_string(threads));
		if (joined)
			Query(writer,
			      "CREATE TABLE dd_b_f(shipment_id VARCHAR,batch_id VARCHAR,ready BOOLEAN,expires DATE);"
			      "CREATE TABLE dd_b_d(batch_id VARCHAR,expires DATE,label VARCHAR);"
			      "INSERT INTO dd_b_f VALUES('d1','1',true,DATE "
			      "'1900-01-01'),('d2','01',true,NULL),('d3','n'||chr(0)||'x',false,NULL),('d4','n',false,NULL);"
			      "INSERT INTO dd_b_d VALUES('1',DATE '2026-02-01','one'),('01',DATE "
			      "'2025-06-01','zero-one'),('n'||chr(0)||'x',DATE '2026-03-01','nul'),('n',DATE "
			      "'2025-03-01','prefix'),('unused',DATE '2024-01-01','unused')");
		else
			Query(writer, "CREATE TABLE dd_a_f(asset_id VARCHAR,capacity INTEGER,active BOOLEAN);INSERT INTO dd_a_f "
			              "VALUES('d1',12,true),('d2',2,true),('d3',20,false),('d4',0,false)");
		auto manifest = root + "test/fixtures/direct-derived-" + (joined ? "joined/" : "single/") + "bundle.json";
		Read(manifest);
		auto load = "CALL ossie_bundle_load(" + Literal(manifest) + ")";
		auto parent = joined ? string("Shipment") : string("Device"),
		     first = joined ? string("ReadyShipment") : string("ActiveDevice"),
		     second = joined ? string("CurrentShipment") : string("LargeDevice");
		auto snapshot = "SELECT snapshot_id FROM ossie_population_capabilities()";
		auto members = [](const string &name) {
			return "SELECT string_agg(entity_id,',' ORDER BY entity_id) FROM ossie_objects(" + Literal(name) + ",[])";
		};
		Query(loader, load);
		auto original = Scalar(reader, snapshot);
		Check(Scalar(reader, members(parent)) == "d1,d2,d3,d4",
		      "multiple children leave complete root population unchanged");
		Check(Scalar(reader, members(first)) == "d1,d2" && Scalar(reader, members(second)) == "d1,d3",
		      "multiple child scopes overlap but have distinct complete members");
		Check(Scalar(reader, "SELECT count(*) FROM ossie_concepts()") == "3", "all direct concepts discovered");
		Check(Scalar(reader, "SELECT api_version FROM ossie_population_capabilities()") == "2.5.0",
		      "direct capability selects API2.5");
		vector<pair<unique_ptr<PreparedStatement>, string>> held;
		for (const auto &name : {parent, first, second}) {
			for (const auto &sql :
			     {members(name), "SELECT plan_json FROM ossie_concept_compile(" + Literal(name) + ",[" +
			                         Literal(joined ? "shipment_count" : "device_count") + "],[],[])"}) {
				auto statement = reader.Prepare(sql);
				Check(!statement->HasError(), "prepare each direct concept and its exact plan");
				held.emplace_back(std::move(statement), Scalar(reader, sql));
			}
		}
		auto discovered = "SELECT string_agg(concept||':'||COALESCE(parent,'-')||':'||COALESCE(derived_by,'-'),';' "
		                  "ORDER BY concept) FROM ossie_concepts()";
		held.emplace_back(reader.Prepare(discovered), Scalar(reader, discovered));
		Check(!held.back().first->HasError(), "prepare complete concept table");
		auto refusal = [&](const string &sql, const string &owner) {
			Query(writer, sql);
			auto result = loader.Query(load);
			Check(result->HasError() && result->GetError().find("CONSTRAINT_VIOLATION") != string::npos &&
			          result->GetError().find("object=" + owner + " stage=constraint") != string::npos,
			      "constraint failure identifies exact direct owner");
			Check(Scalar(reader, snapshot) == original, "direct violation does not publish any new scope");
		};
		refusal(joined ? "UPDATE dd_b_d SET expires=NULL WHERE batch_id='01'"
		               : "UPDATE dd_a_f SET capacity=0 WHERE asset_id='d2'",
		        first);
		Query(writer, joined ? "UPDATE dd_b_d SET expires=DATE '2025-06-01' WHERE batch_id='01'"
		                     : "UPDATE dd_a_f SET capacity=2 WHERE asset_id='d2'");
		refusal(joined ? "UPDATE dd_b_d SET expires=DATE '2026-01-15' WHERE batch_id='n'||chr(0)||'x'"
		               : "UPDATE dd_a_f SET capacity=10 WHERE asset_id='d3'",
		        second);
		Query(writer, joined ? "UPDATE dd_b_d SET expires=DATE '2026-03-01' WHERE batch_id='n'||chr(0)||'x'"
		                     : "UPDATE dd_a_f SET capacity=20 WHERE asset_id='d3'");
		refusal(joined ? "UPDATE dd_b_d SET expires=DATE '2024-01-01' WHERE batch_id='n'"
		               : "UPDATE dd_a_f SET capacity=-1 WHERE asset_id='d4'",
		        parent);
		Query(writer, joined ? "UPDATE dd_b_d SET expires=DATE '2025-03-01' WHERE batch_id='n'"
		                     : "UPDATE dd_a_f SET capacity=0 WHERE asset_id='d4'");
		{
			Attempt attempt(loader, load, PopulationLoadCheckpoint::BEFORE_PUBLISH);
			attempt.gate.Wait();
			loader.Interrupt();
			attempt.Finish();
			Check(attempt.result && attempt.result->HasError() &&
			          attempt.result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
			      "direct scopes preserve native prepublish INTERRUPT");
			Check(Scalar(reader, snapshot) == original, "direct cancellation retains old complete topology");
		}
		Query(writer, joined ? "UPDATE dd_b_f SET ready=false;UPDATE dd_b_d SET expires=DATE '2025-01-01'"
		                     : "UPDATE dd_a_f SET active=false,capacity=0");
		Query(loader, load);
		Check(Scalar(reader, "SELECT count(*) FROM ossie_objects(" + Literal(first) + ",[])") == "0" &&
		          Scalar(reader, "SELECT count(*) FROM ossie_objects(" + Literal(second) + ",[])") == "0",
		      "all direct children may be empty independently of root");
		Query(writer, joined ? "DROP TABLE dd_b_f;DROP TABLE dd_b_d" : "DROP TABLE dd_a_f");
		for (auto &item : held) {
			auto result = item.first->Execute();
			Check(!result->HasError(), "old direct prepared remains executable after reload/drop");
			auto chunk = result->Fetch();
			Check(chunk && chunk->size() == 1 && chunk->ColumnCount() == 1 &&
			          chunk->GetValue(0, 0).ToString() == item.second,
			      "old direct full members/plan/topology exactly retained");
			Check(!result->Fetch() && !result->HasError(), "old direct result exhausted");
		}
	}
}

void SingleInheritanceControls(const string &root, int threads) {
	for (bool joined : {false, true}) {
		DuckDB database(nullptr);
		Connection loader(database), writer(database), reader(database);
		Query(reader, "SET threads=" + std::to_string(threads));
		if (joined)
			Query(
			    writer,
			    "CREATE TABLE si_b_f(shipment_id VARCHAR,batch_id VARCHAR,ready BOOLEAN,expires DATE,express BOOLEAN);"
			    "CREATE TABLE si_b_d(batch_id VARCHAR,expires DATE,label VARCHAR);"
			    "INSERT INTO si_b_f VALUES('b1','1',true,DATE "
			    "'1900-01-01',true),('b2','01',true,NULL,true),('b3','n'||chr(0)||'x',false,NULL,true),('b4','1',true,"
			    "NULL,false),('b5','other',true,NULL,false);"
			    "INSERT INTO si_b_d VALUES('1',DATE '2026-02-01','one'),('01',DATE "
			    "'2025-06-01','zero-one'),('n'||chr(0)||'x',DATE '2026-03-01','nul'),('other',DATE "
			    "'2026-03-01','other'),('unused',DATE '2024-01-01','unused')");
		else
			Query(writer, "CREATE TABLE si_a_f(asset_id VARCHAR,capacity INTEGER,active BOOLEAN,priority BOOLEAN);"
			              "INSERT INTO si_a_f "
			              "VALUES('a1',20,true,true),('a2',5,true,true),('a3',30,false,true),('a4',20,true,false)");
		auto manifest = root + "test/fixtures/single-inheritance-" + (joined ? "joined/" : "single/") + "bundle.json";
		auto load = "CALL ossie_bundle_load(" + Literal(manifest) + ")";
		auto base = joined ? string("Shipment") : string("Device"),
		     parent = joined ? string("ReadyShipment") : string("ActiveDevice"),
		     middle = joined ? string("CurrentReadyShipment") : string("LargeActiveDevice"),
		     leaf = joined ? string("ExpressCurrentShipment") : string("PriorityDevice");
		auto snapshot = "SELECT snapshot_id FROM ossie_population_capabilities()";
		auto members = [](const string &name) {
			return "SELECT string_agg(entity_id,',' ORDER BY entity_id) FROM ossie_objects(" + Literal(name) + ",[])";
		};
		Query(loader, load);
		auto original = Scalar(reader, snapshot);
		Check(Scalar(reader, members(leaf)) == (joined ? "b1" : "a1"), "lineage requires every ancestor TRUE");
		Check(Scalar(reader, members(middle)) == (joined ? "b1,b4,b5" : "a1,a4"),
		      "intermediate full population includes nonleaf members");
		vector<pair<unique_ptr<PreparedStatement>, string>> held;
		for (const auto &name : {base, parent, middle, leaf})
			for (const auto &sql :
			     {members(name), "SELECT plan_json FROM ossie_concept_compile(" + Literal(name) + ",[" +
			                         Literal(joined ? "shipment_count" : "device_count") + "],[],[])"}) {
				auto statement = reader.Prepare(sql);
				Check(!statement->HasError(), "prepare full lineage objects and plan");
				held.emplace_back(std::move(statement), Scalar(reader, sql));
			}
		for (const auto &sql :
		     {string("SELECT capabilities_json FROM ossie_population_capabilities()"),
		      string(
		          "SELECT string_agg(concept||':'||COALESCE(parent,'-'),';' ORDER BY concept) FROM ossie_concepts()")})
			held.emplace_back(reader.Prepare(sql), Scalar(reader, sql));
		Query(writer, joined ? "UPDATE si_b_d SET expires=DATE '2026-01-10' WHERE batch_id='other'"
		                     : "UPDATE si_a_f SET capacity=12 WHERE asset_id='a4'");
		auto rejected = loader.Query(load);
		Check(rejected->HasError() &&
		          rejected->GetError().find("object=" + middle + " stage=constraint") != string::npos,
		      "intermediate violation outside leaf rejects complete load");
		Check(Scalar(reader, snapshot) == original, "failed hierarchy load preserves old snapshot");
		Query(writer, joined ? "UPDATE si_b_d SET expires=DATE '2026-03-01' WHERE batch_id='other'"
		                     : "UPDATE si_a_f SET capacity=20 WHERE asset_id='a4'");
		{
			Attempt attempt(loader, load, PopulationLoadCheckpoint::BEFORE_PUBLISH);
			attempt.gate.Wait();
			loader.Interrupt();
			attempt.Finish();
			Check(attempt.result && attempt.result->HasError() &&
			          attempt.result->GetErrorObject().Type() == ExceptionType::INTERRUPT,
			      "hierarchy cancellation preserves native INTERRUPT");
			Check(Scalar(reader, snapshot) == original, "hierarchy cancellation preserves old complete tree");
		}
		Query(writer, joined ? "UPDATE si_b_f SET ready=false" : "UPDATE si_a_f SET active=false");
		Query(loader, load);
		Check(Scalar(reader, "SELECT count(*) FROM ossie_objects(" + Literal(leaf) + ",[])") == "0",
		      "empty ancestor empties every descendant");
		Query(writer, joined ? "DROP TABLE si_b_f;DROP TABLE si_b_d" : "DROP TABLE si_a_f");
		for (auto &item : held) {
			auto result = item.first->Execute();
			Check(!result->HasError(), "old hierarchy prepared executes after reload/drop");
			auto chunk = result->Fetch();
			Check(chunk && chunk->size() == 1 && chunk->GetValue(0, 0).ToString() == item.second,
			      "old hierarchy members/plans/discovery/capabilities exact");
			Check(!result->Fetch() && !result->HasError(), "old hierarchy prepared fully exhausted");
		}
	}
}

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
		RelationBindControls(root, 4);
		RelationBindControls(root, 1);
		HistoryControls(root, 4);
		HistoryControls(root, 1);
		ConceptRequiresControls(root, 4);
		ConceptRequiresControls(root, 1);
		StringJoinControls(root, 4);
		StringJoinControls(root, 1);
		DirectDerivedControls(root, 4);
		DirectDerivedControls(root, 1);
		SingleInheritanceControls(root, 4);
		SingleInheritanceControls(root, 1);
		std::cout
		    << "PASS: " << checks
		    << " checks; deterministic production capture, capture/prepublish cancel, callback failure; threads=4/1\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << "\n";
		return 1;
	}
}
