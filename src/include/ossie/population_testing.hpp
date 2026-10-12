#pragma once

#include "duckdb/main/client_context_state.hpp"
#include "duckdb/parser/sql_statement.hpp"
#include <functional>

namespace duckdb {
namespace ossie {

// Native-only probes of the production fact loader. They cannot supply a source
// TableRef or alter published state and are never registered as SQL functions.
unique_ptr<SQLStatement> PopulationFactStatementForTesting(ClientContext &context, bool window);
vector<std::function<bool()>> PopulationFactLifetimeForTesting(ClientContext &context);

// Internal native-test seam. No SQL/environment switch and no mutable population access.
// Install before starting a query; remove only after its execution thread has joined.
// Callbacks must not reenter the caller's Connection or ClientContext.
enum class PopulationLoadCheckpoint : uint8_t { AFTER_FACT_CAPTURE, BEFORE_PUBLISH, AFTER_HISTORY_OBSERVATION_CAPTURE };
class PopulationLoadObserver : public ClientContextState {
public:
	static constexpr const char *KEY = "ossie_population_load_observer";
	virtual void Observe(PopulationLoadCheckpoint checkpoint) = 0;
};

enum class PopulationFactCheckpoint : uint8_t { BEFORE_EXECUTE, AFTER_DOMAIN, BEFORE_SORT, BEFORE_ENCODE };
class PopulationFactObserver : public ClientContextState {
public:
	static constexpr const char *KEY = "ossie_population_fact_observer";
	virtual void Observe(PopulationFactCheckpoint checkpoint) = 0;
};

enum class PopulationDailyCheckpoint : uint8_t { BEFORE_EXECUTE, BEFORE_ENCODE };
class PopulationDailyObserver : public ClientContextState {
public:
	static constexpr const char *KEY = "ossie_population_daily_observer";
	virtual void Observe(PopulationDailyCheckpoint checkpoint) = 0;
};

enum class PopulationDailyStateCheckpoint : uint8_t { BEFORE_EXECUTE, AFTER_DOMAIN, BEFORE_EXPAND, BEFORE_FINISH };
class PopulationDailyStateObserver : public ClientContextState {
public:
	static constexpr const char *KEY = "ossie_population_daily_state_observer";
	virtual void Observe(PopulationDailyStateCheckpoint checkpoint) = 0;
};

enum class PopulationRelationCheckpoint : uint8_t { BEFORE_EXECUTE, BEFORE_SORT, BEFORE_ENCODE };
class PopulationRelationObserver : public ClientContextState {
public:
	static constexpr const char *KEY = "ossie_population_relation_observer";
	virtual void Observe(PopulationRelationCheckpoint checkpoint) = 0;
};

class PopulationLegacyBindObserver : public ClientContextState {
public:
	static constexpr const char *KEY = "ossie_population_legacy_bind_observer";
	virtual void Observe() = 0;
};

} // namespace ossie
} // namespace duckdb
