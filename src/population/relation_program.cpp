#include "ossie/relation_program.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/conjunction_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
#include "ossie/names.hpp"
#include <map>
#include <set>
#include <functional>

namespace duckdb {
namespace ossie {
namespace {
[[noreturn]] void Reject(const string &reason) {
	throw InvalidInputException("ossie relation program: " + reason);
}
string Key(const string &name) {
	if (name.empty() || name.find('\0') != string::npos) {
		Reject("empty or NUL name");
	}
	for (idx_t i = 0; i < name.size(); i++) {
		auto c = name[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (i && c >= '0' && c <= '9'))) {
			Reject("only unquoted ASCII identifiers supported");
		}
	}
	return StringUtil::Lower(name);
}
void Budget(const ParsedExpression &expression, idx_t depth, idx_t &nodes) {
	if (++nodes > 1024 || depth > 32) {
		Reject("expression exceeds 1024 nodes or depth 32");
	}
	ParsedExpressionIterator::EnumerateChildren(
	    expression, [&](const ParsedExpression &child) { Budget(child, depth + 1, nodes); });
}
unique_ptr<ParsedExpression> ParseRule(const string &text) {
	if (text.empty() || text.size() > 16384 || text.find('\0') != string::npos) {
		Reject("rule exceeds 16384 bytes or is empty/NUL");
	}
	if (text.find('"') != string::npos) {
		Reject("quoted identifiers unsupported in relation rules");
	}
	auto parsed = Parser::GetBuiltinParser().ParseExpressionList(text);
	if (parsed.size() != 1) {
		Reject("exactly one expression required");
	}
	idx_t nodes = 0;
	Budget(*parsed[0], 0, nodes);
	return std::move(parsed[0]);
}
class Binder {
public:
	explicit Binder(const RelationProgramInput &input_p) : input(input_p) {
		if (Key(input.root) == Key(input.user_concept) || input.declarations.empty() ||
		    input.declarations.size() > 32) {
			Reject("distinct root/User and 1..32 declarations required");
		}
		Key(input.dataset);
		Key(input.user_column);
		auto mapped = Key(input.mapped_user_relation);
		for (auto &scalar : input.scalars) {
			if (!scalar.type.IsIntegral() || Key(scalar.name) == mapped ||
			    !scalars.emplace(Key(scalar.name), &scalar).second) {
				Reject("distinct integral scalar declarations required");
			}
			Key(scalar.physical_column);
		}
		for (auto &declaration : input.declarations) {
			auto name = Key(declaration.name);
			if (name == mapped || scalars.count(name) || !declarations.emplace(name, &declaration).second ||
			    !StringUtil::CIEquals(declaration.role_concept, input.user_concept) ||
			    declaration.multiplicity != "ManyToOne" || declaration.derived_by.empty() ||
			    declaration.derived_by.size() > 8 || declaration.requires_list.size() > 8) {
				Reject("unique single-User ManyToOne declarations with 1..8 rules required");
			}
			for (auto &text : declaration.derived_by) {
				InputBytes(text);
			}
			for (auto &text : declaration.requires_list) {
				InputBytes(text);
			}
		}
		for (auto &text : input.requires_list) {
			InputBytes(text);
		}
	}
	RelationProgram Bind() {
		program.root = input.root;
		program.target_concept = input.user_concept;
		program.mapped_user_relation = input.mapped_user_relation;
		if (input.requires_list.size() > 8) {
			Reject("at most eight root constraints");
		}
		for (auto &text : input.requires_list) {
			program.root_constraints.push_back(Constraint(text));
		}
		for (auto &entry : declarations) {
			Visit(entry.first, 1);
		}
		return std::move(program);
	}

private:
	const RelationProgramInput &input;
	std::map<string, const RelationScalar *> scalars;
	std::map<string, const RelationDeclaration *> declarations;
	std::map<string, idx_t> bound;
	std::map<string, idx_t> bound_nodes;
	std::set<string> visiting;
	RelationProgram program;
	idx_t input_bytes = 0, allocated_nodes = 0;
	void InputBytes(const string &text) {
		if (text.size() > 16384 || input_bytes + text.size() > 131072) {
			Reject("input rules exceed per-rule 16384 or total 131072 bytes");
		}
		input_bytes += text.size();
	}
	void Charge(idx_t count, idx_t &nodes) {
		if (nodes + count > 1024 || allocated_nodes + count > 32768) {
			Reject("expanded expression exceeds 1024 nodes or program 32768 nodes");
		}
		nodes += count;
		allocated_nodes += count;
	}
	const RelationScalar &Scalar(const ParsedExpression &tree) {
		if (tree.GetExpressionClass() != ExpressionClass::COLUMN_REF) {
			Reject("only declared integral scalar references allowed");
		}
		auto names = ColumnNames(tree.Cast<ColumnRefExpression>());
		if (names.size() != 2 || !StringUtil::CIEquals(names[0], input.root) || !scalars.count(Key(names[1]))) {
			Reject("unknown scalar or root namespace");
		}
		return *scalars.at(Key(names[1]));
	}
	unique_ptr<ParsedExpression> Physical(const RelationScalar &scalar) {
		return make_uniq<ColumnRefExpression>(Identifier(scalar.physical_column), Identifier(input.dataset));
	}
	unique_ptr<ParsedExpression> Comparison(const ParsedExpression &tree) {
		if (tree.GetExpressionType() != ExpressionType::COMPARE_GREATERTHANOREQUALTO) {
			Reject("only scalar >= nonnull integral constant supported");
		}
		auto &comparison = tree.Cast<ComparisonExpression>();
		auto &scalar = Scalar(comparison.Left());
		if (comparison.Right().GetExpressionClass() != ExpressionClass::CONSTANT) {
			Reject("nonnull integral constant required");
		}
		auto value = comparison.Right().Cast<ConstantExpression>().GetLiteral().ToValue();
		if (value.IsNull() || !value.type().IsIntegral()) {
			Reject("nonnull integral constant required");
		}
		string error;
		if (!value.DefaultTryCastAs(scalar.type, &error, true)) {
			Reject("integral constant outside declared scalar type");
		}
		return make_uniq<ComparisonExpression>(tree.GetExpressionType(), Physical(scalar), comparison.Right().Copy());
	}
	unique_ptr<ParsedExpression> Constraint(const string &text) {
		auto tree = ParseRule(text);
		if (tree->GetExpressionClass() != ExpressionClass::CONJUNCTION) {
			Reject("constraint requires explicit same-field NULL guard");
		}
		auto &conjunction = tree->Cast<ConjunctionExpression>();
		auto &parts = conjunction.GetChildren();
		if (parts.size() != 2) {
			Reject("constraint requires exactly guard and comparison");
		}
		const ParsedExpression *guard = parts[0].get(), *comparison = parts[1].get();
		if (guard->GetExpressionClass() == ExpressionClass::COMPARISON) {
			std::swap(guard, comparison);
		}
		auto guard_type = tree->GetExpressionType() == ExpressionType::CONJUNCTION_OR
		                      ? ExpressionType::OPERATOR_IS_NULL
		                      : ExpressionType::OPERATOR_IS_NOT_NULL;
		if (guard->GetExpressionType() != guard_type ||
		    comparison->GetExpressionClass() != ExpressionClass::COMPARISON) {
			Reject("constraint requires OR IS NULL or AND IS NOT NULL");
		}
		auto &operation = guard->Cast<OperatorExpression>();
		auto &guarded = operation.GetChildren();
		if (guarded.size() != 1 || &Scalar(*guarded[0]) != &Scalar(comparison->Cast<ComparisonExpression>().Left())) {
			Reject("constraint guard must name same scalar");
		}
		idx_t nodes = 0;
		Charge(6, nodes);
		return make_uniq<ConjunctionExpression>(
		    tree->GetExpressionType(), make_uniq<OperatorExpression>(guard_type, Physical(Scalar(*guarded[0]))),
		    Comparison(*comparison));
	}
	// Boolean membership must treat an upstream UNKNOWN qualification as absent.
	unique_ptr<ParsedExpression> Membership(const ParsedExpression &predicate) {
		return make_uniq<ComparisonExpression>(ExpressionType::COMPARE_NOT_DISTINCT_FROM, predicate.Copy(),
		                                       ConstantExpression::Boolean(true));
	}
	unique_ptr<ParsedExpression> Lower(const ParsedExpression &tree, std::set<string> &dependencies, bool &anchored,
	                                   idx_t stack_depth, idx_t &expanded_nodes) {
		if (tree.GetExpressionClass() == ExpressionClass::FUNCTION) {
			auto &call = tree.Cast<FunctionExpression>();
			auto &qualified = call.GetQualifiedName();
			auto &arguments = call.GetArguments();
			if (!qualified.Catalog().GetIdentifierName().empty() ||
			    !StringUtil::CIEquals(qualified.Schema().GetIdentifierName(), input.root) || call.IsOperator() ||
			    call.Distinct() || call.Filter() || call.ExportState() ||
			    (call.OrderBy() && !call.OrderBy()->orders.empty()) || arguments.size() != 1 ||
			    arguments[0].HasName() || !arguments[0].GetExpression().GetAlias().GetIdentifierName().empty() ||
			    arguments[0].GetExpression().GetExpressionClass() != ExpressionClass::COLUMN_REF) {
				Reject("only explicit root.relation(User) calls supported");
			}
			auto role = ColumnNames(arguments[0].GetExpression().Cast<ColumnRefExpression>());
			if (role.size() != 1 || !StringUtil::CIEquals(role[0], input.user_concept)) {
				Reject("unbound or wrong User role");
			}
			auto name = Key(call.FunctionName().GetIdentifierName());
			anchored = true;
			if (name == Key(input.mapped_user_relation)) {
				Charge(2, expanded_nodes);
				return make_uniq<OperatorExpression>(
				    ExpressionType::OPERATOR_IS_NOT_NULL,
				    make_uniq<ColumnRefExpression>(Identifier(input.user_column), Identifier(input.dataset)));
			}
			if (!declarations.count(name)) {
				Reject("unknown relationship reference");
			}
			Visit(name, stack_depth + 1);
			dependencies.insert(name);
			Charge(bound_nodes.at(name) + 2, expanded_nodes);
			return Membership(*program.relations[bound.at(name)].predicate);
		}
		if (tree.GetExpressionClass() == ExpressionClass::COMPARISON) {
			anchored = false;
			Charge(3, expanded_nodes);
			return Comparison(tree);
		}
		if (tree.GetExpressionClass() == ExpressionClass::CONJUNCTION) {
			Charge(1, expanded_nodes);
			auto &conjunction = tree.Cast<ConjunctionExpression>();
			vector<unique_ptr<ParsedExpression>> children;
			bool is_or = tree.GetExpressionType() == ExpressionType::CONJUNCTION_OR;
			anchored = is_or;
			for (auto &child : conjunction.GetChildren()) {
				bool child_anchor = false;
				children.push_back(Lower(*child, dependencies, child_anchor, stack_depth, expanded_nodes));
				anchored = is_or ? anchored && child_anchor : anchored || child_anchor;
			}
			return make_uniq<ConjunctionExpression>(tree.GetExpressionType(), std::move(children));
		}
		Reject("unsupported rule AST; negative calls, entity equality, window/subquery/parameter refused");
	}
	void Visit(const string &name, idx_t stack_depth) {
		if (stack_depth > 16) {
			Reject("dependency depth exceeds 16");
		}
		if (bound.count(name)) {
			return;
		}
		if (!visiting.insert(name).second) {
			Reject("cyclic relationship dependency");
		}
		auto &declaration = *declarations.at(name);
		BoundRelation relation;
		relation.name = declaration.name;
		relation.root = input.root;
		relation.target_concept = input.user_concept;
		relation.rules = declaration.derived_by;
		relation.dependency_depth = 1;
		std::set<string> dependencies;
		vector<unique_ptr<ParsedExpression>> rules;
		idx_t expanded_nodes = 0;
		if (declaration.derived_by.size() > 1) {
			Charge(1, expanded_nodes);
		}
		for (auto &text : declaration.derived_by) {
			auto parsed = ParseRule(text);
			bool anchored = false;
			rules.push_back(Lower(*parsed, dependencies, anchored, stack_depth, expanded_nodes));
			if (!anchored) {
				Reject("each rule alternative must positively bind the User role");
			}
		}
		for (auto &dependency : dependencies) {
			auto &upstream = program.relations[bound.at(dependency)];
			relation.dependencies.push_back(upstream.name);
			relation.dependency_depth = MaxValue(relation.dependency_depth, upstream.dependency_depth + 1);
		}
		if (relation.dependency_depth > 16) {
			Reject("dependency depth exceeds 16");
		}
		relation.predicate = rules.size() == 1
		                         ? std::move(rules[0])
		                         : make_uniq<ConjunctionExpression>(ExpressionType::CONJUNCTION_OR, std::move(rules));
		idx_t nodes = 0;
		Budget(*relation.predicate, 0, nodes);
		bound_nodes.emplace(name, nodes);
		for (auto &text : declaration.requires_list) {
			relation.constraints.push_back(Constraint(text));
		}
		visiting.erase(name);
		bound.emplace(name, program.relations.size());
		program.relations.push_back(std::move(relation));
	}
};
} // namespace
RelationProgram BindRelationProgram(const RelationProgramInput &input) {
	return Binder(input).Bind();
}
} // namespace ossie
} // namespace duckdb
