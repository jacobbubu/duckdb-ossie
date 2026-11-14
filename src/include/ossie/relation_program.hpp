#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/parser/parsed_expression.hpp"

namespace duckdb {
namespace ossie {

// Binding contract for a bounded, nonrecursive program. The caller must verify
// these declared physical types against the frozen capture before execution.
struct RelationScalar {
	string name;
	string physical_column;
	LogicalType type;
};
struct RelationDeclaration {
	string name;
	string role_concept;
	string multiplicity;
	vector<string> derived_by;
	vector<string> requires;
};
struct RelationProgramInput {
	string root, user_concept, mapped_user_relation, dataset, user_column;
	vector<RelationScalar> scalars;
	vector<RelationDeclaration> declarations;
	vector<string> requires;
};
struct BoundRelation {
	string name, root, target_concept;
	vector<string> rules, dependencies;
	idx_t dependency_depth;
	unique_ptr<ParsedExpression> predicate;
	vector<unique_ptr<ParsedExpression>> constraints;
};
struct RelationProgram {
	string root, target_concept, mapped_user_relation;
	vector<BoundRelation> relations; // Deterministic topological order.
	vector<unique_ptr<ParsedExpression>> root_constraints;
};

// This supports only positive, explicitly anchored root.relation(User) calls.
// Every resulting link retains the caller's complete root tuple and the one
// mapped User identity. No candidate entities, joins or source reads are inferred.
RelationProgram BindRelationProgram(const RelationProgramInput &input);

} // namespace ossie
} // namespace duckdb
