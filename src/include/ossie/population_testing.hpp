#pragma once

#include "duckdb/main/client_context_state.hpp"

namespace duckdb {
namespace ossie {

// Internal native-test seam. No SQL/environment switch and no mutable population access.
// Install before starting a query; remove only after its execution thread has joined.
// Callbacks must not reenter the caller's Connection or ClientContext.
enum class PopulationLoadCheckpoint : uint8_t { AFTER_FACT_CAPTURE, BEFORE_PUBLISH };
class PopulationLoadObserver : public ClientContextState {
public:
	static constexpr const char *KEY = "ossie_population_load_observer";
	virtual void Observe(PopulationLoadCheckpoint checkpoint) = 0;
};

enum class PopulationDailyCheckpoint : uint8_t { BEFORE_EXECUTE, BEFORE_ENCODE };
class PopulationDailyObserver : public ClientContextState {
public:
	static constexpr const char *KEY = "ossie_population_daily_observer";
	virtual void Observe(PopulationDailyCheckpoint checkpoint) = 0;
};

} // namespace ossie
} // namespace duckdb
