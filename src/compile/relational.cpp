#include "ossie/relational.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/star_expression.hpp"
#include "duckdb/parser/expression/subquery_expression.hpp"
#include "duckdb/parser/expression/window_expression.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/query_node/cte_node.hpp"
#include "duckdb/parser/query_node/recursive_cte_node.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/query_node/set_operation_node.hpp"
#include "duckdb/parser/result_modifier.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
#include "duckdb/parser/tableref/expressionlistref.hpp"
#include "duckdb/parser/tableref/joinref.hpp"
#include "duckdb/parser/tableref/pivotref.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "ossie/catalog.hpp"
#include "ossie/compile.hpp"
#include "ossie/names.hpp"

namespace duckdb {
namespace ossie {

namespace {

//! Where an expression stands in its query block. AGG is an aggregate, so the clause decides whether it may
//! appear, and ORDER BY is the one clause in which a name is an output column before it is a column of FROM.
enum class Clause : uint8_t { SELECT_LIST, WHERE, GROUP_BY, JOIN_ON, ORDER_BY, OTHER };

//! One model relation in a FROM clause, replaced by a projection of its dataset's fields over the source.
struct Relation {
	//! What the query block calls it: the alias, or the dataset name as written.
	string name;
	bool aliased = false;
	const Dataset *dataset = nullptr;
	SelectNode *projection = nullptr;
	//! Source columns a measure reads past the fields, by the name the projection gives them.
	case_insensitive_map_t<string> source_columns;
};

struct Block {
	Block *parent = nullptr;
	vector<unique_ptr<Relation>> relations;
	//! Every name the FROM clause introduces, model relation or not, so that an outer relation of the same
	//! name is known to be hidden.
	case_insensitive_set_t names;
	//! The FROM clause is one model relation and nothing else.
	bool single_relation = false;
	case_insensitive_set_t output_names;
	//! Expressions inside FROM, visited once every relation of the block is known.
	vector<unique_ptr<ParsedExpression> *> from_expressions;
	vector<unique_ptr<ParsedExpression> *> join_conditions;
};

bool IsAggCall(const ParsedExpression &expr) {
	return expr.GetExpressionClass() == ExpressionClass::FUNCTION &&
	       StringUtil::CIEquals(expr.Cast<FunctionExpression>().FunctionName().GetIdentifierName(), "agg");
}

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

bool HasAggregate(const ParsedExpression &expr) {
	if (expr.GetExpressionClass() == ExpressionClass::FUNCTION &&
	    IsAggregateName(expr.Cast<FunctionExpression>().FunctionName().GetIdentifierName())) {
		return true;
	}
	bool found = false;
	ParsedExpressionIterator::EnumerateChildren(
	    expr, [&](const ParsedExpression &child) { found = found || HasAggregate(child); });
	return found;
}

//! Points the column references of a field's expression at the source, which the projection aliases to
//! the dataset's name.
void QualifyColumns(unique_ptr<ParsedExpression> &expr, const string &alias) {
	if (expr->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto &colref = expr->Cast<ColumnRefExpression>();
		expr = make_uniq<ColumnRefExpression>(Identifier(ColumnNames(colref).back()), Identifier(alias));
		return;
	}
	ParsedExpressionIterator::EnumerateChildren(
	    *expr, [&](unique_ptr<ParsedExpression> &child) { QualifyColumns(child, alias); });
}

class RelationalCompiler {
public:
	RelationalCompiler(ClientContext &context, const Model &model) : context(context), model(model) {
	}

	void VisitNode(QueryNode &node, Block *parent) {
		for (auto &entry : node.cte_map.map) {
			VisitNode(*entry.second->query_node, parent);
		}
		switch (node.type) {
		case QueryNodeType::SELECT_NODE:
			VisitSelect(node.Cast<SelectNode>(), parent);
			return;
		case QueryNodeType::SET_OPERATION_NODE: {
			for (auto &child : node.Cast<SetOperationNode>().children) {
				VisitNode(*child, parent);
			}
			break;
		}
		case QueryNodeType::RECURSIVE_CTE_NODE: {
			auto &recursive = node.Cast<RecursiveCTENode>();
			VisitNode(*recursive.left, parent);
			VisitNode(*recursive.right, parent);
			break;
		}
		case QueryNodeType::CTE_NODE: {
			auto &cte = node.Cast<CTENode>();
			VisitNode(*cte.query, parent);
			VisitNode(*cte.child, parent);
			break;
		}
		default:
			throw InvalidInputException("ossie_sql: the query may only read. A statement that writes cannot be "
			                            "part of it, in a WITH clause or anywhere else");
		}
		// The ORDER BY and LIMIT of a set operation stand over no FROM clause of their own.
		Block block;
		block.parent = parent;
		VisitModifiers(node.modifiers, block);
	}

private:
	ClientContext &context;
	const Model &model;

	const Metric *FindMeasure(const Dataset &dataset, const string &name) const {
		auto metric = model.FindMetric(dataset.name + "." + name);
		return metric && StringUtil::CIEquals(metric->dataset, dataset.name) ? metric : nullptr;
	}

	vector<string> MeasureNames(const Dataset &dataset) const {
		vector<string> names;
		for (auto &metric : model.metrics) {
			if (StringUtil::CIEquals(metric.dataset, dataset.name)) {
				names.push_back(metric.name.substr(dataset.name.size() + 1));
			}
		}
		return names;
	}

	string RelationLabel(const Relation &relation) const {
		auto label = model.name + "." + relation.dataset->name;
		return relation.aliased ? label + " AS " + relation.name : label;
	}

	//! The model relation `name` stands for at this point of the query, nullptr when the innermost FROM
	//! clause that introduces the name gives it to something else.
	Relation *FindRelation(Block *block, const string &name) const {
		for (; block; block = block->parent) {
			if (block->names.find(name) == block->names.end()) {
				continue;
			}
			for (auto &relation : block->relations) {
				if (StringUtil::CIEquals(relation->name, name)) {
					return relation.get();
				}
			}
			return nullptr;
		}
		return nullptr;
	}

	//! The dataset a table reference names as `model.dataset`, or nullptr for a table of the database.
	const Dataset *ModelRelation(const BaseTableRef &table) const {
		auto &qualified = table.GetQualifiedName();
		if (!qualified.Catalog().empty() || !StringUtil::CIEquals(qualified.Schema().GetIdentifierName(), model.name)) {
			return nullptr;
		}
		auto name = qualified.Name().GetIdentifierName();
		auto dataset = model.FindDataset(name);
		if (dataset) {
			return dataset;
		}
		// A schema of the database may carry the model's name, and its tables are then not ours to refuse.
		if (SourceResolves(context, StringUtil::Format("%s.%s", SQLIdentifier(model.name), SQLIdentifier(name)))) {
			return nullptr;
		}
		vector<string> datasets;
		for (auto &entry : model.datasets) {
			datasets.push_back(entry.name);
		}
		throw InvalidInputException("ossie_sql: model \"%s\" has no dataset named \"%s\".%s", model.name, name,
		                            StringUtil::CandidatesErrorMessage(datasets, name, "Did you mean"));
	}

	//! `(SELECT <field expression> AS <field>, ... FROM <source> AS <dataset>) AS <name>`: the relation's
	//! field columns. Its measures are not columns of the projection; AGG reads them through it.
	unique_ptr<TableRef> Project(BaseTableRef &written, Relation &relation) {
		auto &dataset = *relation.dataset;
		if (dataset.fields.empty()) {
			throw InvalidInputException("ossie_sql: dataset \"%s\" declares no field, so the relation %s.%s has no "
			                            "column to read",
			                            dataset.name, model.name, dataset.name);
		}
		if (written.at_clause) {
			throw InvalidInputException("ossie_sql: AT on the model relation %s.%s is not supported; a dataset is "
			                            "read as its source stands",
			                            model.name, dataset.name);
		}
		if (!written.column_name_alias.empty()) {
			throw InvalidInputException("ossie_sql: column aliases on the model relation %s.%s are not supported; "
			                            "rename a column in the SELECT list",
			                            model.name, dataset.name);
		}
		auto select = make_uniq<SelectNode>();
		for (auto &field : dataset.fields) {
			auto expr = field.expression.tree->Copy();
			QualifyColumns(expr, dataset.name);
			expr->SetAlias(Identifier(field.name));
			select->select_list.push_back(std::move(expr));
		}
		auto source = make_uniq<BaseTableRef>();
		string catalog, schema, name;
		SplitSource(dataset.source_bound, catalog, schema, name);
		source->SetQualifiedName(Identifier(catalog), Identifier(schema), Identifier(name));
		source->alias = Identifier(dataset.name);
		source->sample = std::move(written.sample);
		select->from_table = std::move(source);
		relation.projection = select.get();

		auto statement = make_uniq<SelectStatement>();
		statement->node = std::move(select);
		auto result = make_uniq<SubqueryRef>(std::move(statement), Identifier(relation.name));
		result->query_location = written.query_location;
		return std::move(result);
	}

	void AddName(Block &block, const TableRef &ref) {
		if (!ref.alias.empty()) {
			block.names.insert(ref.alias.GetIdentifierName());
		}
	}

	void VisitTable(unique_ptr<TableRef> &ref, Block &block) {
		if (!ref) {
			return;
		}
		switch (ref->type) {
		case TableReferenceType::BASE_TABLE: {
			auto &table = ref->Cast<BaseTableRef>();
			auto name = table.alias.empty() ? table.Table().GetIdentifierName() : table.alias.GetIdentifierName();
			auto dataset = ModelRelation(table);
			block.names.insert(name);
			if (!dataset) {
				return;
			}
			auto relation = make_uniq<Relation>();
			relation->name = name;
			relation->aliased = !table.alias.empty();
			relation->dataset = dataset;
			ref = Project(table, *relation);
			block.relations.push_back(std::move(relation));
			return;
		}
		case TableReferenceType::SUBQUERY:
			AddName(block, *ref);
			VisitNode(*ref->Cast<SubqueryRef>().subquery->node, &block);
			return;
		case TableReferenceType::JOIN: {
			auto &join = ref->Cast<JoinRef>();
			VisitTable(join.left, block);
			VisitTable(join.right, block);
			if (join.condition) {
				block.join_conditions.push_back(&join.condition);
			}
			return;
		}
		case TableReferenceType::TABLE_FUNCTION: {
			auto &function = ref->Cast<TableFunctionRef>();
			AddName(block, *ref);
			if (function.function) {
				block.from_expressions.push_back(&function.function);
			}
			if (function.subquery) {
				VisitNode(*function.subquery->node, &block);
			}
			return;
		}
		case TableReferenceType::PIVOT: {
			auto &pivot = ref->Cast<PivotRef>();
			AddName(block, *ref);
			VisitTable(pivot.source, block);
			for (auto &aggregate : pivot.aggregates) {
				block.from_expressions.push_back(&aggregate);
			}
			return;
		}
		case TableReferenceType::EXPRESSION_LIST: {
			AddName(block, *ref);
			for (auto &row : ref->Cast<ExpressionListRef>().values) {
				for (auto &value : row) {
					block.from_expressions.push_back(&value);
				}
			}
			return;
		}
		default:
			AddName(block, *ref);
			return;
		}
	}

	void VisitSelect(SelectNode &select, Block *parent) {
		Block block;
		block.parent = parent;
		auto from_one_table = select.from_table && select.from_table->type == TableReferenceType::BASE_TABLE;
		VisitTable(select.from_table, block);
		block.single_relation = from_one_table && block.relations.size() == 1;
		for (auto &expr : select.select_list) {
			if (expr->HasAlias()) {
				block.output_names.insert(expr->GetAlias().GetIdentifierName());
			}
		}

		for (auto expr : block.from_expressions) {
			Visit(*expr, block, Clause::OTHER);
		}
		for (auto expr : block.join_conditions) {
			Visit(*expr, block, Clause::JOIN_ON);
		}
		for (auto &expr : select.select_list) {
			// A measure evaluated on its own is named for the measure, not for its definition.
			auto named = !expr->HasAlias() && IsAggCall(*expr);
			Visit(expr, block, Clause::SELECT_LIST, named);
		}
		Visit(select.where_clause, block, Clause::WHERE);
		for (auto &expr : select.groups.group_expressions) {
			Visit(expr, block, Clause::GROUP_BY);
		}
		Visit(select.having, block, Clause::OTHER);
		Visit(select.qualify, block, Clause::OTHER);
		VisitModifiers(select.modifiers, block);
	}

	void VisitModifiers(vector<unique_ptr<ResultModifier>> &modifiers, Block &block) {
		for (auto &modifier : modifiers) {
			switch (modifier->type) {
			case ResultModifierType::ORDER_MODIFIER:
				for (auto &order : modifier->Cast<OrderModifier>().orders) {
					Visit(order.expression, block, Clause::ORDER_BY);
				}
				break;
			case ResultModifierType::LIMIT_MODIFIER: {
				auto &limit = modifier->Cast<LimitModifier>();
				Visit(limit.limit, block, Clause::OTHER);
				Visit(limit.offset, block, Clause::OTHER);
				break;
			}
			case ResultModifierType::DISTINCT_MODIFIER:
				for (auto &target : modifier->Cast<DistinctModifier>().distinct_on_targets) {
					Visit(target, block, Clause::OTHER);
				}
				break;
			default:
				break;
			}
		}
	}

	void Visit(unique_ptr<ParsedExpression> &expr, Block &block, Clause clause, bool name_measure = false) {
		if (!expr) {
			return;
		}
		switch (expr->GetExpressionClass()) {
		case ExpressionClass::SUBQUERY:
			VisitNode(*expr->Cast<SubqueryExpression>().SubqueryMutable()->node, &block);
			break;
		case ExpressionClass::WINDOW:
			if (StringUtil::CIEquals(expr->Cast<WindowExpression>().FunctionName().GetIdentifierName(), "agg")) {
				throw InvalidInputException("ossie_sql: AGG cannot take an OVER clause. It is an aggregate over the "
				                            "rows of a group; apply a window function to its value instead, as in "
				                            "SUM(AGG(measure)) OVER (...)");
			}
			break;
		case ExpressionClass::FUNCTION:
			if (IsAggCall(*expr)) {
				EvaluateMeasure(expr, block, clause, name_measure);
				return;
			}
			break;
		case ExpressionClass::STAR:
			RefuseStarOverMeasures(expr->Cast<StarExpression>(), block);
			break;
		case ExpressionClass::COLUMN_REF:
			VisitColumn(expr, block, clause);
			return;
		default:
			break;
		}
		ParsedExpressionIterator::EnumerateChildren(
		    *expr, [&](unique_ptr<ParsedExpression> &child) { Visit(child, block, clause); });
	}

	//! `model.dataset.column` names a column of the relation written without an alias, as it does for a table.
	void DropModelQualifier(vector<string> &names, Block &block) const {
		if (names.size() != 3 || !StringUtil::CIEquals(names[0], model.name)) {
			return;
		}
		auto relation = FindRelation(&block, names[1]);
		if (relation && !relation->aliased) {
			names.erase(names.begin());
		}
	}

	[[noreturn]] void ThrowUnevaluatedMeasure(const Relation &relation, const string &measure) const {
		throw InvalidInputException("ossie_sql: \"%s\" is a measure of %s, and a measure has a value only where "
		                            "AGG evaluates it: write AGG(%s). Passing a measure through unevaluated is "
		                            "not supported yet",
		                            measure, RelationLabel(relation), measure);
	}

	void VisitColumn(unique_ptr<ParsedExpression> &expr, Block &block, Clause clause) {
		auto names = ColumnNames(expr->Cast<ColumnRefExpression>());
		if (names.size() == 3) {
			DropModelQualifier(names, block);
			if (names.size() == 2) {
				auto replacement = make_uniq<ColumnRefExpression>(Identifier(names[1]), Identifier(names[0]));
				replacement->SetAlias(expr->GetAlias());
				replacement->SetQueryLocation(expr->GetQueryLocation());
				expr = std::move(replacement);
			}
		}
		if (names.size() == 1) {
			// ORDER BY reads an output column of that name before a column of FROM.
			if (clause == Clause::ORDER_BY && block.output_names.find(names[0]) != block.output_names.end()) {
				return;
			}
			for (auto &relation : block.relations) {
				if (FindMeasure(*relation->dataset, names[0])) {
					ThrowUnevaluatedMeasure(*relation, names[0]);
				}
			}
			return;
		}
		if (names.size() == 2) {
			auto relation = FindRelation(&block, names[0]);
			if (relation && FindMeasure(*relation->dataset, names[1])) {
				ThrowUnevaluatedMeasure(*relation, names[1]);
			}
		}
	}

	void RefuseStarOverMeasures(const StarExpression &star, Block &block) const {
		auto qualifier = star.RelationName().GetIdentifierName();
		for (auto &relation : block.relations) {
			if (!qualifier.empty() && !StringUtil::CIEquals(qualifier, relation->name)) {
				continue;
			}
			if (!MeasureNames(*relation->dataset).empty()) {
				throw InvalidInputException("ossie_sql: * over %s would include its measures, and passing a measure "
				                            "through unevaluated is not supported yet. Name the fields, and "
				                            "evaluate a measure with AGG",
				                            RelationLabel(*relation));
			}
		}
	}

	[[noreturn]] void ThrowNotAMeasure(const Relation &relation, const string &name) const {
		if (relation.dataset->FindField(name)) {
			throw InvalidInputException("ossie_sql: AGG(%s): \"%s\" is a field of %s, not a measure. A field takes "
			                            "an ordinary aggregate, such as SUM(%s)",
			                            name, name, RelationLabel(relation), name);
		}
		auto measures = MeasureNames(*relation.dataset);
		if (measures.empty()) {
			throw InvalidInputException("ossie_sql: AGG(%s): %s has no measure", name, RelationLabel(relation));
		}
		throw InvalidInputException("ossie_sql: AGG(%s): %s has no measure named \"%s\". Its measures: %s", name,
		                            RelationLabel(relation), name, StringUtil::Join(measures, ", "));
	}

	//! Replaces AGG(measure) with the measure's expression, read through the relation's projection.
	void EvaluateMeasure(unique_ptr<ParsedExpression> &expr, Block &block, Clause clause, bool name_measure) {
		auto &call = expr->Cast<FunctionExpression>();
		auto &arguments = call.GetArguments();
		auto ordered = call.OrderBy() && !call.OrderBy()->orders.empty();
		if (arguments.size() != 1 || arguments[0].HasName() || call.Filter() || call.Distinct() || ordered ||
		    arguments[0].GetExpression().GetExpressionClass() != ExpressionClass::COLUMN_REF) {
			throw InvalidInputException("ossie_sql: AGG takes one measure and nothing else, as AGG(measure) or "
			                            "AGG(relation.measure), got %s",
			                            call.ToString());
		}
		auto names = ColumnNames(arguments[0].GetExpression().Cast<ColumnRefExpression>());
		DropModelQualifier(names, block);
		auto written = StringUtil::Join(names, ".");
		if (names.size() > 2) {
			throw InvalidInputException("ossie_sql: AGG takes one measure, as AGG(measure) or AGG(relation.measure), "
			                            "got AGG(%s)",
			                            written);
		}
		if (clause == Clause::WHERE || clause == Clause::GROUP_BY || clause == Clause::JOIN_ON) {
			auto where = clause == Clause::WHERE ? "WHERE" : clause == Clause::GROUP_BY ? "GROUP BY" : "a join's ON";
			throw InvalidInputException("ossie_sql: AGG(%s) cannot appear in %s. AGG is an aggregate over the rows of "
			                            "a group, so a condition on a measure's value goes in HAVING",
			                            written, where);
		}

		Relation *relation = nullptr;
		const Metric *metric = nullptr;
		if (names.size() == 2) {
			for (auto &candidate : block.relations) {
				if (StringUtil::CIEquals(candidate->name, names[0])) {
					relation = candidate.get();
				}
			}
			if (!relation) {
				if (FindRelation(block.parent, names[0])) {
					throw InvalidInputException("ossie_sql: AGG(%s): \"%s\" is a relation of an outer query block. "
					                            "AGG evaluates a measure of a relation in its own block's FROM "
					                            "clause",
					                            written, names[0]);
				}
				throw InvalidInputException("ossie_sql: AGG(%s): the FROM clause of this query block has no model "
				                            "relation named \"%s\"",
				                            written, names[0]);
			}
			metric = FindMeasure(*relation->dataset, names[1]);
			if (!metric) {
				ThrowNotAMeasure(*relation, names[1]);
			}
		} else {
			if (block.relations.empty()) {
				throw InvalidInputException("ossie_sql: AGG(%s): the FROM clause of this query block names no model "
				                            "relation. A measure is a column of a model relation, such as %s.%s",
				                            written, model.name,
				                            model.datasets.empty() ? "dataset" : model.datasets[0].name);
			}
			for (auto &candidate : block.relations) {
				auto found = FindMeasure(*candidate->dataset, names[0]);
				if (!found) {
					continue;
				}
				if (relation) {
					throw InvalidInputException("ossie_sql: AGG(%s) is ambiguous: %s and %s both have a measure of "
					                            "that name. Qualify it, as AGG(%s.%s)",
					                            written, RelationLabel(*relation), RelationLabel(*candidate),
					                            relation->name, names[0]);
				}
				relation = candidate.get();
				metric = found;
			}
			if (!relation) {
				ThrowNotAMeasure(*block.relations[0], names[0]);
			}
		}
		if (!block.single_relation) {
			throw InvalidInputException("ossie_sql: AGG(%s): evaluating a measure is supported only where the FROM "
			                            "clause of its query block is one model relation. Over a join, each "
			                            "measure has to be aggregated at its own relation's grain, which is not "
			                            "supported yet",
			                            written);
		}
		auto window = FindWindow(*metric->expression.tree);
		if (window) {
			throw InvalidInputException("ossie_sql: AGG(%s): measure \"%s\" uses the window function %s, whose "
			                            "partition and order are fixed in the model while the rows it runs over "
			                            "depend on the query. Window measures are not supported yet",
			                            written, metric->name,
			                            StringUtil::Upper(window->FunctionName().GetIdentifierName()));
		}

		if (!HasAggregate(*metric->expression.tree)) {
			throw InvalidInputException("ossie_sql: AGG(%s): measure \"%s\" contains no aggregate, so it has no "
			                            "value over the rows of a group",
			                            written, metric->name);
		}

		auto value = metric->expression.tree->Copy();
		ReadThroughProjection(value, *relation);
		if (expr->HasAlias()) {
			value->SetAlias(expr->GetAlias());
		} else if (name_measure) {
			value->SetAlias(Identifier(names.back()));
		}
		value->SetQueryLocation(expr->GetQueryLocation());
		expr = std::move(value);
	}

	//! A measure names its dataset's fields as `dataset.field` and a column of the source as
	//! `dataset.source.column`. The first is a column of the projection; the second is added to it.
	void ReadThroughProjection(unique_ptr<ParsedExpression> &expr, Relation &relation) {
		if (expr->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
			ParsedExpressionIterator::EnumerateChildren(
			    *expr, [&](unique_ptr<ParsedExpression> &child) { ReadThroughProjection(child, relation); });
			return;
		}
		auto names = ColumnNames(expr->Cast<ColumnRefExpression>());
		auto column = names.back();
		if (names.size() == 3) {
			column = SourceColumn(relation, column);
		}
		expr = make_uniq<ColumnRefExpression>(Identifier(column), Identifier(relation.name));
	}

	string SourceColumn(Relation &relation, const string &column) {
		auto entry = relation.source_columns.find(column);
		if (entry != relation.source_columns.end()) {
			return entry->second;
		}
		auto name = "source." + column;
		while (relation.dataset->FindField(name)) {
			name += "_";
		}
		auto expr = make_uniq<ColumnRefExpression>(Identifier(column), Identifier(relation.dataset->name));
		expr->SetAlias(Identifier(name));
		relation.projection->select_list.push_back(std::move(expr));
		relation.source_columns[column] = name;
		return name;
	}
};

} // namespace

unique_ptr<SelectStatement> CompileRelational(ClientContext &context, const Model &model, const string &sql) {
	Parser parser(context);
	parser.ParseQuery(sql);
	if (parser.statements.size() != 1) {
		throw InvalidInputException("ossie_sql: the query must be one statement, got %d",
		                            static_cast<int>(parser.statements.size()));
	}
	if (parser.statements[0]->type != StatementType::SELECT_STATEMENT) {
		throw InvalidInputException("ossie_sql: the query must be a SELECT; ossie_sql only reads");
	}
	auto statement = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	RelationalCompiler compiler(context, model);
	compiler.VisitNode(*statement->node, nullptr);
	return statement;
}

} // namespace ossie
} // namespace duckdb
