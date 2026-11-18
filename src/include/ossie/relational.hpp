#pragma once

#include "duckdb.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "ossie/model.hpp"

namespace duckdb {
namespace ossie {

//! Compiles one SELECT written against the model's relations -- `model.dataset` in FROM, AGG(measure) to
//! evaluate a measure -- into a statement over the datasets' sources. Everything else in the query is left
//! as written for DuckDB.
unique_ptr<SelectStatement> CompileRelational(ClientContext &context, const Model &model, const string &sql);

} // namespace ossie
} // namespace duckdb
