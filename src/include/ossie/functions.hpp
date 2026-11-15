#pragma once

#include "duckdb.hpp"

namespace duckdb {
class ExtensionLoader;

namespace ossie {

//! Reads a VARCHAR[] argument, rejecting NULL entries.
vector<string> ToStringList(const Value &value, const char *function_name);

//! Registers ossie_load(path, rebind => MAP).
void RegisterLoadFunction(ExtensionLoader &loader);

//! Registers the model description table functions.
void RegisterDescribeFunctions(ExtensionLoader &loader);

//! Registers ossie_compile(metrics, dimensions, filters).
void RegisterCompileFunction(ExtensionLoader &loader);

//! Registers ossie_query(metrics, dimensions, filters).
void RegisterQueryFunction(ExtensionLoader &loader);

//! Registers ossie_date_trunc and ossie_dateadd, the DATE-preserving forms the OSSIE_SQL_2026 lowering emits.
void RegisterDateFunctions(ExtensionLoader &loader);

//! Fixed single-table ontology population profile and private observation batches.
void RegisterPopulationFunctions(ExtensionLoader &loader);

} // namespace ossie
} // namespace duckdb
