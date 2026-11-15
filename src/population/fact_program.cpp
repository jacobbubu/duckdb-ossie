#include "ossie/fact_program.hpp"
#include "ossie/names.hpp"
#include "ossie/compile.hpp"
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

namespace duckdb {
namespace ossie {
namespace {
constexpr idx_t RULE_BYTES = 16384, TOTAL_BYTES = 131072, NODE_LIMIT = 1024, TOTAL_NODES = 32768;
[[noreturn]] void Reject(const string &why) {
	throw InvalidInputException("ossie_program: %s", why);
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
	auto exprs = Parser::GetBuiltinParser().ParseExpressionList(sql);
	if (exprs.size() != 1 || !exprs[0]->GetAlias().GetIdentifierName().empty()) {
		Reject("one unaliased expression required");
	}
	Nodes(*exprs[0]);
	return std::move(exprs[0]);
}
const FactField &MappedField(const FactSource &s, const string &name) {
	for (auto &f : s.fields) {
		if (StringUtil::CIEquals(f.name, name)) {
			return f;
		}
	}
	Reject("undeclared mapped field: " + s.dataset + "." + name);
}
unique_ptr<ParsedExpression> Column(const FactSource &s, const FactField &f) {
	return make_uniq<ColumnRefExpression>(Identifier(f.physical_column), Identifier(s.dataset));
}
unique_ptr<ParsedExpression> DateConstant(const Value &value) {
	// DuckDB renders the isolated finite INT32_MIN day but refuses to parse it back, so reach it by arithmetic.
	auto day = value.GetValue<date_t>();
	if (day.days != NumericLimits<int32_t>::Minimum()) {
		return ConstantExpression::FromValue(value);
	}
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(ConstantExpression::FromValue(Value::DATE(date_t(0))));
	children.push_back(ConstantExpression::FromValue(Value::INTEGER(day.days)));
	return make_uniq<FunctionExpression>(QualifiedName("system", "main", "+"), std::move(children), nullptr, nullptr,
	                                     false, true);
}
pair<idx_t, const FactField *> SourceField(const FactProgram &p, const ParsedExpression &e) {
	if (e.GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		Reject("aggregate/window value must be declared source field");
	}
	auto names = ColumnNames(e.Cast<ColumnRefExpression>());
	if (names.size() != 2) {
		Reject("explicit dataset.field required");
	}
	for (idx_t i = 0; i < p.sources.size(); i++) {
		if (StringUtil::CIEquals(p.sources[i].dataset, names[0])) {
			return {i, &MappedField(p.sources[i], names[1])};
		}
	}
	Reject("undeclared source dataset");
}
string AmountUnit(const FactSource &s) {
	return "currency:" + s.currency + ":scale2";
}
void PlainFunction(const FunctionExpression &f) {
	auto &qualified = f.GetQualifiedName();
	if (!qualified.Catalog().GetIdentifierName().empty() || !qualified.Schema().GetIdentifierName().empty() ||
	    f.Distinct() || f.Filter() || (f.OrderBy() && !f.OrderBy()->orders.empty()) || f.ExportState()) {
		Reject("aggregate/operator modifiers or qualified functions unsupported");
	}
	for (auto &argument : f.GetArguments()) {
		if (argument.HasName()) {
			Reject("aggregate/operator modifiers or qualified functions unsupported");
		}
	}
}
unique_ptr<SelectStatement> SQL(const string &sql) {
	auto parser = Parser::GetBuiltinParser();
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
			auto names = ColumnNames(e.Cast<ColumnRefExpression>());
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
		auto name = StringUtil::Lower(f.FunctionName().GetIdentifierName());
		auto &arguments = f.GetArguments();
		if (f.IsOperator()) {
			if ((name != "+" && name != "-") || arguments.size() != 2) {
				Reject("only binary +/- supported");
			}
			string left, right;
			auto a = Bind(arguments[0].GetExpression(), left, depth, expanded, dependency_depth),
			     b = Bind(arguments[1].GetExpression(), right, depth, expanded, dependency_depth);
			if (left != right || left.find("currency:") != 0) {
				Reject("arithmetic requires equal currency/scale money components, not counts");
			}
			budget.Charge();
			unit = left;
			vector<unique_ptr<ParsedExpression>> children;
			children.push_back(std::move(a));
			children.push_back(std::move(b));
			return make_uniq<FunctionExpression>(QualifiedName("system", "main", Identifier(name)),
			                                     std::move(children));
		}
		if ((name != "sum" && name != "count") || arguments.size() != 1) {
			Reject("only single-field SUM/COUNT supported");
		}
		auto field = SourceField(program, arguments[0].GetExpression());
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
		auto aggregate =
		    make_uniq<FunctionExpression>(QualifiedName("system", "main", Identifier(name)), std::move(children));
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
		vector<unique_ptr<ParsedExpression>> leaf;
		leaf.push_back(make_uniq<ColumnRefExpression>(Identifier(field.second->name), Identifier(source.dataset)));
		return make_uniq<FunctionExpression>(QualifiedName("system", "main", Identifier(name)), std::move(leaf));
	}
	void Window(const FactDefinition &definition) {
		auto e = Expression(definition.sql);
		if (e->GetExpressionClass() != ExpressionClass::WINDOW) {
			Reject("one window expression required");
		}
		auto &w = e->Cast<WindowExpression>();
		auto &qualified = w.GetQualifiedName();
		auto &arguments = w.GetArguments();
		auto &orders = w.OrderBy();
		if (!qualified.Catalog().GetIdentifierName().empty() || !qualified.Schema().GetIdentifierName().empty() ||
		    w.Distinct() || w.Filter() || w.IgnoreNulls() || !w.ArgOrders().empty() ||
		    w.WindowExclude() != WindowExcludeMode::NO_OTHER || arguments.empty() || w.Partitions().size() != 1 ||
		    orders.size() != 1) {
			Reject("window shape/modifiers unsupported");
		}
		for (auto &argument : arguments) {
			if (argument.HasName()) {
				Reject("window shape/modifiers unsupported");
			}
		}
		auto field = SourceField(program, arguments[0].GetExpression());
		auto &s = program.sources[field.first];
		if (!s.product_field.empty() || !StringUtil::CIEquals(field.second->name, s.amount_field)) {
			Reject("window requires mapped amount at User/date grain");
		}
		auto part = SourceField(program, *w.Partitions()[0]), order = SourceField(program, *orders[0].expression);
		if (part.first != field.first || order.first != field.first ||
		    !StringUtil::CIEquals(part.second->name, s.user_field) ||
		    !StringUtil::CIEquals(order.second->name, s.date_field)) {
			Reject("window requires same mapped User partition and unique DATE order");
		}
		auto ot = orders[0].type;
		auto nt = orders[0].null_order;
		if ((ot != OrderType::ASCENDING && ot != OrderType::ORDER_DEFAULT) ||
		    (nt != OrderByNullType::ORDER_DEFAULT && nt != OrderByNullType::NULLS_LAST)) {
			Reject("ascending finite DATE order required");
		}
		auto integral = [&](const ParsedExpression *p, int64_t lo, int64_t hi) {
			if (!p || p->GetExpressionClass() != ExpressionClass::CONSTANT) {
				Reject("literal bounded integral required");
			}
			auto v = p->Cast<ConstantExpression>().GetLiteral().ToValue();
			if (v.IsNull() || !v.type().IsIntegral()) {
				Reject("literal bounded integral required");
			}
			string error;
			auto represented = v.DefaultTryCastAs(LogicalType::BIGINT, &error, true);
			if (!represented) {
				Reject("window literal outside BIGINT");
			}
			auto n = represented->GetValue<int64_t>();
			if (n < lo || n > hi) {
				Reject("window offset/frame range");
			}
		};
		auto name = StringUtil::Lower(w.FunctionName().GetIdentifierName());
		if (w.GetExpressionType() == ExpressionType::WINDOW_LAG && name == "lag") {
			if (arguments.size() != 3) {
				Reject("LAG requires explicit NULL default");
			}
			integral(&arguments[1].GetExpression(), 1, 1);
			auto &fallback = arguments[2].GetExpression();
			if (fallback.GetExpressionClass() != ExpressionClass::CONSTANT ||
			    !fallback.Cast<ConstantExpression>().GetLiteral().IsNull()) {
				Reject("LAG requires explicit NULL default");
			}
			if (w.WindowStart() != WindowBoundary::UNBOUNDED_PRECEDING ||
			    w.WindowEnd() != WindowBoundary::CURRENT_ROW_RANGE || w.StartExpr() || w.EndExpr()) {
				Reject("LAG default frame only");
			}
		} else if (w.GetExpressionType() == ExpressionType::WINDOW_AGGREGATE && name == "sum") {
			if (w.WindowStart() != WindowBoundary::EXPR_PRECEDING_ROWS ||
			    w.WindowEnd() != WindowBoundary::CURRENT_ROW_ROWS || w.EndExpr() || arguments.size() != 1) {
				Reject("SUM requires explicit bounded preceding ROWS to current row");
			}
			integral(w.StartExpr().get(), 0, 366);
		} else {
			Reject("only LAG(value,1,NULL) and SUM ROWS windows supported");
		}
		budget.Charge(Nodes(*e));
		auto copy = e->Copy();
		auto &bound = copy->Cast<WindowExpression>();
		bound.GetArgumentsMutable()[0].GetExpressionMutable() = Column(s, *field.second);
		bound.PartitionsMutable()[0] = Column(s, *part.second);
		bound.OrderByMutable()[0].expression = Column(s, *order.second);
		bound.OrderByMutable()[0].type = OrderType::ASCENDING;
		bound.OrderByMutable()[0].null_order = OrderByNullType::NULLS_LAST;
		bound.SetQualifiedName("system", "main", w.FunctionName());
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
		if (cmp.Left().GetExpressionClass() != ExpressionClass::COLUMN_REF ||
		    cmp.Left().Cast<ColumnRefExpression>().ColumnNames().size() != 1 ||
		    cmp.Right().GetExpressionClass() != ExpressionClass::CONSTANT) {
			Reject("metric compared to typed numeric constant required");
		}
		auto &metric = FindMetric(p, cmp.Left().Cast<ColumnRefExpression>().ColumnNames()[0].GetIdentifierName());
		auto v = cmp.Right().Cast<ConstantExpression>().GetLiteral().ToValue();
		if (v.IsNull() || !(v.type().IsIntegral() || v.type().id() == LogicalTypeId::DECIMAL)) {
			Reject("finite exact numeric constant required");
		}
		if (metric.unit.find("count:") == 0 && !v.type().IsIntegral()) {
			Reject("count comparison requires integer constant");
		}
		if (v.type().id() == LogicalTypeId::DECIMAL && DecimalType::GetScale(v.type()) > 2) {
			Reject("comparison amount scale exceeds2");
		}
		string error;
		auto represented = v.DefaultTryCastAs(
		    metric.unit.find("currency:") == 0 ? LogicalType::DECIMAL(38, 2) : LogicalType::BIGINT, &error, true);
		if (!represented) {
			Reject("comparison constant outside measure type");
		}
		cmp.RightMutable() = ConstantExpression::FromValue(*represented);
		cmp.LeftMutable() = make_uniq<ColumnRefExpression>(Identifier(metric.name));
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
//! The program as a core model: one dataset per fact whose fields map to physical columns, and the
//! expanded metrics. Sources are never bound by name; the caller's frozen ASTs replace them.
Model CoreModel(const FactProgram &p) {
	Model model;
	model.name = "fact_program";
	for (auto &s : p.sources) {
		Dataset dataset;
		dataset.name = s.dataset;
		dataset.source_raw = s.dataset;
		dataset.source_bound = s.dataset;
		for (auto &f : s.fields) {
			Field field;
			field.name = f.name;
			field.expression.sql = f.physical_column;
			field.expression.tree = make_uniq<ColumnRefExpression>(Identifier(f.physical_column));
			field.is_computed = false;
			dataset.AddField(std::move(field));
		}
		model.dataset_index[dataset.name] = model.datasets.size();
		model.datasets.push_back(std::move(dataset));
	}
	for (auto &m : p.metrics) {
		Metric metric;
		metric.name = m.name;
		metric.expression.sql = m.expression->ToString();
		metric.expression.tree = m.expression->Copy();
		model.metric_index[metric.name] = model.metrics.size();
		model.metrics.push_back(std::move(metric));
	}
	return model;
}
//! user_id and the metrics, each fact reduced on its own and the reductions INNER joined on the
//! User key, so the row set is the common User domain and no fact row is ever repeated.
unique_ptr<SelectStatement> CoreUsers(const FactProgram &p, const vector<string> &metrics,
                                      const vector<string> &filters, vector<unique_ptr<TableRef>> &sources) {
	auto model = CoreModel(p);
	CompileOptions options;
	SharedKey user;
	user.name = "user_id";
	for (idx_t i = 0; i < p.sources.size(); i++) {
		auto &s = p.sources[i];
		user.fields[s.dataset] = s.user_field;
		options.population_sources[s.dataset] = sources[i].get();
		options.required_grains.push_back(s.dataset);
	}
	options.shared_keys.push_back(std::move(user));
	options.grain_join = JoinType::INNER;
	return Compile(model, metrics, {"user_id"}, filters, options);
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
		if (MappedField(s, s.user_field).type != LogicalType::VARCHAR ||
		    MappedField(s, s.date_field).type != LogicalType::DATE ||
		    MappedField(s, s.amount_field).type != LogicalType::DECIMAL(18, 2) ||
		    (!s.product_field.empty() && MappedField(s, s.product_field).type != LogicalType::VARCHAR)) {
			Reject("actual role types must be VARCHAR/DATE[/VARCHAR]/DECIMAL18,2");
		}
		case_insensitive_set_t roles;
		for (auto &n : identity) {
			if (!roles.insert(MappedField(s, n).physical_column).second) {
				Reject("identity roles cannot share physical column");
			}
		}
		if (!roles.insert(MappedField(s, s.amount_field).physical_column).second) {
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
	auto statement = SQL("SELECT COUNT(*) AS common_user_count FROM placeholder AS combined");
	auto &count = statement->node->Cast<SelectNode>().select_list[0]->Cast<FunctionExpression>();
	count.SetQualifiedName("system", "main", count.FunctionName());
	statement->node->Cast<SelectNode>().from_table =
	    make_uniq<SubqueryRef>(CoreUsers(p, {p.metrics[0].name}, {}, sources), Identifier("combined"));
	return statement;
}

unique_ptr<SelectStatement> CompileFactUsers(const FactProgram &p, const vector<string> &names, const string &domain,
                                             const FactCohort &cohort, vector<unique_ptr<TableRef>> sources) {
	SelectNames(names);
	if (domain != "common" || sources.size() != 2 || !sources[0] || !sources[1]) {
		Reject("explicit common domain and two source ASTs required");
	}
	vector<string> metrics;
	string projection = "SELECT combined.user_id";
	for (auto &name : names) {
		metrics.push_back(FindMetric(p, name).name);
		projection += ",combined." + Q(metrics.back());
	}
	idx_t nodes = 0;
	auto where = Cohort(p, cohort, 1, nodes);
	vector<string> filters;
	if (where != "TRUE") {
		filters.push_back(where);
	}
	auto result = SQL(projection + " FROM placeholder AS combined ORDER BY combined.user_id ASC NULLS LAST");
	result->node->Cast<SelectNode>().from_table =
	    make_uniq<SubqueryRef>(CoreUsers(p, metrics, filters, sources), "combined");
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
		tree->SetAlias(Identifier(window->name));
		window_trees.push_back(std::move(tree));
	}
	auto i = source_index.GetIndex();
	auto &s = p.sources[i];
	auto inner = SQL("SELECT " + Q(s.dataset) + "." + Q(MappedField(s, s.user_field).physical_column) + " AS user_id," +
	                 Q(s.dataset) + "." + Q(MappedField(s, s.date_field).physical_column) + " AS ds" + projection +
	                 " FROM placeholder");
	// Keep the typed window trees rather than reparsing rendered SQL and losing their system binding.
	for (idx_t w = 0; w < window_trees.size(); w++) {
		inner->node->Cast<SelectNode>().select_list[w + 2] = std::move(window_trees[w]);
	}
	sources[i]->alias = Identifier(s.dataset);
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
		if (!a.IsFinite() || !b.IsFinite() || a.days > b.days) {
			Reject("finite ordered display DATE required");
		}
	}
	auto result = SQL(out + " ORDER BY observed.user_id ASC NULLS LAST,observed.ds ASC NULLS LAST");
	if (!start.IsNull()) {
		result->node->Cast<SelectNode>().where_clause = make_uniq<BetweenExpression>(
		    make_uniq<ColumnRefExpression>("ds", "observed"), DateConstant(start), DateConstant(end));
	}
	result->node->Cast<SelectNode>().from_table = make_uniq<SubqueryRef>(std::move(inner), "observed");
	return result;
}
} // namespace ossie
} // namespace duckdb
