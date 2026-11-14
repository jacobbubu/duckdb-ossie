#include "ossie/fact_program.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/between_expression.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/window_expression.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/tableref/joinref.hpp"

namespace duckdb {
namespace ossie {
namespace {
constexpr idx_t RULE_BYTES = 16384, TOTAL_BYTES = 131072, NODE_LIMIT = 1024, TOTAL_NODES = 32768;
[[noreturn]] void Reject(const string &why) {
	throw InvalidInputException("ossie_fact_program: %s", why);
}
string Q(const string &s) {
	return "\"" + StringUtil::Replace(s, "\"", "\"\"") + "\"";
}
void Name(const string &s) {
	if (s.empty() || s.size() > 128) {
		Reject("bounded ASCII identifier required");
	}
	for (idx_t i = 0; i < s.size(); i++) {
		auto c = s[i];
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (i > 0 && c >= '0' && c <= '9'))) {
			Reject("unquoted ASCII identifier required");
		}
	}
}
struct Budget {
	idx_t nodes = 0;
	void Charge(idx_t n = 1) {
		if (n > TOTAL_NODES - nodes) {
			Reject("expanded program node budget");
		}
		nodes += n;
	}
};
idx_t Nodes(const ParsedExpression &e, idx_t depth = 1) {
	if (depth > 32) {
		Reject("expression depth exceeds32");
	}
	idx_t n = 1;
	ParsedExpressionIterator::EnumerateChildren(e, [&](const ParsedExpression &c) {
		auto size = Nodes(c, depth + 1);
		if (size > NODE_LIMIT - n) {
			Reject("expression nodes exceed1024");
		}
		n += size;
	});
	return n;
}
unique_ptr<ParsedExpression> Expression(const string &sql) {
	if (sql.empty() || sql.size() > RULE_BYTES || sql.find('\0') != string::npos) {
		Reject("rule bytes or NUL");
	}
	if (sql.find('"') != string::npos) {
		Reject("quoted identifiers unsupported");
	}
	auto exprs = Parser::ParseExpressionList(sql);
	if (exprs.size() != 1 || !exprs[0]->GetAlias().empty()) {
		Reject("one unaliased expression required");
	}
	Nodes(*exprs[0]);
	return std::move(exprs[0]);
}
const FactField &Field(const FactSource &s, const string &name) {
	for (auto &f : s.fields) {
		if (StringUtil::CIEquals(f.name, name)) {
			return f;
		}
	}
	Reject("undeclared mapped field: " + s.dataset + "." + name);
}
unique_ptr<ParsedExpression> Column(const FactSource &s, const FactField &f) {
	return make_uniq<ColumnRefExpression>(f.physical_column, s.dataset);
}
pair<idx_t, const FactField *> SourceField(const FactProgram &p, const ParsedExpression &e) {
	if (e.GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		Reject("aggregate/window value must be declared source field");
	}
	auto &names = e.Cast<ColumnRefExpression>().column_names;
	if (names.size() != 2) {
		Reject("explicit dataset.field required");
	}
	for (idx_t i = 0; i < p.sources.size(); i++) {
		if (StringUtil::CIEquals(p.sources[i].dataset, names[0])) {
			return {i, &Field(p.sources[i], names[1])};
		}
	}
	Reject("undeclared source dataset");
}
string AmountUnit(const FactSource &s) {
	return "currency:" + s.currency + ":scale2";
}
void PlainFunction(const FunctionExpression &f) {
	if (!f.catalog.empty() || !f.schema.empty() || f.distinct || f.filter ||
	    (f.order_bys && !f.order_bys->orders.empty()) || f.export_state) {
		Reject("aggregate/operator modifiers or qualified functions unsupported");
	}
}
unique_ptr<SelectStatement> SQL(const string &sql) {
	Parser parser;
	parser.ParseQuery(sql);
	if (parser.statements.size() != 1 || parser.statements[0]->type != StatementType::SELECT_STATEMENT) {
		Reject("internal single select invariant");
	}
	return unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
}
struct Binder {
	const FactProgramInput &input;
	FactProgram program;
	Budget budget;
	case_insensitive_map_t<idx_t> indices;
	vector<uint8_t> state;
	vector<idx_t> sizes, dependency_depths;
	explicit Binder(const FactProgramInput &i) : input(i) {
	}
	idx_t Metric(idx_t index, idx_t depth = 1) {
		if (depth > 16) {
			Reject("metric dependency depth exceeds16");
		}
		if (state[index] == 1) {
			Reject("metric dependency cycle");
		}
		if (state[index] == 2) {
			return index;
		}
		state[index] = 1;
		auto tree = Expression(input.metrics[index].sql);
		string unit;
		idx_t expanded = 0, dependency_depth = 1;
		auto bound = Bind(*tree, unit, depth, expanded, dependency_depth);
		if (dependency_depth > 16) {
			Reject("metric dependency depth exceeds16");
		}
		dependency_depths[index] = dependency_depth;
		auto n = Nodes(*bound);
		program.metrics[index] = {input.metrics[index].name, unit, std::move(bound)};
		sizes[index] = n;
		state[index] = 2;
		return index;
	}
	unique_ptr<ParsedExpression> Bind(const ParsedExpression &e, string &unit, idx_t depth, idx_t &expanded,
	                                  idx_t &dependency_depth) {
		if (e.GetExpressionClass() == ExpressionClass::COLUMN_REF) {
			auto &names = e.Cast<ColumnRefExpression>().column_names;
			if (names.size() != 1) {
				Reject("bare metric reference required outside aggregate");
			}
			auto it = indices.find(names[0]);
			if (it == indices.end()) {
				Reject("unknown metric reference");
			}
			auto index = Metric(it->second, depth + 1);
			dependency_depth = MaxValue(dependency_depth, dependency_depths[index] + 1);
			if (dependency_depth > 16 || sizes[index] > NODE_LIMIT - expanded) {
				Reject("expanded metric depth/node budget");
			}
			expanded += sizes[index];
			budget.Charge(sizes[index]);
			unit = program.metrics[index].unit;
			return program.metrics[index].expression->Copy();
		}
		if (expanded >= NODE_LIMIT) {
			Reject("expanded metric node budget");
		}
		expanded++;
		if (e.GetExpressionClass() != ExpressionClass::FUNCTION) {
			Reject("only SUM/COUNT and same-unit metric +/- supported");
		}
		auto &f = e.Cast<FunctionExpression>();
		PlainFunction(f);
		auto name = StringUtil::Lower(f.function_name);
		if (f.is_operator) {
			if ((name != "+" && name != "-") || f.children.size() != 2) {
				Reject("only binary +/- supported");
			}
			string left, right;
			auto a = Bind(*f.children[0], left, depth, expanded, dependency_depth),
			     b = Bind(*f.children[1], right, depth, expanded, dependency_depth);
			if (left != right || left.find("currency:") != 0) {
				Reject("arithmetic requires equal currency/scale money components, not counts");
			}
			budget.Charge();
			unit = left;
			vector<unique_ptr<ParsedExpression>> children;
			children.push_back(std::move(a));
			children.push_back(std::move(b));
			return make_uniq<FunctionExpression>("system", "main", name, std::move(children));
		}
		if ((name != "sum" && name != "count") || f.children.size() != 1) {
			Reject("only single-field SUM/COUNT supported");
		}
		auto field = SourceField(program, *f.children[0]);
		auto &source = program.sources[field.first];
		if (name == "sum" && !StringUtil::CIEquals(field.second->name, source.amount_field)) {
			Reject("SUM requires mapped amount");
		}
		if (name == "count" && !(StringUtil::CIEquals(field.second->name, source.amount_field) ||
		                         StringUtil::CIEquals(field.second->name, source.user_field))) {
			Reject("COUNT supports amount value or nonnull User identity only");
		}
		unit = name == "sum" ? AmountUnit(source) : "count:" + source.concept + ":" + field.second->name;
		auto column = Column(source, *field.second);
		vector<unique_ptr<ParsedExpression>> children;
		children.push_back(std::move(column));
		auto aggregate = make_uniq<FunctionExpression>("system", "main", name, std::move(children));
		idx_t index = program.aggregates.size();
		for (idx_t i = 0; i < program.aggregates.size(); i++) {
			if (program.aggregates[i].expression->Equals(*aggregate)) {
				index = i;
				break;
			}
		}
		if (index == program.aggregates.size()) {
			if (index >= 64) {
				Reject("aggregate component limit64");
			}
			budget.Charge(2);
			program.aggregates.push_back({field.first, field.second->name, name, unit, std::move(aggregate)});
		}
		budget.Charge();
		return make_uniq<ColumnRefExpression>("c" + to_string(index), "f" + to_string(field.first));
	}
	void Window(const FactDefinition &definition) {
		auto e = Expression(definition.sql);
		if (e->GetExpressionClass() != ExpressionClass::WINDOW) {
			Reject("one window expression required");
		}
		auto &w = e->Cast<WindowExpression>();
		if (!w.catalog.empty() || !w.schema.empty() || w.distinct || w.filter_expr || w.ignore_nulls ||
		    !w.arg_orders.empty() || w.exclude_clause != WindowExcludeMode::NO_OTHER || w.children.size() != 1 ||
		    w.partitions.size() != 1 || w.orders.size() != 1) {
			Reject("window shape/modifiers unsupported");
		}
		auto field = SourceField(program, *w.children[0]);
		auto &s = program.sources[field.first];
		if (!s.product_field.empty() || !StringUtil::CIEquals(field.second->name, s.amount_field)) {
			Reject("window requires mapped amount at User/date grain");
		}
		auto part = SourceField(program, *w.partitions[0]), order = SourceField(program, *w.orders[0].expression);
		if (part.first != field.first || order.first != field.first ||
		    !StringUtil::CIEquals(part.second->name, s.user_field) ||
		    !StringUtil::CIEquals(order.second->name, s.date_field)) {
			Reject("window requires same mapped User partition and unique DATE order");
		}
		auto ot = w.orders[0].type;
		auto nt = w.orders[0].null_order;
		if ((ot != OrderType::ASCENDING && ot != OrderType::ORDER_DEFAULT) ||
		    (nt != OrderByNullType::ORDER_DEFAULT && nt != OrderByNullType::NULLS_LAST)) {
			Reject("ascending finite DATE order required");
		}
		auto integral = [&](const unique_ptr<ParsedExpression> &p, int64_t lo, int64_t hi) {
			if (!p || p->GetExpressionClass() != ExpressionClass::CONSTANT) {
				Reject("literal bounded integral required");
			}
			auto &v = p->Cast<ConstantExpression>().value;
			if (v.IsNull() || !v.type().IsIntegral()) {
				Reject("literal bounded integral required");
			}
			Value represented;
			string error;
			if (!v.DefaultTryCastAs(LogicalType::BIGINT, represented, &error, true)) {
				Reject("window literal outside BIGINT");
			}
			auto n = represented.GetValue<int64_t>();
			if (n < lo || n > hi) {
				Reject("window offset/frame range");
			}
		};
		auto name = StringUtil::Lower(w.function_name);
		if (w.GetExpressionType() == ExpressionType::WINDOW_LAG && name == "lag") {
			integral(w.offset_expr, 1, 1);
			if (!w.default_expr || w.default_expr->GetExpressionClass() != ExpressionClass::CONSTANT ||
			    !w.default_expr->Cast<ConstantExpression>().value.IsNull()) {
				Reject("LAG requires explicit NULL default");
			}
			if (w.start != WindowBoundary::UNBOUNDED_PRECEDING || w.end != WindowBoundary::CURRENT_ROW_RANGE ||
			    w.start_expr || w.end_expr) {
				Reject("LAG default frame only");
			}
		} else if (w.GetExpressionType() == ExpressionType::WINDOW_AGGREGATE && name == "sum") {
			if (w.start != WindowBoundary::EXPR_PRECEDING_ROWS || w.end != WindowBoundary::CURRENT_ROW_ROWS ||
			    w.end_expr || w.offset_expr || w.default_expr) {
				Reject("SUM requires explicit bounded preceding ROWS to current row");
			}
			integral(w.start_expr, 0, 366);
		} else {
			Reject("only LAG(value,1,NULL) and SUM ROWS windows supported");
		}
		budget.Charge(Nodes(*e));
		auto copy = e->Copy();
		auto &bound = copy->Cast<WindowExpression>();
		bound.children[0] = Column(s, *field.second);
		bound.partitions[0] = Column(s, *part.second);
		bound.orders[0].expression = Column(s, *order.second);
		bound.orders[0].type = OrderType::ASCENDING;
		bound.orders[0].null_order = OrderByNullType::NULLS_LAST;
		bound.catalog = "system";
		bound.schema = "main";
		program.windows.push_back({definition.name, field.first, AmountUnit(s), std::move(copy)});
	}
};
const FactMetric &FindMetric(const FactProgram &p, const string &name) {
	for (auto &m : p.metrics) {
		if (StringUtil::CIEquals(m.name, name)) {
			return m;
		}
	}
	Reject("unknown selected metric");
}
string Cohort(const FactProgram &p, const FactCohort &c, idx_t depth, idx_t &nodes) {
	if (depth > 16 || ++nodes > 64) {
		Reject("cohort depth/node budget");
	}
	if (c.operation == "all") {
		if (!c.predicate.empty() || !c.children.empty()) {
			Reject("all cohort must be empty");
		}
		return "TRUE";
	}
	if (c.operation == "predicate") {
		if (!c.children.empty()) {
			Reject("predicate has no children");
		}
		auto e = Expression(c.predicate);
		if (e->GetExpressionClass() != ExpressionClass::COMPARISON) {
			Reject("one metric comparison required");
		}
		auto &cmp = e->Cast<ComparisonExpression>();
		switch (e->GetExpressionType()) {
		case ExpressionType::COMPARE_EQUAL:
		case ExpressionType::COMPARE_NOTEQUAL:
		case ExpressionType::COMPARE_GREATERTHAN:
		case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
		case ExpressionType::COMPARE_LESSTHAN:
		case ExpressionType::COMPARE_LESSTHANOREQUALTO:
			break;
		default:
			Reject("comparison unsupported");
		}
		if (cmp.left->GetExpressionClass() != ExpressionClass::COLUMN_REF ||
		    cmp.left->Cast<ColumnRefExpression>().column_names.size() != 1 ||
		    cmp.right->GetExpressionClass() != ExpressionClass::CONSTANT) {
			Reject("metric compared to typed numeric constant required");
		}
		auto &metric = FindMetric(p, cmp.left->Cast<ColumnRefExpression>().column_names[0]);
		auto &v = cmp.right->Cast<ConstantExpression>().value;
		if (v.IsNull() || !(v.type().IsIntegral() || v.type().id() == LogicalTypeId::DECIMAL)) {
			Reject("finite exact numeric constant required");
		}
		if (metric.unit.find("count:") == 0 && !v.type().IsIntegral()) {
			Reject("count comparison requires integer constant");
		}
		if (v.type().id() == LogicalTypeId::DECIMAL && DecimalType::GetScale(v.type()) > 2) {
			Reject("comparison amount scale exceeds2");
		}
		Value represented;
		string error;
		if (!v.DefaultTryCastAs(metric.unit.find("currency:") == 0 ? LogicalType::DECIMAL(38, 2) : LogicalType::BIGINT,
		                        represented, &error, true)) {
			Reject("comparison constant outside measure type");
		}
		cmp.right = make_uniq<ConstantExpression>(std::move(represented));
		cmp.left = make_uniq<ColumnRefExpression>(metric.name, "combined");
		return "(" + e->ToString() + ") IS TRUE";
	}
	if (!c.predicate.empty() || c.children.size() != 2 ||
	    (c.operation != "intersection" && c.operation != "union" && c.operation != "difference")) {
		Reject("bounded binary set operator required");
	}
	auto a = Cohort(p, c.children[0], depth + 1, nodes), b = Cohort(p, c.children[1], depth + 1, nodes);
	return "(" + a + (c.operation == "union" ? " OR " : " AND ") + (c.operation == "difference" ? "NOT (" : "(") + b +
	       "))";
}
vector<string> SelectNames(const vector<string> &names) {
	if (names.empty() || names.size() > 32) {
		Reject("select1..32 declared names");
	}
	case_insensitive_set_t unique;
	for (auto &n : names) {
		Name(n);
		if (!unique.insert(n).second) {
			Reject("duplicate selected name");
		}
	}
	return names;
}
} // namespace

FactProgram BindFactProgram(const FactProgramInput &input) {
	Name(input.user_concept);
	Name(input.user_identity_relationship);
	if (input.sources.size() != 2 || input.metrics.empty() || input.metrics.size() > 32 || input.windows.size() > 16) {
		Reject("two facts,1..32 metrics,0..16 windows required");
	}
	Binder binder(input);
	auto &p = binder.program;
	p.user_concept = input.user_concept;
	p.user_identity_relationship = input.user_identity_relationship;
	case_insensitive_set_t datasets, concepts;
	for (auto &s : input.sources) {
		Name(s.dataset);
		Name(s.concept);
		Name(s.user_field);
		Name(s.date_field);
		Name(s.amount_field);
		if (!datasets.insert(s.dataset).second || !concepts.insert(s.concept).second) {
			Reject("distinct fact datasets and roots required");
		}
		if (!StringUtil::CIEquals(s.user_concept, input.user_concept) ||
		    !StringUtil::CIEquals(s.target_identity_relationship, input.user_identity_relationship)) {
			Reject("both facts must map same declared User identity");
		}
		if (s.currency.size() != 3) {
			Reject("explicit three-letter currency required");
		}
		for (auto c : s.currency) {
			if (c < 'A' || c > 'Z') {
				Reject("explicit uppercase currency required");
			}
		}
		vector<string> identity = {s.user_field, s.date_field};
		if (!s.product_field.empty()) {
			Name(s.product_field);
			identity.push_back(s.product_field);
		}
		if (s.identity_fields.size() != identity.size()) {
			Reject("full declared source identity required");
		}
		for (idx_t i = 0; i < identity.size(); i++) {
			if (!StringUtil::CIEquals(s.identity_fields[i], identity[i])) {
				Reject("ordered User/date[/product] identity required");
			}
		}
		if (s.fields.empty() || s.fields.size() > 256) {
			Reject("field budget");
		}
		case_insensitive_set_t fields;
		for (auto &f : s.fields) {
			Name(f.name);
			if (f.physical_column.empty() || f.physical_column.size() > 256 ||
			    f.physical_column.find('\0') != string::npos) {
				Reject("bounded physical column required");
			}
			if (!fields.insert(f.name).second) {
				Reject("duplicate declared field");
			}
		}
		if (Field(s, s.user_field).type != LogicalType::VARCHAR || Field(s, s.date_field).type != LogicalType::DATE ||
		    Field(s, s.amount_field).type != LogicalType::DECIMAL(18, 2) ||
		    (!s.product_field.empty() && Field(s, s.product_field).type != LogicalType::VARCHAR)) {
			Reject("actual role types must be VARCHAR/DATE[/VARCHAR]/DECIMAL18,2");
		}
		case_insensitive_set_t roles;
		for (auto &n : identity) {
			if (!roles.insert(Field(s, n).physical_column).second) {
				Reject("identity roles cannot share physical column");
			}
		}
		if (!roles.insert(Field(s, s.amount_field).physical_column).second) {
			Reject("amount cannot alias identity column");
		}
	}
	if (input.sources[0].product_field.empty() == input.sources[1].product_field.empty()) {
		Reject("different User/date and User/date/product grains required");
	}
	p.sources = input.sources;
	idx_t bytes = 0;
	case_insensitive_set_t allnames;
	for (auto &d : input.metrics) {
		Name(d.name);
		if (StringUtil::CIEquals(d.name, "user_id") || StringUtil::CIEquals(d.name, "ds") ||
		    !allnames.insert(d.name).second) {
			Reject("duplicate program name");
		}
		if (d.sql.size() > RULE_BYTES || d.sql.size() > TOTAL_BYTES - bytes) {
			Reject("program rule bytes");
		}
		bytes += d.sql.size();
		binder.indices.emplace(d.name, binder.indices.size());
	}
	for (auto &d : input.windows) {
		Name(d.name);
		if (StringUtil::CIEquals(d.name, "user_id") || StringUtil::CIEquals(d.name, "ds") ||
		    !allnames.insert(d.name).second) {
			Reject("duplicate program name");
		}
		if (d.sql.size() > RULE_BYTES || d.sql.size() > TOTAL_BYTES - bytes) {
			Reject("program rule bytes");
		}
		bytes += d.sql.size();
	}
	binder.state.resize(input.metrics.size());
	binder.sizes.resize(input.metrics.size());
	binder.dependency_depths.resize(input.metrics.size());
	p.metrics.resize(input.metrics.size());
	for (idx_t i = 0; i < input.metrics.size(); i++) {
		binder.Metric(i);
	}
	for (auto &d : input.windows) {
		binder.Window(d);
	}
	return std::move(p);
}

unique_ptr<SelectStatement> CompileFactCommonCount(const FactProgram &p, vector<unique_ptr<TableRef>> sources) {
	if (sources.size() != 2 || !sources[0] || !sources[1]) {
		Reject("two source ASTs required for common count");
	}
	auto statement = SQL("SELECT COUNT(*) AS common_user_count FROM (SELECT 1) AS f0 INNER JOIN (SELECT 1) AS f1 ON "
	                     "f0.user_id=f1.user_id");
	auto &count = statement->node->Cast<SelectNode>().select_list[0]->Cast<FunctionExpression>();
	count.catalog = "system";
	count.schema = "main";
	auto &join = statement->node->Cast<SelectNode>().from_table->Cast<JoinRef>();
	for (idx_t i = 0; i < 2; i++) {
		auto &s = p.sources[i];
		auto user = Q(s.dataset) + "." + Q(Field(s, s.user_field).physical_column);
		auto reduction = SQL("SELECT " + user + " AS user_id FROM placeholder GROUP BY " + user);
		sources[i]->alias = s.dataset;
		reduction->node->Cast<SelectNode>().from_table = std::move(sources[i]);
		auto table = make_uniq<SubqueryRef>(std::move(reduction), "f" + to_string(i));
		if (i == 0) {
			join.left = std::move(table);
		} else {
			join.right = std::move(table);
		}
	}
	return statement;
}

unique_ptr<SelectStatement> CompileFactUsers(const FactProgram &p, const vector<string> &names, const string &domain,
                                             const FactCohort &cohort, vector<unique_ptr<TableRef>> sources) {
	SelectNames(names);
	if (domain != "common" || sources.size() != 2 || !sources[0] || !sources[1]) {
		Reject("explicit common domain and two source ASTs required");
	}
	string sql = "SELECT ";
	for (idx_t i = 0; i < p.metrics.size(); i++) {
		if (i) {
			sql += ",";
		}
		sql += p.metrics[i].expression->ToString() + " AS " + Q(p.metrics[i].name);
	}
	sql += ", f0.user_id AS user_id FROM ";
	auto statement = SQL("SELECT * FROM (SELECT 1) AS f0 INNER JOIN (SELECT 1) AS f1 ON f0.user_id=f1.user_id");
	auto &join = statement->node->Cast<SelectNode>().from_table->Cast<JoinRef>();
	for (idx_t i = 0; i < 2; i++) {
		auto &s = p.sources[i];
		string reduced = "SELECT " + Q(s.dataset) + "." + Q(Field(s, s.user_field).physical_column) + " AS user_id";
		for (idx_t a = 0; a < p.aggregates.size(); a++) {
			if (p.aggregates[a].source == i) {
				reduced += "," + p.aggregates[a].expression->ToString() + " AS c" + to_string(a);
			}
		}
		reduced += " FROM placeholder GROUP BY " + Q(s.dataset) + "." + Q(Field(s, s.user_field).physical_column);
		auto reduction = SQL(reduced);
		sources[i]->alias = s.dataset;
		reduction->node->Cast<SelectNode>().from_table = std::move(sources[i]);
		auto table = make_uniq<SubqueryRef>(std::move(reduction), "f" + to_string(i));
		if (i == 0) {
			join.left = std::move(table);
		} else {
			join.right = std::move(table);
		}
	}
	auto combined = SQL(sql + "placeholder");
	combined->node->Cast<SelectNode>().from_table = std::move(statement->node->Cast<SelectNode>().from_table);
	idx_t nodes = 0;
	auto where = Cohort(p, cohort, 1, nodes);
	string projection = "SELECT combined.user_id";
	for (auto &name : names) {
		projection += ",combined." + Q(FindMetric(p, name).name);
	}
	auto result =
	    SQL(projection + " FROM placeholder AS combined WHERE " + where + " ORDER BY combined.user_id ASC NULLS LAST");
	result->node->Cast<SelectNode>().from_table = make_uniq<SubqueryRef>(std::move(combined), "combined");
	return result;
}

unique_ptr<SelectStatement> CompileFactWindows(const FactProgram &p, const vector<string> &names, const string &scope,
                                               const Value &start, const Value &end,
                                               vector<unique_ptr<TableRef>> sources) {
	SelectNames(names);
	if (scope != "entire_capture" || sources.size() != 2 || !sources[0] || !sources[1]) {
		Reject("explicit whole-capture window input and two source ASTs required");
	}
	optional_idx source_index;
	string projection;
	vector<unique_ptr<ParsedExpression>> window_trees;
	for (auto &name : names) {
		const FactWindow *window = nullptr;
		for (auto &w : p.windows) {
			if (StringUtil::CIEquals(w.name, name)) {
				window = &w;
			}
		}
		if (!window) {
			Reject("unknown window");
		}
		if (source_index.IsValid() && source_index.GetIndex() != window->source) {
			Reject("window selection cannot mix fact grains");
		}
		source_index = window->source;
		projection += ",0 AS " + Q(window->name);
		auto tree = window->expression->Copy();
		tree->alias = window->name;
		window_trees.push_back(std::move(tree));
	}
	auto i = source_index.GetIndex();
	auto &s = p.sources[i];
	auto inner =
	    SQL("SELECT " + Q(s.dataset) + "." + Q(Field(s, s.user_field).physical_column) + " AS user_id," + Q(s.dataset) +
	        "." + Q(Field(s, s.date_field).physical_column) + " AS ds" + projection + " FROM placeholder");
	// DuckDB 1.5.6 WindowExpression::ToString omits catalog. Keep the typed
	// tree rather than reparsing rendered windows and losing system binding.
	for (idx_t w = 0; w < window_trees.size(); w++) {
		inner->node->Cast<SelectNode>().select_list[w + 2] = std::move(window_trees[w]);
	}
	sources[i]->alias = s.dataset;
	inner->node->Cast<SelectNode>().from_table = std::move(sources[i]);
	string out = "SELECT observed.user_id,observed.ds";
	for (auto &name : names) {
		out += ",observed." + Q(name);
	}
	out += " FROM placeholder AS observed";
	if (!start.IsNull() || !end.IsNull()) {
		if (start.IsNull() || end.IsNull() || start.type() != LogicalType::DATE || end.type() != LogicalType::DATE) {
			Reject("display DATE endpoints both required");
		}
		auto a = start.GetValue<date_t>(), b = end.GetValue<date_t>();
		if (!Date::IsFinite(a) || !Date::IsFinite(b) || a.days > b.days) {
			Reject("finite ordered display DATE required");
		}
	}
	auto result = SQL(out + " ORDER BY observed.user_id ASC NULLS LAST,observed.ds ASC NULLS LAST");
	if (!start.IsNull()) {
		// Preserve typed DATE values: the isolated finite INT32_MIN has a canonical
		// rendering that DuckDB 1.5.6 cannot parse back as a textual DATE cast.
		result->node->Cast<SelectNode>().where_clause =
		    make_uniq<BetweenExpression>(make_uniq<ColumnRefExpression>("ds", "observed"),
		                                 make_uniq<ConstantExpression>(start), make_uniq<ConstantExpression>(end));
	}
	result->node->Cast<SelectNode>().from_table = make_uniq<SubqueryRef>(std::move(inner), "observed");
	return result;
}
} // namespace ossie
} // namespace duckdb
