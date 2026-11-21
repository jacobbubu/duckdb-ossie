#include "ossie/relational.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/entry_lookup_info.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/conjunction_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
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
#include "duckdb/parser/tableref/emptytableref.hpp"
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
enum class Clause : uint8_t { SELECT_LIST, WHERE, GROUP_BY, JOIN_ON, HAVING, ORDER_BY, PIVOT, OTHER };

//! A measure as a column of a relation. It has no value until AGG evaluates it. Until then the column holds
//! what the evaluation takes from a row of the measure's dataset: the columns the measure reads, and the
//! primary key, which tells one row of the dataset from another.
struct Carried {
	//! The column's name in the relation.
	string name;
	const Metric *metric = nullptr;
	const Dataset *dataset = nullptr;
	//! No row of the dataset stands in two rows of the relation.
	bool once = true;
	//! The column came out of a *.
	bool starred = false;
	//! In a model relation: the query reads the column, so the projection has to have it.
	bool needed = false;
};

//! A relation of a FROM clause that has, or may have, measure columns.
struct Relation {
	//! What the query block calls it: the alias, or the name as written.
	string name;
	bool aliased = false;
	//! The dataset of a model relation, which is replaced by a projection of the dataset's fields over the
	//! source. nullptr for a subquery or a CTE that passes measures through.
	const Dataset *dataset = nullptr;
	SelectNode *projection = nullptr;
	vector<Carried> measures;
};

//! A NATURAL JOIN, as the relations of the block on its two sides: [first, middle) and [middle, last).
struct NaturalJoin {
	idx_t first = 0;
	idx_t middle = 0;
	idx_t last = 0;
	//! The side has an item whose columns are not known here.
	bool left_unknown = false;
	bool right_unknown = false;
};

struct Block {
	Block *parent = nullptr;
	const SelectNode *select = nullptr;
	vector<unique_ptr<Relation>> relations;
	//! Every name the FROM clause introduces, whatever it names, so that an outer relation of the same name
	//! is known to be hidden.
	case_insensitive_set_t names;
	//! The FROM clause is one relation and nothing else, so each of its rows stands in the block once.
	bool single_relation = false;
	//! Whether the SELECT groups or aggregates.
	bool aggregated = false;
	//! How many items of the FROM clause are not model relations. The columns of those are not known here.
	idx_t unknown = 0;
	vector<NaturalJoin> naturals;
	case_insensitive_set_t output_names;
	//! The measures the SELECT list passes on unevaluated.
	vector<Carried> outputs;
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

//! `the measure "a"`, or `the measures "a", "b"`.
string Listed(const vector<Carried> &measures) {
	vector<string> names;
	for (auto &measure : measures) {
		names.push_back("\"" + measure.name + "\"");
	}
	return (names.size() == 1 ? "the measure " : "the measures ") + StringUtil::Join(names, ", ");
}

//! Where a query hands on a measure that nothing after it can evaluate.
[[noreturn]] void ThrowReturned(const string &what, const vector<Carried> &measures) {
	vector<string> starred;
	for (auto &measure : measures) {
		if (measure.starred) {
			starred.push_back(measure.name);
		}
	}
	string hint;
	if (!starred.empty()) {
		hint = " (out of *, with * EXCLUDE (" + StringUtil::Join(starred, ", ") + "))";
	}
	throw InvalidInputException("ossie_sql: %s returns %s unevaluated. A measure has a value only where AGG "
	                            "evaluates it: write AGG(%s), or leave the measure out of the SELECT list%s",
	                            what, Listed(measures), measures[0].name, hint);
}

//! One member of the struct a measure is carried as: a field of the dataset, or a column of its source.
struct Member {
	string name;
	const Field *field = nullptr;
	string column;
};

//! A column of the source is carried as `source.<column>`, a name no field of the dataset has.
Member MemberOf(const Dataset &dataset, const string &column, bool of_source) {
	Member member;
	member.field = of_source ? nullptr : dataset.FindField(column);
	if (member.field) {
		member.name = member.field->name;
		return member;
	}
	member.column = column;
	member.name = "source." + column;
	while (dataset.FindField(member.name)) {
		member.name += "_";
	}
	return member;
}

void AddMember(vector<Member> &members, Member member) {
	for (auto &known : members) {
		if (StringUtil::CIEquals(known.name, member.name)) {
			return;
		}
	}
	members.push_back(std::move(member));
}

//! A measure names a field of its dataset as `dataset.field` and a column of the source as
//! `dataset.source.column`.
void AddColumnsRead(const Dataset &dataset, const ParsedExpression &expr, vector<Member> &members) {
	if (expr.GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto names = ColumnNames(expr.Cast<ColumnRefExpression>());
		AddMember(members, MemberOf(dataset, names.back(), names.size() == 3));
		return;
	}
	auto visit = [&](const ParsedExpression &child) {
		AddColumnsRead(dataset, child, members);
	};
	ParsedExpressionIterator::EnumerateChildren(expr, visit);
}

//! What a measure is carried as: the primary key of its dataset, then the columns it reads.
vector<Member> Members(const Dataset &dataset, const Metric &metric) {
	vector<Member> members;
	for (auto &key : dataset.primary_key) {
		AddMember(members, MemberOf(dataset, key, false));
	}
	AddColumnsRead(dataset, *metric.expression.tree, members);
	return members;
}

unique_ptr<ParsedExpression> Column(const string &column, const string &relation) {
	return make_uniq<ColumnRefExpression>(Identifier(column), Identifier(relation));
}

unique_ptr<ParsedExpression> MemberValue(unique_ptr<ParsedExpression> carried, const string &member) {
	vector<unique_ptr<ParsedExpression>> arguments;
	arguments.push_back(std::move(carried));
	arguments.push_back(make_uniq<ConstantExpression>(Literal::String(member)));
	return make_uniq<FunctionExpression>(Identifier("struct_extract"), std::move(arguments));
}

class RelationalCompiler {
public:
	RelationalCompiler(ClientContext &context, const Model &model) : context(context), model(model) {
	}

	//! Rewrites a query node and returns the measures its result carries unevaluated.
	vector<Carried> VisitNode(QueryNode &node, Block *parent) {
		ctes.emplace_back();
		for (auto &entry : node.cte_map.map) {
			DefineCte(entry.first.GetIdentifierName(), *entry.second->query_node, entry.second->aliases, parent);
		}
		vector<Carried> carried;
		switch (node.type) {
		case QueryNodeType::SELECT_NODE:
			carried = VisitSelect(node.Cast<SelectNode>(), parent);
			ctes.pop_back();
			return carried;
		case QueryNodeType::SET_OPERATION_NODE: {
			for (auto &child : node.Cast<SetOperationNode>().children) {
				VisitOperand(*child, parent, "a query under a set operator, such as UNION,");
			}
			break;
		}
		case QueryNodeType::RECURSIVE_CTE_NODE: {
			auto &recursive = node.Cast<RecursiveCTENode>();
			VisitOperand(*recursive.left, parent, "a query of a recursive CTE");
			VisitOperand(*recursive.right, parent, "a query of a recursive CTE");
			break;
		}
		case QueryNodeType::CTE_NODE: {
			auto &cte = node.Cast<CTENode>();
			DefineCte(cte.ctename.GetIdentifierName(), *cte.query, cte.aliases, parent);
			carried = VisitNode(*cte.child, parent);
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
		ctes.pop_back();
		return carried;
	}

private:
	//! What a column reference names, where it names a measure.
	struct Reference {
		Relation *relation = nullptr;
		Carried *measure = nullptr;
		//! A second relation of the block with a measure of the name.
		Relation *other = nullptr;
		//! The relation is of an outer query block.
		bool outer = false;
		//! The reference reads into the measure, as measure.member.
		bool member = false;
	};

	ClientContext &context;
	const Model &model;
	//! The CTEs in scope, innermost last, each with the measures its result carries.
	vector<case_insensitive_map_t<vector<Carried>>> ctes;

	//! A set operator compares or stacks rows, and a measure has nothing to compare or stack until it is
	//! evaluated.
	void VisitOperand(QueryNode &node, Block *parent, const char *what) {
		auto returned = VisitNode(node, parent);
		if (!returned.empty()) {
			ThrowReturned(what, returned);
		}
	}

	void DefineCte(const string &name, QueryNode &query, const vector<Identifier> &aliases, Block *parent) {
		if (query.type == QueryNodeType::RECURSIVE_CTE_NODE) {
			// A recursive CTE reads itself. It carries no measure: its queries may not return one.
			ctes.back()[name] = vector<Carried>();
		}
		auto carried = VisitNode(query, parent);
		if (!carried.empty() && !aliases.empty()) {
			RefuseColumnAliases(name, carried);
		}
		ctes.back()[name] = std::move(carried);
	}

	const vector<Carried> *FindCte(const string &name) const {
		for (auto scope = ctes.rbegin(); scope != ctes.rend(); ++scope) {
			auto entry = scope->find(name);
			if (entry != scope->end()) {
				return &entry->second;
			}
		}
		return nullptr;
	}

	const Metric *FindMeasure(const Dataset &dataset, const string &name) const {
		auto metric = model.FindMetric(dataset.name + "." + name);
		return metric && StringUtil::CIEquals(metric->dataset, dataset.name) ? metric : nullptr;
	}

	//! The measures of a dataset, as columns of its relation. A field of the same name is the column.
	vector<Carried> Measures(const Dataset &dataset) const {
		vector<Carried> measures;
		for (auto &metric : model.metrics) {
			if (!StringUtil::CIEquals(metric.dataset, dataset.name)) {
				continue;
			}
			Carried measure;
			measure.name = metric.name.substr(dataset.name.size() + 1);
			if (dataset.FindField(measure.name)) {
				continue;
			}
			measure.metric = &metric;
			measure.dataset = &dataset;
			measures.push_back(std::move(measure));
		}
		return measures;
	}

	static vector<string> MeasureNames(const Relation &relation) {
		vector<string> names;
		for (auto &measure : relation.measures) {
			names.push_back(measure.name);
		}
		return names;
	}

	static Carried *FindColumn(Relation &relation, const string &name) {
		for (auto &measure : relation.measures) {
			if (StringUtil::CIEquals(measure.name, name)) {
				return &measure;
			}
		}
		return nullptr;
	}

	string RelationLabel(const Relation &relation) const {
		if (!relation.dataset) {
			return relation.name;
		}
		auto label = model.name + "." + relation.dataset->name;
		return relation.aliased ? label + " AS " + relation.name : label;
	}

	//! The relation `name` stands for at this point of the query, nullptr when the innermost FROM clause
	//! that introduces the name gives it to something that has no measure.
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

	static Relation *FindOwn(Block &block, const string &name) {
		for (auto &relation : block.relations) {
			if (StringUtil::CIEquals(relation->name, name)) {
				return relation.get();
			}
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
	//! field columns. A measure the query reads is added to them once the block is done: see Finish.
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

	//! What a measure is carried as: `struct_pack(<key>, <column the measure reads>, ...) AS <measure>`.
	unique_ptr<ParsedExpression> Carrier(const Carried &measure) const {
		auto &dataset = *measure.dataset;
		vector<FunctionArgument> arguments;
		for (auto &member : Members(dataset, *measure.metric)) {
			unique_ptr<ParsedExpression> value;
			if (member.field) {
				value = member.field->expression.tree->Copy();
				QualifyColumns(value, dataset.name);
			} else {
				value = Column(member.column, dataset.name);
			}
			arguments.emplace_back(Identifier(member.name), std::move(value));
		}
		if (arguments.empty()) {
			// A measure that reads no column, of a dataset without a key: the row is all there is to carry.
			arguments.emplace_back(Identifier("row"), make_uniq<ConstantExpression>(Literal::Boolean(true)));
		}
		auto carrier = make_uniq<FunctionExpression>(Identifier("struct_pack"), std::move(arguments));
		carrier->SetAlias(Identifier(measure.name));
		return std::move(carrier);
	}

	//! Whether the measure is a column of what the relation was rewritten to.
	static bool IsColumn(const Relation &relation, const Carried &measure) {
		return !relation.dataset || measure.needed;
	}

	//! NATURAL JOIN is on every column name its two sides share, and a measure cannot be joined on.
	void RefuseNaturalJoinOn(Block &block, idx_t first, idx_t last, idx_t other_first, idx_t other_last,
	                         bool other_unknown) const {
		for (auto i = first; i < last; i++) {
			auto &relation = *block.relations[i];
			for (auto &measure : relation.measures) {
				if (!IsColumn(relation, measure)) {
					continue;
				}
				string shared;
				for (auto k = other_first; k < other_last && !other_unknown; k++) {
					auto &other = *block.relations[k];
					auto same = FindColumn(other, measure.name);
					auto is_field = other.dataset && other.dataset->FindField(measure.name);
					if (is_field || (same && IsColumn(other, *same))) {
						shared = RelationLabel(other) + " has a column of that name";
					}
				}
				if (other_unknown) {
					shared = "the other side of the join may have a column of that name";
				}
				if (shared.empty()) {
					continue;
				}
				throw InvalidInputException("ossie_sql: NATURAL JOIN would join on \"%s\", a measure of %s: %s. A "
				                            "measure has a value only where AGG evaluates it, so it cannot be "
				                            "joined on. Write the join's columns, with USING or ON",
				                            measure.name, RelationLabel(relation), shared);
			}
		}
	}

	//! Once a block is done it is known which measures of its model relations the query reads.
	void Finish(Block &block) {
		for (auto &join : block.naturals) {
			RefuseNaturalJoinOn(block, join.first, join.middle, join.middle, join.last, join.right_unknown);
			RefuseNaturalJoinOn(block, join.middle, join.last, join.first, join.middle, join.left_unknown);
		}
		for (auto &relation : block.relations) {
			if (!relation->dataset) {
				continue;
			}
			for (auto &measure : relation->measures) {
				if (measure.needed) {
					relation->projection->select_list.push_back(Carrier(measure));
				}
			}
		}
	}

	void AddName(Block &block, const TableRef &ref) {
		if (!ref.alias.empty()) {
			block.names.insert(ref.alias.GetIdentifierName());
		}
	}

	[[noreturn]] void RefuseColumnAliases(const string &name, const vector<Carried> &measures) const {
		throw InvalidInputException("ossie_sql: a list of column aliases on \"%s\" is not supported, because the "
		                            "relation carries %s unevaluated. Rename a column in the SELECT list",
		                            name, Listed(measures));
	}

	//! A subquery or a CTE whose result carries measures is a relation that has them as columns.
	void Derive(Block &block, const string &name, const vector<Carried> &measures) {
		auto relation = make_uniq<Relation>();
		relation->name = name;
		relation->aliased = true;
		for (auto &measure : measures) {
			if (FindColumn(*relation, measure.name)) {
				throw InvalidInputException("ossie_sql: \"%s\" would have two measures named \"%s\". Rename one of "
				                            "them where the query passes them on, so that AGG can tell them apart",
				                            name, measure.name);
			}
			relation->measures.push_back(measure);
			relation->measures.back().starred = false;
			relation->measures.back().needed = false;
		}
		block.relations.push_back(std::move(relation));
	}

	static string UnusedName(const Block &block) {
		string name = "unnamed_subquery";
		for (idx_t i = 2; block.names.find(name) != block.names.end(); i++) {
			name = "unnamed_subquery" + to_string(i);
		}
		return name;
	}

	void VisitTable(unique_ptr<TableRef> &ref, Block &block) {
		if (!ref) {
			return;
		}
		switch (ref->type) {
		case TableReferenceType::EMPTY_FROM:
			return;
		case TableReferenceType::BASE_TABLE: {
			auto &table = ref->Cast<BaseTableRef>();
			auto name = table.alias.empty() ? table.Table().GetIdentifierName() : table.alias.GetIdentifierName();
			block.names.insert(name);
			auto &qualified = table.GetQualifiedName();
			if (qualified.Catalog().empty() && qualified.Schema().empty()) {
				auto cte = FindCte(qualified.Name().GetIdentifierName());
				if (cte) {
					block.unknown++;
					if (cte->empty()) {
						return;
					}
					if (!table.column_name_alias.empty()) {
						RefuseColumnAliases(name, *cte);
					}
					Derive(block, name, *cte);
					return;
				}
			}
			auto dataset = ModelRelation(table);
			if (!dataset) {
				block.unknown++;
				return;
			}
			auto relation = make_uniq<Relation>();
			relation->name = name;
			relation->aliased = !table.alias.empty();
			relation->dataset = dataset;
			relation->measures = Measures(*dataset);
			ref = Project(table, *relation);
			block.relations.push_back(std::move(relation));
			return;
		}
		case TableReferenceType::SUBQUERY: {
			auto &subquery = ref->Cast<SubqueryRef>();
			auto carried = VisitNode(*subquery.subquery->node, &block);
			block.unknown++;
			if (carried.empty()) {
				AddName(block, *ref);
				return;
			}
			if (subquery.alias.empty()) {
				// AGG names the relation of its measure, so the relation has to have a name.
				subquery.alias = Identifier(UnusedName(block));
			}
			auto name = subquery.alias.GetIdentifierName();
			if (!subquery.column_name_alias.empty()) {
				RefuseColumnAliases(name, carried);
			}
			block.names.insert(name);
			Derive(block, name, carried);
			return;
		}
		case TableReferenceType::JOIN: {
			auto &join = ref->Cast<JoinRef>();
			NaturalJoin sides;
			sides.first = block.relations.size();
			auto unknown = block.unknown;
			VisitTable(join.left, block);
			sides.middle = block.relations.size();
			sides.left_unknown = block.unknown != unknown;
			unknown = block.unknown;
			VisitTable(join.right, block);
			sides.last = block.relations.size();
			sides.right_unknown = block.unknown != unknown;
			for (auto &column : join.using_columns) {
				for (auto i = sides.first; i < sides.last; i++) {
					auto measure = FindColumn(*block.relations[i], column.GetIdentifierName());
					if (measure) {
						ThrowMisused(*block.relations[i], measure->name, Clause::JOIN_ON);
					}
				}
			}
			if (join.ref_type == JoinRefType::NATURAL) {
				block.naturals.push_back(sides);
			}
			if (join.condition) {
				block.join_conditions.push_back(&join.condition);
			}
			return;
		}
		case TableReferenceType::TABLE_FUNCTION: {
			auto &function = ref->Cast<TableFunctionRef>();
			block.unknown++;
			AddName(block, *ref);
			if (function.function) {
				block.from_expressions.push_back(&function.function);
			}
			if (function.subquery) {
				VisitOperand(*function.subquery->node, &block, "the query a table function reads");
			}
			return;
		}
		case TableReferenceType::PIVOT: {
			auto &pivot = ref->Cast<PivotRef>();
			block.unknown++;
			AddName(block, *ref);
			// What a pivot reads is a FROM clause of its own: the block sees the pivot's columns, not these.
			Block source;
			source.parent = &block;
			VisitTable(pivot.source, source);
			for (auto &relation : source.relations) {
				if (!relation->dataset) {
					ThrowReturned("the source of a PIVOT or UNPIVOT", relation->measures);
				}
			}
			for (auto expr : source.from_expressions) {
				Visit(*expr, source, Clause::OTHER);
			}
			for (auto expr : source.join_conditions) {
				Visit(*expr, source, Clause::JOIN_ON);
			}
			for (auto &aggregate : pivot.aggregates) {
				Visit(aggregate, source, Clause::PIVOT);
			}
			for (auto &column : pivot.pivots) {
				for (auto &expr : column.pivot_expressions) {
					Visit(expr, source, Clause::PIVOT);
				}
			}
			Finish(source);
			return;
		}
		case TableReferenceType::EXPRESSION_LIST: {
			block.unknown++;
			AddName(block, *ref);
			for (auto &row : ref->Cast<ExpressionListRef>().values) {
				for (auto &value : row) {
					block.from_expressions.push_back(&value);
				}
			}
			return;
		}
		default:
			block.unknown++;
			AddName(block, *ref);
			return;
		}
	}

	optional_ptr<CatalogEntry> FunctionEntry(const FunctionExpression &function) const {
		EntryLookupInfo lookup(CatalogType::SCALAR_FUNCTION_ENTRY, function.GetQualifiedName());
		return Catalog::GetEntry(context, lookup, OnEntryNotFound::RETURN_NULL);
	}

	bool CallsAggregate(const ParsedExpression &expr) const {
		if (expr.GetExpressionClass() == ExpressionClass::FUNCTION) {
			auto &function = expr.Cast<FunctionExpression>();
			if (IsAggCall(expr) || IsAggregateName(function.FunctionName().GetIdentifierName())) {
				return true;
			}
			if (!function.IsOperator()) {
				auto entry = FunctionEntry(function);
				if (entry && entry->type == CatalogType::AGGREGATE_FUNCTION_ENTRY) {
					return true;
				}
			}
		}
		bool found = false;
		auto visit = [&](const ParsedExpression &child) {
			found = found || CallsAggregate(child);
		};
		ParsedExpressionIterator::EnumerateChildren(expr, visit);
		return found;
	}

	//! Whether the SELECT groups or aggregates, which is asked before AGG is rewritten into what it stands for.
	bool Aggregates(const SelectNode &select) const {
		if (!select.groups.group_expressions.empty() || !select.groups.grouping_sets.empty() || select.having ||
		    select.aggregate_handling == AggregateHandling::FORCE_AGGREGATES) {
			return true;
		}
		for (auto &expr : select.select_list) {
			if (CallsAggregate(*expr)) {
				return true;
			}
		}
		for (auto &modifier : select.modifiers) {
			if (modifier->type != ResultModifierType::ORDER_MODIFIER) {
				continue;
			}
			for (auto &order : modifier->Cast<OrderModifier>().orders) {
				if (CallsAggregate(*order.expression)) {
					return true;
				}
			}
		}
		return false;
	}

	//! Whether an expression of a SELECT list may give more than one row for a row of FROM, as UNNEST does. A
	//! macro may stand for one, and a function the catalog does not know is not known not to.
	bool MayRepeatRows(const ParsedExpression &expr) const {
		if (expr.GetExpressionClass() == ExpressionClass::FUNCTION && !expr.Cast<FunctionExpression>().IsOperator()) {
			auto &function = expr.Cast<FunctionExpression>();
			auto name = StringUtil::Lower(function.FunctionName().GetIdentifierName());
			if (name == "unnest" || name == "unlist") {
				return true;
			}
			auto entry = FunctionEntry(function);
			if (!entry || entry->type == CatalogType::MACRO_ENTRY) {
				return true;
			}
		}
		bool found = false;
		auto visit = [&](const ParsedExpression &child) {
			found = found || MayRepeatRows(child);
		};
		ParsedExpressionIterator::EnumerateChildren(expr, visit);
		return found;
	}

	vector<Carried> VisitSelect(SelectNode &select, Block *parent) {
		Block block;
		block.parent = parent;
		block.select = &select;
		block.aggregated = Aggregates(select);
		auto from = select.from_table ? select.from_table->type : TableReferenceType::EMPTY_FROM;
		VisitTable(select.from_table, block);
		auto from_one = from == TableReferenceType::BASE_TABLE || from == TableReferenceType::SUBQUERY;
		block.single_relation = from_one && block.relations.size() == 1;
		for (auto &expr : select.select_list) {
			if (expr->HasAlias()) {
				block.output_names.insert(expr->GetAlias().GetIdentifierName());
			}
		}
		// Asked before the SELECT list is rewritten: AGG may put an UNNEST of its own there.
		auto repeats = false;
		for (auto &expr : select.select_list) {
			repeats = repeats || MayRepeatRows(*expr);
		}

		for (auto expr : block.from_expressions) {
			Visit(*expr, block, Clause::OTHER);
		}
		for (auto expr : block.join_conditions) {
			Visit(*expr, block, Clause::JOIN_ON);
		}
		for (auto &expr : select.select_list) {
			VisitItem(expr, block);
		}
		Visit(select.where_clause, block, Clause::WHERE);
		for (auto &expr : select.groups.group_expressions) {
			Visit(expr, block, Clause::GROUP_BY);
		}
		Visit(select.having, block, Clause::HAVING);
		Visit(select.qualify, block, Clause::OTHER);
		VisitModifiers(select.modifiers, block);
		Finish(block);

		// A row of a measure's dataset stands in one row of the result at most if it did in the relation the
		// measure came from, and the block is that relation and nothing that repeats its rows.
		for (auto &output : block.outputs) {
			output.once = output.once && block.single_relation && !repeats;
		}
		return std::move(block.outputs);
	}

	void VisitModifiers(vector<unique_ptr<ResultModifier>> &modifiers, Block &block) {
		for (auto &modifier : modifiers) {
			switch (modifier->type) {
			case ResultModifierType::ORDER_MODIFIER: {
				auto &orders = modifier->Cast<OrderModifier>().orders;
				for (auto &order : orders) {
					VisitOrder(order.expression, block, orders.size() == 1);
				}
				break;
			}
			case ResultModifierType::LIMIT_MODIFIER: {
				auto &limit = modifier->Cast<LimitModifier>();
				Visit(limit.limit, block, Clause::OTHER);
				Visit(limit.offset, block, Clause::OTHER);
				break;
			}
			case ResultModifierType::DISTINCT_MODIFIER:
				if (!block.outputs.empty()) {
					throw InvalidInputException("ossie_sql: DISTINCT over %s, which the SELECT list passes on "
					                            "unevaluated, is not supported: a measure has a value only where "
					                            "AGG evaluates it, so rows cannot be compared on it. Write AGG(%s), "
					                            "or leave the measure out of the SELECT list",
					                            Listed(block.outputs), block.outputs[0].name);
				}
				for (auto &target : modifier->Cast<DistinctModifier>().distinct_on_targets) {
					Visit(target, block, Clause::OTHER);
				}
				break;
			default:
				break;
			}
		}
	}

	//! ORDER BY ALL is a star that names nothing, standing alone.
	static bool IsOrderByAll(const ParsedExpression &expr) {
		if (expr.GetExpressionClass() != ExpressionClass::STAR) {
			return false;
		}
		auto &star = expr.Cast<StarExpression>();
		return star.ExcludeList().empty() && star.ReplaceList().empty() && !star.Expression();
	}

	void VisitOrder(unique_ptr<ParsedExpression> &expr, Block &block, bool alone) {
		auto type = expr->GetExpressionClass();
		if (alone && IsOrderByAll(*expr)) {
			// It orders by the columns of the SELECT list, which were visited there.
			if (!block.outputs.empty()) {
				throw InvalidInputException("ossie_sql: ORDER BY ALL would order by %s, which the SELECT list "
				                            "passes on unevaluated. Name the columns to order by",
				                            Listed(block.outputs));
			}
			return;
		}
		if (type == ExpressionClass::CONSTANT && !block.outputs.empty()) {
			throw InvalidInputException("ossie_sql: ORDER BY %s: ordering by a position in the SELECT list is not "
			                            "supported where the list passes %s on unevaluated. Name the column",
			                            expr->ToString(), Listed(block.outputs));
		}
		Visit(expr, block, Clause::ORDER_BY);
	}

	//! An item of the SELECT list: the one place where a measure may stand on its own, to be passed on.
	void VisitItem(unique_ptr<ParsedExpression> &expr, Block &block) {
		auto type = expr->GetExpressionClass();
		if (type == ExpressionClass::STAR && !expr->Cast<StarExpression>().IsColumns()) {
			PassStar(expr->Cast<StarExpression>(), block);
			return;
		}
		if (type == ExpressionClass::COLUMN_REF && Pass(expr, block)) {
			return;
		}
		// A measure evaluated on its own is named for the measure, not for its definition.
		auto named = !expr->HasAlias() && IsAggCall(*expr);
		Visit(expr, block, Clause::SELECT_LIST, named);
	}

	void Visit(unique_ptr<ParsedExpression> &expr, Block &block, Clause clause, bool name_measure = false) {
		if (!expr) {
			return;
		}
		switch (expr->GetExpressionClass()) {
		case ExpressionClass::SUBQUERY: {
			auto &subquery = expr->Cast<SubqueryExpression>();
			auto returned = VisitNode(*subquery.SubqueryMutable()->node, &block);
			auto type = subquery.GetSubqueryType();
			// EXISTS asks only whether there is a row.
			if (!returned.empty() && type != SubqueryType::EXISTS && type != SubqueryType::NOT_EXISTS) {
				ThrowReturned("a subquery used as a value", returned);
			}
			break;
		}
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
			RefuseStar(expr->Cast<StarExpression>(), block);
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
		if (relation && relation->dataset && !relation->aliased) {
			names.erase(names.begin());
		}
	}

	vector<string> ReadNames(unique_ptr<ParsedExpression> &expr, Block &block) const {
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
		return names;
	}

	//! Whether a FROM clause may have a column of the name that is not a measure. Only of a model relation
	//! are the columns known here.
	static bool MayHaveColumn(const Block &block, const string &name) {
		if (block.unknown > 0) {
			return true;
		}
		for (auto &relation : block.relations) {
			if (relation->dataset && relation->dataset->FindField(name)) {
				return true;
			}
		}
		return false;
	}

	//! The measure a column reference names, if it names one.
	Reference Resolve(const vector<string> &names, Block &block) const {
		Reference result;
		if (names.size() > 1) {
			auto relation = FindRelation(&block, names[0]);
			if (relation) {
				result.measure = FindColumn(*relation, names[1]);
				result.relation = relation;
				result.outer = FindOwn(block, names[0]) != relation;
				result.member = names.size() > 2;
				return result;
			}
			if (block.names.find(names[0]) != block.names.end()) {
				return result;
			}
		}
		// An unqualified name, or the first part of a path into a column: measure.member.
		auto &name = names[0];
		result.member = names.size() > 1;
		for (auto &relation : block.relations) {
			auto measure = FindColumn(*relation, name);
			if (!measure) {
				continue;
			}
			if (result.measure) {
				result.other = relation.get();
				return result;
			}
			result.relation = relation.get();
			result.measure = measure;
		}
		if (result.measure) {
			return result;
		}
		for (auto inner = &block; inner->parent && !MayHaveColumn(*inner, name); inner = inner->parent) {
			for (auto &relation : inner->parent->relations) {
				auto measure = FindColumn(*relation, name);
				if (measure) {
					result.relation = relation.get();
					result.measure = measure;
					result.outer = true;
					return result;
				}
			}
		}
		return result;
	}

	[[noreturn]] void ThrowMisused(const Relation &relation, const string &measure, Clause clause) const {
		string tail;
		switch (clause) {
		case Clause::WHERE:
			tail = StringUtil::Format(". WHERE reads the rows before they are grouped; a condition on the "
			                          "measure's value goes in HAVING, as AGG(%s)",
			                          measure);
			break;
		case Clause::JOIN_ON:
			tail = ", so it cannot be joined on";
			break;
		case Clause::GROUP_BY:
			tail = ", so it cannot be a GROUP BY key";
			break;
		case Clause::HAVING:
		case Clause::ORDER_BY:
			tail = StringUtil::Format(": write AGG(%s)", measure);
			break;
		case Clause::PIVOT:
			tail = ", so PIVOT cannot read it. Evaluate it with AGG in a subquery, and pivot the value";
			break;
		default:
			tail = StringUtil::Format(", so it cannot be an operand or an argument: write AGG(%s). Only on its own, "
			                          "as a column of a SELECT list that does not aggregate, is a measure passed "
			                          "on unevaluated",
			                          measure);
			break;
		}
		throw InvalidInputException("ossie_sql: \"%s\" is a measure of %s, and a measure has a value only where "
		                            "AGG evaluates it%s",
		                            measure, RelationLabel(relation), tail);
	}

	[[noreturn]] void ThrowOuter(const Relation &relation, const string &measure) const {
		throw InvalidInputException("ossie_sql: \"%s\" is a measure of %s, a relation of an outer query block. A "
		                            "measure has a value only where AGG evaluates it, and AGG evaluates a measure "
		                            "of a relation in its own block's FROM clause",
		                            measure, RelationLabel(relation));
	}

	void VisitColumn(unique_ptr<ParsedExpression> &expr, Block &block, Clause clause) {
		auto names = ReadNames(expr, block);
		// ORDER BY reads an output column of that name before a column of FROM.
		if (clause == Clause::ORDER_BY && names.size() == 1 &&
		    block.output_names.find(names[0]) != block.output_names.end()) {
			for (auto &output : block.outputs) {
				if (StringUtil::CIEquals(output.name, names[0])) {
					throw InvalidInputException("ossie_sql: \"%s\" is a measure the SELECT list passes on "
					                            "unevaluated, and a measure has a value only where AGG evaluates "
					                            "it, so ORDER BY cannot read it",
					                            names[0]);
				}
			}
			return;
		}
		auto reference = Resolve(names, block);
		if (!reference.measure) {
			return;
		}
		if (reference.outer) {
			ThrowOuter(*reference.relation, reference.measure->name);
		}
		if (clause == Clause::ORDER_BY && !block.aggregated) {
			throw InvalidInputException("ossie_sql: \"%s\" is a measure of %s, and a measure has a value only where "
			                            "AGG evaluates it. This query block does not aggregate, so its rows cannot "
			                            "be ordered by the measure",
			                            reference.measure->name, RelationLabel(*reference.relation));
		}
		ThrowMisused(*reference.relation, reference.measure->name, clause);
	}

	//! A measure named on its own in a SELECT list that does not aggregate is passed on as it is. Returns
	//! false for a column that is not a measure.
	bool Pass(unique_ptr<ParsedExpression> &expr, Block &block) {
		auto names = ReadNames(expr, block);
		auto reference = Resolve(names, block);
		if (!reference.measure || reference.member) {
			return false;
		}
		auto &relation = *reference.relation;
		auto &measure = *reference.measure;
		if (reference.outer) {
			ThrowOuter(relation, measure.name);
		}
		if (reference.other) {
			throw InvalidInputException("ossie_sql: \"%s\" is ambiguous: %s and %s both have a measure of that "
			                            "name. Qualify it, as %s.%s",
			                            measure.name, RelationLabel(relation), RelationLabel(*reference.other),
			                            relation.name, measure.name);
		}
		if (block.aggregated) {
			auto all = block.select->aggregate_handling == AggregateHandling::FORCE_AGGREGATES;
			string reason = "This query block aggregates, so the measure cannot pass through it unevaluated";
			if (all) {
				reason = "GROUP BY ALL takes every column of the SELECT list that is not an aggregate as a key, "
				         "and a measure cannot be a key";
			}
			throw InvalidInputException("ossie_sql: \"%s\" is a measure of %s, and a measure has a value only "
			                            "where AGG evaluates it. %s: write AGG(%s)",
			                            measure.name, RelationLabel(relation), reason, measure.name);
		}
		measure.needed = true;
		auto output = measure;
		if (expr->HasAlias()) {
			output.name = expr->GetAlias().GetIdentifierName();
		}
		output.starred = false;
		block.outputs.push_back(std::move(output));
		return true;
	}

	static bool Covers(const StarExpression &star, const Relation &relation) {
		auto &qualifier = star.RelationName().GetIdentifierName();
		return qualifier.empty() || StringUtil::CIEquals(qualifier, relation.name);
	}

	static bool Names(const QualifiedColumnName &column, const Relation &relation, const string &name) {
		if (!StringUtil::CIEquals(column.column.GetIdentifierName(), name)) {
			return false;
		}
		return column.table.empty() || StringUtil::CIEquals(column.table.GetIdentifierName(), relation.name);
	}

	//! `*` and `relation.*` take a relation's measures with its fields, and pass them on.
	void PassStar(StarExpression &star, Block &block) {
		for (auto &relation : block.relations) {
			if (!Covers(star, *relation)) {
				continue;
			}
			vector<Carried> passed;
			for (auto &measure : relation->measures) {
				// EXCLUDE, REPLACE and RENAME name a column, so the column has to be there.
				measure.needed = true;
				auto kept = true;
				for (auto &column : star.ExcludeList()) {
					kept = kept && !Names(column, *relation, measure.name);
				}
				// What replaces a measure is an expression, and no longer the measure.
				for (auto &entry : star.ReplaceList()) {
					kept = kept && !StringUtil::CIEquals(entry.first.GetIdentifierName(), measure.name);
				}
				if (!kept) {
					continue;
				}
				auto output = measure;
				for (auto &entry : star.RenameList()) {
					if (Names(entry.first, *relation, measure.name)) {
						output.name = entry.second.GetIdentifierName();
					}
				}
				output.starred = true;
				passed.push_back(std::move(output));
			}
			if (block.aggregated && !passed.empty()) {
				throw InvalidInputException("ossie_sql: * would take in %s of %s, and a measure has a value only "
				                            "where AGG evaluates it. This query block aggregates, so a measure "
				                            "cannot pass through it unevaluated: leave the measures out, as in "
				                            "* EXCLUDE (%s), and evaluate them with AGG",
				                            Listed(passed), RelationLabel(*relation),
				                            StringUtil::Join(MeasureNames(*relation), ", "));
			}
			for (auto &output : passed) {
				block.outputs.push_back(std::move(output));
			}
		}
		for (auto &entry : star.ReplaceListMutable()) {
			Visit(entry.second, block, Clause::SELECT_LIST);
		}
	}

	//! A star anywhere else, and COLUMNS(...), would take a measure as a value.
	void RefuseStar(const StarExpression &star, Block &block) const {
		for (auto &relation : block.relations) {
			if (!Covers(star, *relation) || relation->measures.empty()) {
				continue;
			}
			throw InvalidInputException("ossie_sql: %s over %s is not supported: the relation has measures, and a "
			                            "measure has a value only where AGG evaluates it. Name the columns",
			                            star.IsColumns() ? "COLUMNS(...)" : "* inside an expression",
			                            RelationLabel(*relation));
		}
	}

	[[noreturn]] void ThrowNotAMeasure(const Relation &relation, const string &name) const {
		if (relation.dataset && relation.dataset->FindField(name)) {
			if (FindMeasure(*relation.dataset, name)) {
				throw InvalidInputException("ossie_sql: AGG(%s): dataset \"%s\" declares a field and a measure "
				                            "named \"%s\", and as a column of %s the name is the field. Rename one "
				                            "of them in the model",
				                            name, relation.dataset->name, name, RelationLabel(relation));
			}
			throw InvalidInputException("ossie_sql: AGG(%s): \"%s\" is a field of %s, not a measure. A field takes "
			                            "an ordinary aggregate, such as SUM(%s)",
			                            name, name, RelationLabel(relation), name);
		}
		auto measures = MeasureNames(relation);
		if (measures.empty()) {
			throw InvalidInputException("ossie_sql: AGG(%s): %s has no measure", name, RelationLabel(relation));
		}
		throw InvalidInputException("ossie_sql: AGG(%s): %s has no measure named \"%s\". Its measures: %s", name,
		                            RelationLabel(relation), name, StringUtil::Join(measures, ", "));
	}

	string WhereMeasuresAre() const {
		return StringUtil::Format("A measure is a column of a model relation, such as %s.%s, and of a subquery or "
		                          "a CTE that passes one on",
		                          model.name, model.datasets.empty() ? "dataset" : model.datasets[0].name);
	}

	//! Replaces AGG(measure) with the measure's expression, read from where the relation has the measure.
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
		if (clause == Clause::PIVOT) {
			throw InvalidInputException("ossie_sql: AGG(%s) cannot appear in PIVOT, which aggregates the rows it "
			                            "reads itself. Evaluate the measure in a subquery, and pivot its value",
			                            written);
		}

		Relation *relation = nullptr;
		Carried *measure = nullptr;
		if (names.size() == 2) {
			relation = FindOwn(block, names[0]);
			if (!relation) {
				if (block.names.find(names[0]) != block.names.end()) {
					throw InvalidInputException("ossie_sql: AGG(%s): \"%s\" has no measure. %s", written, names[0],
					                            WhereMeasuresAre());
				}
				if (FindRelation(block.parent, names[0])) {
					throw InvalidInputException("ossie_sql: AGG(%s): \"%s\" is a relation of an outer query block. "
					                            "AGG evaluates a measure of a relation in its own block's FROM "
					                            "clause",
					                            written, names[0]);
				}
				throw InvalidInputException("ossie_sql: AGG(%s): the FROM clause of this query block has no "
				                            "relation named \"%s\"",
				                            written, names[0]);
			}
			measure = FindColumn(*relation, names[1]);
			if (!measure) {
				ThrowNotAMeasure(*relation, names[1]);
			}
		} else {
			if (block.relations.empty()) {
				throw InvalidInputException("ossie_sql: AGG(%s): no relation in the FROM clause of this query block "
				                            "has a measure. %s",
				                            written, WhereMeasuresAre());
			}
			for (auto &candidate : block.relations) {
				auto found = FindColumn(*candidate, names[0]);
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
				measure = found;
			}
			if (!relation) {
				ThrowNotAMeasure(*block.relations[0], names[0]);
			}
		}
		auto &metric = *measure->metric;
		auto window = FindWindow(*metric.expression.tree);
		if (window) {
			throw InvalidInputException("ossie_sql: AGG(%s): measure \"%s\" uses the window function %s, whose "
			                            "partition and order are fixed in the model while the rows it runs over "
			                            "depend on the query. Window measures are not supported yet",
			                            written, metric.name,
			                            StringUtil::Upper(window->FunctionName().GetIdentifierName()));
		}

		if (!HasAggregate(*metric.expression.tree)) {
			throw InvalidInputException("ossie_sql: AGG(%s): measure \"%s\" contains no aggregate, so it has no "
			                            "value over the rows of a group",
			                            written, metric.name);
		}

		auto value = metric.expression.tree->Copy();
		if (block.single_relation && measure->once) {
			// Each row of the block is one row of the measure's dataset, so the measure runs over them as it is.
			Read(value, *relation, *measure, false);
		} else {
			Read(value, *relation, *measure, true);
			value = OverDistinctRows(std::move(value), *relation, *measure, written);
		}
		if (expr->HasAlias()) {
			value->SetAlias(expr->GetAlias());
		} else if (name_measure) {
			value->SetAlias(Identifier(names.back()));
		}
		value->SetQueryLocation(expr->GetQueryLocation());
		expr = std::move(value);
	}

	//! Points the columns a measure reads at where the query has them. A model relation has its fields as
	//! columns; everything else is read out of the measure's column, which carries it. Unpacked, the columns
	//! are those of the rows OverDistinctRows lays out.
	void Read(unique_ptr<ParsedExpression> &expr, Relation &relation, Carried &measure, bool unpacked) {
		if (expr->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
			auto visit = [&](unique_ptr<ParsedExpression> &child) {
				Read(child, relation, measure, unpacked);
			};
			ParsedExpressionIterator::EnumerateChildren(*expr, visit);
			return;
		}
		auto names = ColumnNames(expr->Cast<ColumnRefExpression>());
		auto member = MemberOf(*measure.dataset, names.back(), names.size() == 3);
		if (unpacked) {
			expr = Column(member.name, relation.name);
		} else if (relation.dataset && member.field) {
			expr = Column(member.field->name, relation.name);
		} else {
			measure.needed = true;
			expr = MemberValue(Column(measure.name, relation.name), member.name);
		}
	}

	//! Where a row of the measure's dataset may stand in several rows of a group, or in none that is its own
	//! (an outer join pads a row that matched nothing), the measure is evaluated over the distinct rows of its
	//! dataset in the group, told apart by the primary key:
	//!
	//!   (SELECT <measure> FROM (SELECT unnest(list(DISTINCT <measure's column>) FILTER (<key> IS NOT NULL))
	//!                           AS <relation>))
	//!
	//! The list names only columns of the enclosing block, which makes it an aggregate of that block: it is
	//! computed once for each of the block's groups, whatever the block groups by.
	unique_ptr<ParsedExpression> OverDistinctRows(unique_ptr<ParsedExpression> value, Relation &relation,
	                                              Carried &measure, const string &written) {
		auto &dataset = *measure.dataset;
		if (dataset.primary_key.empty()) {
			throw InvalidInputException("ossie_sql: AGG(%s): dataset \"%s\" declares no primary_key. Where a row of "
			                            "the dataset may stand in more than one row, as over a join, a measure "
			                            "counts each row of its dataset once, and the primary key is what tells "
			                            "the rows apart",
			                            written, dataset.name);
		}
		measure.needed = true;
		vector<unique_ptr<ParsedExpression>> present;
		for (auto &key : dataset.primary_key) {
			auto member = MemberOf(dataset, key, false);
			present.push_back(make_uniq<OperatorExpression>(
			    ExpressionType::OPERATOR_IS_NOT_NULL, MemberValue(Column(measure.name, relation.name), member.name)));
		}
		unique_ptr<ParsedExpression> filter;
		if (present.size() == 1) {
			filter = std::move(present[0]);
		} else {
			filter = make_uniq<ConjunctionExpression>(ExpressionType::CONJUNCTION_AND, std::move(present));
		}
		vector<unique_ptr<ParsedExpression>> row;
		row.push_back(Column(measure.name, relation.name));
		vector<unique_ptr<ParsedExpression>> rows;
		rows.push_back(
		    make_uniq<FunctionExpression>(Identifier("list"), std::move(row), std::move(filter), nullptr, true));
		auto unnest = make_uniq<FunctionExpression>(Identifier("unnest"), std::move(rows));
		unnest->SetAlias(Identifier(relation.name));

		auto distinct_rows = make_uniq<SelectNode>();
		distinct_rows->select_list.push_back(std::move(unnest));
		distinct_rows->from_table = make_uniq<EmptyTableRef>();
		auto distinct_statement = make_uniq<SelectStatement>();
		distinct_statement->node = std::move(distinct_rows);

		auto evaluation = make_uniq<SelectNode>();
		evaluation->select_list.push_back(std::move(value));
		evaluation->from_table = make_uniq<SubqueryRef>(std::move(distinct_statement));
		auto statement = make_uniq<SelectStatement>();
		statement->node = std::move(evaluation);

		auto result = make_uniq<SubqueryExpression>();
		result->GetSubqueryTypeMutable() = SubqueryType::SCALAR;
		result->SubqueryMutable() = std::move(statement);
		return std::move(result);
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
	if (parser.statements[0]->type == StatementType::MULTI_STATEMENT) {
		// PIVOT without an IN list is several statements: the values are collected first.
		throw InvalidInputException("ossie_sql: the query must be one statement, and a PIVOT that does not list "
		                            "the values to pivot on is several. List them, as in ON column IN (...)");
	}
	if (parser.statements[0]->type != StatementType::SELECT_STATEMENT) {
		throw InvalidInputException("ossie_sql: the query must be a SELECT; ossie_sql only reads");
	}
	auto statement = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	RelationalCompiler compiler(context, model);
	auto carried = compiler.VisitNode(*statement->node, nullptr);
	if (!carried.empty()) {
		// Whether a query may return a measure unevaluated is left open; here nothing could evaluate it.
		ThrowReturned("the query", carried);
	}
	return statement;
}

} // namespace ossie
} // namespace duckdb
