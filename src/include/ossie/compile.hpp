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
	//! Internal. Whether an aggregate may read, beside the dataset whose rows it runs over, datasets those rows
	//! determine. A population bundle designates its grain and checks each metric against it; a query against
	//! a model alone has only the aggregate's arguments to go by, and those must read one dataset.
	bool determined_arguments = false;
};

//! Whether `name` is an aggregate function this compiler recognises. Not exhaustive: anything missed
//! reaches DuckDB as a plain function and is rejected there with DuckDB's own message.
bool IsAggregateName(const string &name);

//! Requires each metric's recognized original-model aggregate grain to be the designated dataset.
//! An aggregate that reads no dataset has none. Needs separate original/expanded expression
//! validation, Compile/binding and data-key checks; the aggregate-name set is not exhaustive.
void RequireMetricGrain(const Model &model, const vector<string> &metrics, const string &designated_dataset);

//! What a caller asks for. `filters` are conditions on rows, applied before aggregation; `metric_filters` are
//! conditions on metrics, applied after.
struct Request {
	vector<string> metrics;
	vector<string> dimensions;
	vector<string> filters;
	vector<string> metric_filters;
	//! Output names by position. An entry that is missing or empty leaves its column named by the final part
	//! of the reference.
	vector<string> metric_aliases;
	vector<string> dimension_aliases;
};

//! Reads a query in the Wide Table specification's JSON form: an object with `metrics`, `dimensions` and
//! `filter`, where an entry is a reference string or an object that adds an alias. Anything else in it is
//! refused with E_INVALID_QUERY rather than ignored.
Request ParseRequest(const string &json);

//! Compiles a semantic request, throwing when it cannot answer.
unique_ptr<SelectStatement> Compile(const Model &model, const Request &request, const CompileOptions &options);

//! The same, for a request without aliases.
unique_ptr<SelectStatement> Compile(const Model &model, const vector<string> &metrics, const vector<string> &dimensions,
                                    const vector<string> &filters, const vector<string> &metric_filters,
                                    const CompileOptions &options);

//! The same query rendered as text. ossie_query executes the statement instead, so the SQL shown
//! here and the SQL run cannot drift apart.
string CompileToSQL(const Model &model, const vector<string> &metrics, const vector<string> &dimensions,
                    const vector<string> &filters, const vector<string> &metric_filters, const CompileOptions &options);

} // namespace ossie
} // namespace duckdb
