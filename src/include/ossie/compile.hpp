#pragma once

#include "duckdb/common/enums/join_type.hpp"
#include "ossie/model.hpp"

namespace duckdb {
class SelectStatement;
class TableRef;

namespace ossie {

//! A bare dimension name that resolves to a different field on each dataset, so grains that share
//! no relationship can still be grouped and joined on the same key.
struct SharedKey {
	string name;
	//! dataset name -> field name on that dataset
	case_insensitive_map_t<string> fields;
};

struct CompileOptions {
	//! Whether filters may call named functions. Operators are always allowed, subqueries never.
	bool allow_filter_functions = false;
	//! Join dimensions with LEFT JOIN instead of INNER, so a grain row whose foreign key is NULL or
	//! dangling is kept, with NULL dimension values, rather than dropped.
	bool left_joins = false;
	//! Internal, already resolved row predicate. Never reparsed or field-expanded.
	const ParsedExpression *population_predicate = nullptr;
	//! Internal typed batch sources carrying their lifetime dependency, keyed by dataset name. A
	//! dataset absent here binds to its declared source.
	case_insensitive_map_t<TableRef *> population_sources;
	vector<SharedKey> shared_keys;
	//! Datasets that get a grain subquery even when no requested metric aggregates over them, so
	//! their row presence takes part in the grain join.
	vector<string> required_grains;
	//! How the per-grain subqueries of a multi-grain request are joined on their dimension values.
	JoinType grain_join = JoinType::OUTER;
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
