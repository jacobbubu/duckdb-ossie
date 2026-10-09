#pragma once

#include "ossie/model.hpp"

namespace duckdb {
class SelectStatement;
class TableRef;

namespace ossie {

struct CompileOptions {
	//! Whether filters may call named functions. Operators are always allowed, subqueries never.
	bool allow_filter_functions = false;
	//! Internal, already resolved row predicate. Never reparsed or field-expanded.
	const ParsedExpression *population_predicate = nullptr;
	//! Internal typed batch source carrying its lifetime dependency.
	TableRef *population_source = nullptr;
};

//! Requires each metric's recognized original-model aggregate grain to be the designated dataset.
//! Does not infer recognized grainless aggregates. Needs separate original/expanded expression
//! validation, Compile/binding and data-key checks; the aggregate-name set is not exhaustive.
void RequireMetricGrain(const Model &model, const vector<string> &metrics, const string &designated_dataset);

//! Compiles a semantic request, throwing when it cannot answer.
unique_ptr<SelectStatement> Compile(const Model &model, const vector<string> &metrics, const vector<string> &dimensions,
                                    const vector<string> &filters, const CompileOptions &options);

//! The same query rendered as text. ossie_query executes the statement instead, so the SQL shown
//! here and the SQL run cannot drift apart.
string CompileToSQL(const Model &model, const vector<string> &metrics, const vector<string> &dimensions,
                    const vector<string> &filters, const CompileOptions &options);

} // namespace ossie
} // namespace duckdb
