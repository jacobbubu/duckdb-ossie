#include "ossie/compile.hpp"

#include "duckdb/common/error_data.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/conjunction_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
#include "duckdb/parser/expression/window_expression.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
#include "duckdb/parser/tableref/joinref.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "ossie/catalog.hpp"
#include "ossie/dialect.hpp"

#include <algorithm>

namespace duckdb {
namespace ossie {

namespace {

//! Aliasing each dataset to its model name keeps emitted SQL readable, but assumes a dataset
//! appears at most once per query. Joining one twice would need aliases keyed by join path.
const string &AliasFor(const Dataset &dataset) {
	return dataset.name;
}

//! Points the column refs inside an inlined field expression at the bound alias.
void QualifyColumns(unique_ptr<ParsedExpression> &expr, const string &alias) {
	if (expr->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto &colref = expr->Cast<ColumnRefExpression>();
		expr = make_uniq<ColumnRefExpression>(colref.column_names.back(), alias);
		return;
	}
	ParsedExpressionIterator::EnumerateChildren(
	    *expr, [&](unique_ptr<ParsedExpression> &child) { QualifyColumns(child, alias); });
}

//! Replaces `dataset.field` with the field's expression. Field names are model-level and need not
//! exist on the table: `customer_full_name` is really `c_first_name || ' ' || c_last_name`.
void InlineFields(const Model &model, unique_ptr<ParsedExpression> &expr) {
	if (expr->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto &colref = expr->Cast<ColumnRefExpression>();
		if (colref.column_names.size() == 2) {
			auto dataset = model.FindDataset(colref.column_names[0]);
			if (!dataset) {
				// Not a model dataset -- already a physical reference, leave it alone.
				return;
			}
			auto field = dataset->FindField(colref.column_names[1]);
			if (!field) {
				// The Ossie schema does not require that every column a model expression names be
				// declared as a field, and real models rely on that. Such a name is a physical
				// column: qualify it to the bound alias and let DuckDB's binder resolve it.
				expr = make_uniq<ColumnRefExpression>(colref.column_names[1], AliasFor(*dataset));
				return;
			}
			auto replacement = field->expression.tree->Copy();
			QualifyColumns(replacement, AliasFor(*dataset));
			expr = std::move(replacement);
			return;
		}
	}
	ParsedExpressionIterator::EnumerateChildren(
	    *expr, [&](unique_ptr<ParsedExpression> &child) { InlineFields(model, child); });
}

void CollectDatasets(const ParsedExpression &expr, case_insensitive_set_t &datasets) {
	if (expr.GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto &colref = expr.Cast<ColumnRefExpression>();
		if (colref.column_names.size() == 2) {
			datasets.insert(colref.column_names[0]);
		}
	}
	ParsedExpressionIterator::EnumerateChildren(
	    expr, [&](const ParsedExpression &child) { CollectDatasets(child, datasets); });
}

//! case_insensitive_set_t is an unordered_set, so its iteration order is implementation-defined and
//! differs between standard libraries. Anything that reaches emitted SQL or an error message has to
//! be ordered explicitly, or the same model and request produce different output on different
//! platforms -- which is exactly how this was found: golden SQL and a refusal message both differed
//! on MSVC while passing on libc++.
vector<string> SortedNames(const case_insensitive_set_t &names) {
	vector<string> result(names.begin(), names.end());
	// Compare case-insensitively so ordering matches how names are looked up, with the raw string as
	// a tiebreak so the result is a total order even if two names differ only in case.
	std::sort(result.begin(), result.end(), [](const string &a, const string &b) {
		auto la = StringUtil::Lower(a);
		auto lb = StringUtil::Lower(b);
		return la != lb ? la < lb : a < b;
	});
	return result;
}

//! Appends `name` unless it is the root or already present. Keeps insertion order, which is what
//! makes the emitted join order stable and intuitive: metric datasets, then dimensions in the order
//! requested, then filters.
void AddRequired(vector<string> &required, case_insensitive_set_t &seen, const string &name, const string &root) {
	if (StringUtil::CIEquals(name, root) || !seen.insert(name).second) {
		return;
	}
	required.push_back(name);
}

vector<string> MetricNames(const Model &model) {
	vector<string> names;
	for (auto &metric : model.metrics) {
		names.push_back(metric.name);
	}
	return names;
}

vector<string> DimensionNames(const Model &model) {
	vector<string> names;
	for (auto &dataset : model.datasets) {
		for (auto &field : dataset.fields) {
			names.push_back(dataset.name + "." + field.name);
		}
	}
	return names;
}

unique_ptr<TableRef> BindTable(const Dataset &dataset) {
	auto table_ref = make_uniq<BaseTableRef>();
	SplitSource(dataset.source_bound, table_ref->catalog_name, table_ref->schema_name, table_ref->table_name);
	table_ref->alias = AliasFor(dataset);
	return std::move(table_ref);
}

//! A dimension request e.g. "item.i_brand".
struct Dimension {
	const Dataset *dataset;
	unique_ptr<ParsedExpression> expr;
	string alias;
};

Dimension ResolveDimension(const Model &model, const string &request) {
	vector<unique_ptr<ParsedExpression>> parsed;
	try {
		parsed = Parser::ParseExpressionList(request);
	} catch (const ParserException &) {
		throw InvalidInputException("ossie: dimension \"%s\" is not a valid name", request);
	}
	if (parsed.size() != 1 || parsed[0]->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		throw InvalidInputException("ossie: dimension \"%s\" must be a dataset.field name", request);
	}
	auto &colref = parsed[0]->Cast<ColumnRefExpression>();
	if (colref.column_names.size() != 2) {
		throw InvalidInputException("ossie: dimension \"%s\" must be qualified as dataset.field", request);
	}

	auto dataset = model.FindDataset(colref.column_names[0]);
	if (!dataset) {
		throw InvalidInputException("ossie: dimension \"%s\" names dataset \"%s\", which the model does not "
		                            "declare",
		                            request, colref.column_names[0]);
	}
	if (!dataset->FindField(colref.column_names[1])) {
		throw InvalidInputException("ossie: dataset \"%s\" has no field \"%s\".%s", dataset->name,
		                            colref.column_names[1],
		                            StringUtil::CandidatesErrorMessage(DimensionNames(model), request, "Did you mean"));
	}

	Dimension result;
	result.dataset = dataset;
	result.expr = std::move(parsed[0]);
	InlineFields(model, result.expr);
	result.alias = request;
	return result;
}

//! Whether joining across this edge repeats rows of the side we started from. Travelling toward the
//! unique side matches at most one row; travelling away from it matches many.
bool FansOut(const Relationship &relationship, bool moving_to_target) {
	switch (relationship.cardinality) {
	case Cardinality::ONE_TO_ONE:
		return false;
	case Cardinality::MANY_TO_ONE:
		return !moving_to_target;
	case Cardinality::ONE_TO_MANY:
		return moving_to_target;
	default:
		return true;
	}
}

//! Datasets joinable to `root` by cardinality-preserving edges, root included. The property
//! composes: if no hop repeats rows, neither does the whole path.
case_insensitive_set_t SafelyReachable(const Model &model, const string &root) {
	case_insensitive_set_t visited {root};
	vector<string> queue {root};
	for (idx_t head = 0; head < queue.size(); head++) {
		for (auto &relationship : model.relationships) {
			string other;
			bool moving_to_target;
			if (StringUtil::CIEquals(relationship.from_dataset, queue[head])) {
				other = relationship.to_dataset;
				moving_to_target = true;
			} else if (StringUtil::CIEquals(relationship.to_dataset, queue[head])) {
				other = relationship.from_dataset;
				moving_to_target = false;
			} else {
				continue;
			}
			if (FansOut(relationship, moving_to_target)) {
				continue;
			}
			if (visited.insert(other).second) {
				queue.push_back(other);
			}
		}
	}
	return visited;
}

//! Named aggregates recognised in a filter, which routes the predicate to HAVING. Not exhaustive:
//! anything missed lands in WHERE and DuckDB rejects it with its own message.
bool IsAggregateName(const string &name) {
	static const case_insensitive_set_t AGGREGATES {
	    "sum",     "count",    "avg",        "min",      "max",        "stddev",  "median",
	    "mode",    "variance", "string_agg", "list",     "first",      "last",    "bool_and",
	    "bool_or", "arg_max",  "arg_min",    "quantile", "count_star", "product", "histogram"};
	return AGGREGATES.find(name) != AGGREGATES.end();
}

//! Filters come from the caller, so they are allowlisted: operators always, named calls on request,
//! subqueries never.
void CheckFilterNode(const ParsedExpression &expr, const string &request, const CompileOptions &options) {
	switch (expr.GetExpressionClass()) {
	case ExpressionClass::COLUMN_REF:
	case ExpressionClass::CONSTANT:
	case ExpressionClass::COMPARISON:
	case ExpressionClass::CONJUNCTION:
	case ExpressionClass::OPERATOR:
	case ExpressionClass::BETWEEN:
	case ExpressionClass::CASE:
	case ExpressionClass::CAST:
		return;
	case ExpressionClass::FUNCTION: {
		auto &function = expr.Cast<FunctionExpression>();
		// Arithmetic, || and LIKE all arrive here with is_operator set.
		if (function.is_operator || IsAggregateName(function.function_name) || options.allow_filter_functions) {
			return;
		}
		throw InvalidInputException("ossie: filter \"%s\" calls function \"%s\". Load the model with "
		                            "allow_filter_functions => true to permit function calls in filters",
		                            request, function.function_name);
	}
	case ExpressionClass::SUBQUERY:
		throw InvalidInputException("ossie: filter \"%s\" contains a subquery, which could read tables the "
		                            "model does not declare",
		                            request);
	default:
		throw InvalidInputException("ossie: filter \"%s\" uses an expression that is not allowed in a filter", request);
	}
}

//! An aggregate's grain is the dataset that functionally determines the others it touches: their
//! values are replicated onto its rows, so each of its rows is still counted exactly once.
string AggregateGrain(const Model &model, const case_insensitive_set_t &datasets, const string &metric_name) {
	vector<string> candidates;
	for (auto &candidate : datasets) {
		auto reachable = SafelyReachable(model, candidate);
		bool covers_all = true;
		for (auto &other : datasets) {
			if (reachable.find(other) == reachable.end()) {
				covers_all = false;
				break;
			}
		}
		if (covers_all) {
			candidates.push_back(candidate);
		}
	}
	if (candidates.size() != 1) {
		auto names = SortedNames(datasets);
		throw InvalidInputException("ossie: an aggregate in metric \"%s\" spans datasets %s, and none of them "
		                            "is the grain the others hang off, so there is no single set of rows to aggregate",
		                            metric_name, StringUtil::Join(names, ", "));
	}
	return candidates[0];
}

//! The first window function in `expr`, or nullptr. A window's frame and partition are written into the
//! metric, but the rows it runs over are decided by the request's dimensions, which the model cannot know.
const WindowExpression *FindWindow(const ParsedExpression &expr) {
	if (expr.GetExpressionClass() == ExpressionClass::WINDOW) {
		return &expr.Cast<WindowExpression>();
	}
	const WindowExpression *found = nullptr;
	ParsedExpressionIterator::EnumerateChildren(expr, [&](const ParsedExpression &child) {
		if (!found) {
			found = FindWindow(child);
		}
	});
	return found;
}

//! Collects the grain of every aggregate in a metric. An aggregate with no column reference, such
//! as COUNT(*), names no dataset and is recorded separately.
void CollectGrains(const Model &model, const ParsedExpression &expr, const string &metric_name,
                   case_insensitive_set_t &grains, bool &has_grainless) {
	if (expr.GetExpressionClass() == ExpressionClass::FUNCTION &&
	    IsAggregateName(expr.Cast<FunctionExpression>().function_name)) {
		case_insensitive_set_t datasets;
		CollectDatasets(expr, datasets);
		if (datasets.empty()) {
			has_grainless = true;
		} else {
			grains.insert(AggregateGrain(model, datasets, metric_name));
		}
		return;
	}
	ParsedExpressionIterator::EnumerateChildren(
	    expr, [&](const ParsedExpression &child) { CollectGrains(model, child, metric_name, grains, has_grainless); });
}

struct MetricAnalysis {
	const Metric *metric = nullptr;
	case_insensitive_set_t grains;
	bool grainless = false;
};

MetricAnalysis AnalyzeMetric(const Model &model, const string &metric_name) {
	auto metric = model.FindMetric(metric_name);
	if (!metric) {
		throw InvalidInputException(
		    "ossie: model \"%s\" has no metric named \"%s\".%s", model.name, metric_name,
		    StringUtil::CandidatesErrorMessage(MetricNames(model), metric_name, "Did you mean"));
	}
	// A metric's window cannot define the partition/order of an arbitrary request.
	auto window = FindWindow(*metric->expression.tree);
	if (window) {
		throw InvalidInputException(
		    "ossie: metric \"%s\" uses the window function %s, whose result changes with the dimensions "
		    "requested in a way the model does not define: the rows it runs over depend on the request, but "
		    "its partition and order are fixed in the metric. Window metrics are not supported yet",
		    metric->name, StringUtil::Upper(window->function_name));
	}
	MetricAnalysis result;
	result.metric = metric;
	CollectGrains(model, *metric->expression.tree, metric->name, result.grains, result.grainless);
	return result;
}

struct Filter {
	unique_ptr<ParsedExpression> expr;
	//! Aggregates cannot appear in WHERE, so such a predicate goes to HAVING whole rather than
	//! being split -- HAVING may also reference grouped columns, so the halves stay valid together.
	bool is_aggregate = false;
};

//! Resolves the names inside a filter: `dataset.field` inlines to the field's expression, and a
//! bare metric name inlines to the metric's, which makes the predicate an aggregate.
void ResolveFilterNames(const Model &model, unique_ptr<ParsedExpression> &expr, const string &request,
                        bool &is_aggregate) {
	if (expr->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto &colref = expr->Cast<ColumnRefExpression>();
		if (colref.column_names.size() == 1) {
			auto metric = model.FindMetric(colref.column_names[0]);
			if (!metric) {
				throw InvalidInputException(
				    "ossie: filter \"%s\" references \"%s\", which is neither a "
				    "metric nor a dataset.field name.%s",
				    request, colref.column_names[0],
				    StringUtil::CandidatesErrorMessage(MetricNames(model), colref.column_names[0], "Did you mean"));
			}
			auto replacement = metric->expression.tree->Copy();
			InlineFields(model, replacement);
			expr = std::move(replacement);
			is_aggregate = true;
			return;
		}
		if (colref.column_names.size() != 2) {
			throw InvalidInputException("ossie: filter \"%s\" references \"%s\"; columns must be qualified "
			                            "as dataset.field",
			                            request, StringUtil::Join(colref.column_names, "."));
		}
		auto dataset = model.FindDataset(colref.column_names[0]);
		if (!dataset) {
			throw InvalidInputException("ossie: filter \"%s\" references dataset \"%s\", which the model does "
			                            "not declare",
			                            request, colref.column_names[0]);
		}
		if (!dataset->FindField(colref.column_names[1])) {
			throw InvalidInputException(
			    "ossie: filter \"%s\" references \"%s.%s\", which the model does not declare.%s", request,
			    colref.column_names[0], colref.column_names[1],
			    StringUtil::CandidatesErrorMessage(
			        DimensionNames(model), colref.column_names[0] + "." + colref.column_names[1], "Did you mean"));
		}
		auto replacement = dataset->FindField(colref.column_names[1])->expression.tree->Copy();
		QualifyColumns(replacement, AliasFor(*dataset));
		expr = std::move(replacement);
		return;
	}

	if (expr->GetExpressionClass() == ExpressionClass::FUNCTION &&
	    IsAggregateName(expr->Cast<FunctionExpression>().function_name)) {
		is_aggregate = true;
	}
	ParsedExpressionIterator::EnumerateChildren(
	    *expr, [&](unique_ptr<ParsedExpression> &child) { ResolveFilterNames(model, child, request, is_aggregate); });
}

void CheckFilterTree(const ParsedExpression &expr, const string &request, const CompileOptions &options) {
	CheckFilterNode(expr, request, options);
	ParsedExpressionIterator::EnumerateChildren(
	    expr, [&](const ParsedExpression &child) { CheckFilterTree(child, request, options); });
}

Filter ResolveFilter(const Model &model, const string &request, const CompileOptions &options) {
	vector<unique_ptr<ParsedExpression>> parsed;
	try {
		parsed = Parser::ParseExpressionList(request);
	} catch (const ParserException &ex) {
		throw InvalidInputException("ossie: filter \"%s\" does not parse: %s", request, ErrorData(ex).RawMessage());
	}
	if (parsed.size() != 1) {
		throw InvalidInputException("ossie: filter \"%s\" must be a single predicate", request);
	}

	// Policy is checked before names are resolved, so the message names what the caller wrote.
	CheckFilterTree(*parsed[0], request, options);

	Filter result;
	result.expr = std::move(parsed[0]);
	// A filter is written in the model's own expression language, so it is lowered the way an OSSIE_SQL_2026
	// field is: NVL and DATEADD(day, ...) work, and CONCAT means what it means in a field. This comes after the
	// policy check, which judges what the caller wrote, and before names are resolved, since the bare date part
	// in DATEADD(day, ...) is not a name.
	LowerOssieSql2026(result.expr, StringUtil::Format("ossie: filter \"%s\"", request));
	ResolveFilterNames(model, result.expr, request, result.is_aggregate);
	return result;
}

struct PlannedJoin {
	const Relationship *relationship;
	const Dataset *dataset;
};

//! Executes BFS through the relationship graph outward from the metric's dataset, returning the joins needed to
//! reach every required dataset. Refuses ambiguous joins (when multiple routes to join two datasets exist).
vector<PlannedJoin> PlanJoins(const Model &model, const Dataset &root, const vector<string> &required) {
	case_insensitive_set_t required_lookup(required.begin(), required.end());
	case_insensitive_map_t<const Relationship *> parent_edge;
	case_insensitive_map_t<string> parent;
	case_insensitive_set_t visited {root.name};
	vector<string> queue {root.name};

	for (idx_t head = 0; head < queue.size(); head++) {
		auto current = queue[head];
		for (auto &relationship : model.relationships) {
			string other;
			if (StringUtil::CIEquals(relationship.from_dataset, current)) {
				other = relationship.to_dataset;
			} else if (StringUtil::CIEquals(relationship.to_dataset, current)) {
				other = relationship.from_dataset;
			} else {
				continue;
			}

			if (visited.find(other) == visited.end()) {
				visited.insert(other);
				parent_edge[other] = &relationship;
				parent[other] = current;
				queue.push_back(other);
				continue;
			}
			// A second edge into an already-reached dataset means two routes exist, and they can
			// produce different numbers. We refuse this ambiguous case.
			auto reached_by = parent_edge.find(other);
			auto came_by = parent_edge.find(current);
			bool is_tree_edge = (reached_by != parent_edge.end() && reached_by->second == &relationship) ||
			                    (came_by != parent_edge.end() && came_by->second == &relationship);
			if (!is_tree_edge && required_lookup.find(other) != required_lookup.end()) {
				throw InvalidInputException("ossie: more than one join path connects \"%s\" to \"%s\"; "
				                            "the model must disambiguate before this can be answered",
				                            root.name, other);
			}
		}
	}

	// Collect the tree edges on the path from each required dataset back to the root.
	vector<PlannedJoin> joins;
	case_insensitive_set_t added;
	for (auto &name : required) {
		if (visited.find(name) == visited.end()) {
			throw InvalidInputException("ossie: no relationship connects \"%s\" to \"%s\"", root.name, name);
		}
		vector<string> path;
		for (auto node = name; !StringUtil::CIEquals(node, root.name); node = parent[node]) {
			path.push_back(node);
		}
		// Root-first, so every join attaches to a dataset already in the tree.
		for (auto entry = path.rbegin(); entry != path.rend(); ++entry) {
			if (added.insert(*entry).second) {
				joins.push_back({parent_edge[*entry], model.FindDataset(*entry)});
			}
		}
	}
	return joins;
}

unique_ptr<ParsedExpression> JoinCondition(const Relationship &relationship) {
	unique_ptr<ParsedExpression> condition;
	for (idx_t i = 0; i < relationship.from_columns.size(); i++) {
		auto left = make_uniq<ColumnRefExpression>(relationship.from_columns[i], relationship.from_dataset);
		auto right = make_uniq<ColumnRefExpression>(relationship.to_columns[i], relationship.to_dataset);
		auto equality =
		    make_uniq<ComparisonExpression>(ExpressionType::COMPARE_EQUAL, std::move(left), std::move(right));
		if (!condition) {
			condition = std::move(equality);
		} else {
			condition = make_uniq<ConjunctionExpression>(ExpressionType::CONJUNCTION_AND, std::move(condition),
			                                             std::move(equality));
		}
	}
	return condition;
}

//! Joins every dataset in `required` onto `root`, refusing any hop that would repeat root rows.
unique_ptr<TableRef> JoinRequired(const Model &model, const Dataset &root, const vector<string> &required,
                                  unique_ptr<TableRef> from) {
	auto planned_joins = PlanJoins(model, root, required);
	for (auto &planned : planned_joins) {
		bool moving_to_target = StringUtil::CIEquals(planned.relationship->to_dataset, planned.dataset->name);
		if (FansOut(*planned.relationship, moving_to_target)) {
			throw InvalidInputException("ossie: joining \"%s\" repeats each \"%s\" row once per match, which "
			                            "would inflate every aggregate. Relationship \"%s\" is %s",
			                            planned.dataset->name, root.name, planned.relationship->name,
			                            CardinalityName(planned.relationship->cardinality));
		}
	}
	for (auto &planned : planned_joins) {
		auto join = make_uniq<JoinRef>(JoinRefType::REGULAR);
		// Ossie does not declare a join type. INNER matches hand-written SQL, but drops fact rows
		// where the foreign key is NULL; an override belongs here when one is needed.
		join->type = JoinType::INNER;
		join->left = std::move(from);
		join->right = BindTable(*planned.dataset);
		join->condition = JoinCondition(*planned.relationship);
		from = std::move(join);
	}
	return from;
}

void AndInto(unique_ptr<ParsedExpression> &target, unique_ptr<ParsedExpression> expr) {
	if (!target) {
		target = std::move(expr);
	} else {
		target = make_uniq<ConjunctionExpression>(ExpressionType::CONJUNCTION_AND, std::move(target), std::move(expr));
	}
}

void GroupByAll(SelectNode &select, idx_t count) {
	if (count == 0) {
		return;
	}
	GroupingSet grouping_set;
	for (idx_t i = 0; i < count; i++) {
		select.groups.group_expressions.push_back(select.select_list[i]->Copy());
		grouping_set.insert(i);
	}
	select.groups.grouping_sets.push_back(std::move(grouping_set));
}

//! One per-grain subquery of a multi-grain request: the aggregates that run over this dataset's rows,
//! and the datasets its dimensions, filters and aggregates need joined in.
struct GrainQuery {
	const Dataset *dataset;
	case_insensitive_set_t reachable;
	vector<string> required;
	case_insensitive_set_t required_seen;
	vector<unique_ptr<ParsedExpression>> aggregates;
	vector<string> aggregate_text;
	unique_ptr<ParsedExpression> where_clause;
};

//! A dimension or filter applies to every grain, so each grain's subquery must be able to join the
//! dataset it names without repeating rows. JoinRequired would refuse anyway, but in terms of the
//! join rather than of the request the caller made.
void RequireReachable(GrainQuery &query, const string &dataset, const string &kind, const string &request,
                      const vector<string> &grains) {
	if (query.reachable.find(dataset) != query.reachable.end()) {
		AddRequired(query.required, query.required_seen, dataset, query.dataset->name);
		return;
	}
	throw InvalidInputException("ossie: %s \"%s\" names dataset \"%s\", which \"%s\" cannot join without repeating "
	                            "its rows, so it cannot apply to the aggregates computed at that grain. The requested "
	                            "metrics aggregate at %s different grains (%s)",
	                            kind, request, dataset, query.dataset->name, to_string(grains.size()),
	                            StringUtil::Join(grains, ", "));
}

//! Replaces each aggregate in `expr` with a reference to the column that its grain's subquery will
//! compute it under, registering the aggregate there. Identical aggregates share one column.
void HoistAggregates(const Model &model, unique_ptr<ParsedExpression> &expr, const string &request,
                     vector<GrainQuery> &grain_queries) {
	if (expr->GetExpressionClass() == ExpressionClass::FUNCTION &&
	    IsAggregateName(expr->Cast<FunctionExpression>().function_name)) {
		case_insensitive_set_t datasets;
		CollectDatasets(*expr, datasets);
		auto grain = AggregateGrain(model, datasets, request);
		for (auto &query : grain_queries) {
			if (!StringUtil::CIEquals(query.dataset->name, grain)) {
				continue;
			}
			auto text = expr->ToString();
			idx_t index = 0;
			for (; index < query.aggregate_text.size(); index++) {
				if (query.aggregate_text[index] == text) {
					break;
				}
			}
			if (index == query.aggregate_text.size()) {
				query.aggregate_text.push_back(text);
				query.aggregates.push_back(std::move(expr));
			}
			for (auto &name : SortedNames(datasets)) {
				AddRequired(query.required, query.required_seen, name, query.dataset->name);
			}
			expr = make_uniq<ColumnRefExpression>("agg_" + to_string(index), query.dataset->name);
			return;
		}
		throw InternalException("ossie: aggregate grain \"%s\" has no subquery", grain);
	}
	ParsedExpressionIterator::EnumerateChildren(
	    *expr, [&](unique_ptr<ParsedExpression> &child) { HoistAggregates(model, child, request, grain_queries); });
}

unique_ptr<ParsedExpression> Coalesce(vector<unique_ptr<ParsedExpression>> children) {
	if (children.size() == 1) {
		return std::move(children[0]);
	}
	return make_uniq<OperatorExpression>(ExpressionType::OPERATOR_COALESCE, std::move(children));
}

//! Metrics whose aggregates sit at several grains: each grain gets its own grouped subquery, the
//! subqueries are joined on the dimension values (NULL-safe, keeping every group either side
//! produced), and the metric expressions are evaluated over the aggregate columns. Each aggregate
//! therefore runs over the complete rows of its own dataset, restricted only by filters that
//! dataset can reach without fanning out.
unique_ptr<SelectStatement> CompileMultiGrain(const Model &model, const vector<string> &grains,
                                              vector<unique_ptr<ParsedExpression>> metric_expressions,
                                              const vector<string> &dimensions, const vector<string> &filters,
                                              const CompileOptions &options) {
	if (options.population_source || options.population_predicate) {
		throw InvalidInputException("ossie: a population request cannot aggregate at more than one grain");
	}
	vector<GrainQuery> grain_queries;
	for (auto &grain : grains) {
		GrainQuery query;
		query.dataset = model.FindDataset(grain);
		query.reachable = SafelyReachable(model, grain);
		grain_queries.push_back(std::move(query));
	}

	for (auto &expr : metric_expressions) {
		auto alias = expr->GetAlias();
		HoistAggregates(model, expr, alias, grain_queries);
		expr->SetAlias(alias);
	}

	vector<Dimension> resolved_dimensions;
	for (auto &request : dimensions) {
		auto dimension = ResolveDimension(model, request);
		for (auto &query : grain_queries) {
			RequireReachable(query, dimension.dataset->name, "dimension", request, grains);
		}
		resolved_dimensions.push_back(std::move(dimension));
	}

	unique_ptr<ParsedExpression> outer_where;
	for (auto &request : filters) {
		auto filter = ResolveFilter(model, request, options);
		if (filter.is_aggregate) {
			HoistAggregates(model, filter.expr, request, grain_queries);
			AndInto(outer_where, std::move(filter.expr));
			continue;
		}
		case_insensitive_set_t referenced;
		CollectDatasets(*filter.expr, referenced);
		for (auto &query : grain_queries) {
			for (auto &name : SortedNames(referenced)) {
				RequireReachable(query, name, "filter", request, grains);
			}
			AndInto(query.where_clause, filter.expr->Copy());
		}
	}

	auto outer = make_uniq<SelectNode>();
	unique_ptr<TableRef> from;
	// The dimension values seen so far, coalesced across the subqueries already joined, so the next
	// subquery can match a group that only one earlier side produced.
	vector<vector<unique_ptr<ParsedExpression>>> dimension_refs(resolved_dimensions.size());
	for (auto &query : grain_queries) {
		auto select = make_uniq<SelectNode>();
		for (idx_t i = 0; i < resolved_dimensions.size(); i++) {
			auto expr = resolved_dimensions[i].expr->Copy();
			expr->SetAlias("dim_" + to_string(i));
			select->select_list.push_back(std::move(expr));
		}
		GroupByAll(*select, resolved_dimensions.size());
		for (idx_t i = 0; i < query.aggregates.size(); i++) {
			auto &expr = query.aggregates[i];
			expr->SetAlias("agg_" + to_string(i));
			select->select_list.push_back(std::move(expr));
		}
		select->where_clause = std::move(query.where_clause);
		select->from_table = JoinRequired(model, *query.dataset, query.required, BindTable(*query.dataset));
		select->aggregate_handling = AggregateHandling::STANDARD_HANDLING;
		auto statement = make_uniq<SelectStatement>();
		statement->node = std::move(select);
		auto subquery = make_uniq<SubqueryRef>(std::move(statement), query.dataset->name);

		if (!from) {
			from = std::move(subquery);
		} else {
			auto join = make_uniq<JoinRef>(resolved_dimensions.empty() ? JoinRefType::CROSS : JoinRefType::REGULAR);
			join->type = JoinType::OUTER;
			join->left = std::move(from);
			for (idx_t i = 0; i < resolved_dimensions.size(); i++) {
				vector<unique_ptr<ParsedExpression>> seen;
				for (auto &ref : dimension_refs[i]) {
					seen.push_back(ref->Copy());
				}
				auto right = make_uniq<ColumnRefExpression>("dim_" + to_string(i), query.dataset->name);
				AndInto(join->condition, make_uniq<ComparisonExpression>(ExpressionType::COMPARE_NOT_DISTINCT_FROM,
				                                                         Coalesce(std::move(seen)), std::move(right)));
			}
			join->right = std::move(subquery);
			from = std::move(join);
		}
		for (idx_t i = 0; i < resolved_dimensions.size(); i++) {
			dimension_refs[i].push_back(make_uniq<ColumnRefExpression>("dim_" + to_string(i), query.dataset->name));
		}
	}

	for (idx_t i = 0; i < resolved_dimensions.size(); i++) {
		auto expr = Coalesce(std::move(dimension_refs[i]));
		expr->SetAlias(resolved_dimensions[i].alias);
		outer->select_list.push_back(std::move(expr));
	}
	for (auto &expr : metric_expressions) {
		outer->select_list.push_back(std::move(expr));
	}
	outer->where_clause = std::move(outer_where);
	outer->from_table = std::move(from);
	auto statement = make_uniq<SelectStatement>();
	statement->node = std::move(outer);
	return statement;
}

} // namespace

void RequireMetricGrain(const Model &model, const vector<string> &metrics, const string &designated_dataset) {
	if (!model.FindDataset(designated_dataset)) {
		throw InvalidInputException("ossie: designated grain dataset \"%s\" is not declared", designated_dataset);
	}
	if (metrics.empty()) {
		throw InvalidInputException("ossie: at least one metric is required for designated grain validation");
	}
	for (auto &name : metrics) {
		auto analysis = AnalyzeMetric(model, name);
		if (analysis.grainless) {
			throw InvalidInputException("ossie: metric \"%s\" contains an aggregate without a dataset; "
			                            "designated grain validation does not infer a grain for it",
			                            name);
		}
		if (analysis.grains.size() != 1 || !StringUtil::CIEquals(*analysis.grains.begin(), designated_dataset)) {
			throw InvalidInputException("ossie: metric \"%s\" must aggregate only at designated grain \"%s\"; "
			                            "inferred grains: [%s]",
			                            name, designated_dataset, StringUtil::Join(SortedNames(analysis.grains), ", "));
		}
	}
}

unique_ptr<SelectStatement> Compile(const Model &model, const vector<string> &metrics, const vector<string> &dimensions,
                                    const vector<string> &filters, const CompileOptions &options) {
	if (metrics.empty()) {
		throw InvalidInputException("ossie: at least one metric is required");
	}

	auto select = make_uniq<SelectNode>();
	if (options.population_predicate) {
		select->where_clause = options.population_predicate->Copy();
	}
	case_insensitive_set_t grains;
	case_insensitive_set_t metric_datasets;
	bool has_grainless = false;
	string grainless_metric;
	vector<unique_ptr<ParsedExpression>> metric_expressions;

	for (auto &metric_name : metrics) {
		auto analysis = AnalyzeMetric(model, metric_name);
		auto metric = analysis.metric;
		for (auto &grain : analysis.grains) {
			grains.insert(grain);
		}
		CollectDatasets(*metric->expression.tree, metric_datasets);
		if (analysis.grainless) {
			has_grainless = true;
			grainless_metric = metric->name;
		}

		auto expr = metric->expression.tree->Copy();
		InlineFields(model, expr);
		expr->SetAlias(metric->name);
		metric_expressions.push_back(std::move(expr));
	}

	// One query has one set of rows to group, so when aggregates sit at several grains each grain gets
	// its own subquery.
	if (grains.size() > 1) {
		if (has_grainless) {
			throw InvalidInputException("ossie: metric \"%s\" has an aggregate with no column reference, and the "
			                            "requested metrics aggregate at %s different grains (%s), so there is no "
			                            "single dataset for it to count. Qualify it, for example "
			                            "COUNT(store_sales.ss_item_sk) rather than COUNT(*)",
			                            grainless_metric, to_string(grains.size()),
			                            StringUtil::Join(SortedNames(grains), ", "));
		}
		return CompileMultiGrain(model, SortedNames(grains), std::move(metric_expressions), dimensions, filters,
		                         options);
	}
	if (grains.empty()) {
		if (has_grainless) {
			throw InvalidInputException("ossie: metric \"%s\" has an aggregate with no column reference, so "
			                            "there is no dataset to aggregate over. Qualify it, for example "
			                            "COUNT(store_sales.ss_item_sk) rather than COUNT(*)",
			                            grainless_metric);
		}
		throw InvalidInputException("ossie: no requested metric contains an aggregate, so the query has no "
		                            "grain to group by");
	}
	// A grainless aggregate is safe once one grain exists: fan-out is refused below, so the joined
	// row count equals the root row count.
	auto &root = *model.FindDataset(*grains.begin());

	// A metric may reference a second dataset as a scalar, which still has to be joined.
	vector<string> required;
	case_insensitive_set_t required_seen;
	for (auto &name : SortedNames(metric_datasets)) {
		AddRequired(required, required_seen, name, root.name);
	}
	for (auto &request : dimensions) {
		auto dimension = ResolveDimension(model, request);
		AddRequired(required, required_seen, dimension.dataset->name, root.name);
		dimension.expr->SetAlias(dimension.alias);
		select->select_list.push_back(std::move(dimension.expr));
	}
	GroupByAll(*select, select->select_list.size());

	for (auto &expr : metric_expressions) {
		select->select_list.push_back(std::move(expr));
	}

	// Filters may reference datasets nothing else does, which pulls in a join.
	for (auto &request : filters) {
		auto filter = ResolveFilter(model, request, options);
		case_insensitive_set_t referenced;
		CollectDatasets(*filter.expr, referenced);
		for (auto &name : SortedNames(referenced)) {
			AddRequired(required, required_seen, name, root.name);
		}

		auto &target = filter.is_aggregate ? select->having : select->where_clause;
		AndInto(target, std::move(filter.expr));
	}

	unique_ptr<TableRef> from = options.population_source ? options.population_source->Copy() : BindTable(root);
	select->from_table = JoinRequired(model, root, required, std::move(from));
	select->aggregate_handling = AggregateHandling::STANDARD_HANDLING;

	auto statement = make_uniq<SelectStatement>();
	statement->node = std::move(select);
	return statement;
}

string CompileToSQL(const Model &model, const vector<string> &metrics, const vector<string> &dimensions,
                    const vector<string> &filters, const CompileOptions &options) {
	return Compile(model, metrics, dimensions, filters, options)->ToString();
}

} // namespace ossie
} // namespace duckdb
