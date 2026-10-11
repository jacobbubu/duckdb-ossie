#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/parser/parsed_expression.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref.hpp"

namespace duckdb {
namespace ossie {

// Internal typed inputs extracted from validated ontology/mapping and actual frozen
// projection types. This binder does not read sources, validate data keys or publish.
struct FactField {
	string name, physical_column;
	LogicalType type;
};
struct FactSource {
	string dataset, concept, user_concept, target_identity_relationship;
	string user_field, date_field, product_field, amount_field, currency;
	vector<string> identity_fields;
	vector<FactField> fields;
};
struct FactDefinition {
	string name, sql;
};
struct FactProgramInput {
	string user_concept, user_identity_relationship;
	vector<FactSource> sources;
	vector<FactDefinition> metrics, windows;
};
struct FactAggregate {
	idx_t source;
	string field, kind, unit;
	unique_ptr<ParsedExpression> expression;
};
struct FactMetric {
	string name, unit;
	unique_ptr<ParsedExpression> expression; // Only references already reduced components.
};
struct FactWindow {
	string name;
	idx_t source;
	string unit;
	unique_ptr<ParsedExpression> expression;
};
struct FactProgram {
	string user_concept, user_identity_relationship;
	vector<FactSource> sources;
	vector<FactAggregate> aggregates;
	vector<FactMetric> metrics;
	vector<FactWindow> windows;
};
// A bounded set expression. Atomic predicates are comparisons of one declared
// reduced metric to one typed constant. Operators consume TRUE member sets.
struct FactCohort {
	string operation; // all, predicate, intersection, union, difference
	string predicate;
	vector<FactCohort> children;
};
FactProgram BindFactProgram(const FactProgramInput &input);
// Every source TableRef is supplied explicitly. The loader must supply frozen
// AST dependencies from one validated batch, rather than a raw fact-to-fact join.
// Counts the row-presence common User domain without evaluating monetary metrics.
unique_ptr<SelectStatement> CompileFactCommonCount(const FactProgram &program, vector<unique_ptr<TableRef>> sources);
unique_ptr<SelectStatement> CompileFactUsers(const FactProgram &program, const vector<string> &metrics,
                                             const string &domain, const FactCohort &cohort,
                                             vector<unique_ptr<TableRef>> sources);
// Window input is the entire supplied source; optional display range is applied
// only after window computation. Caller verifies finite DATE values and unique keys.
unique_ptr<SelectStatement> CompileFactWindows(const FactProgram &program, const vector<string> &windows,
                                               const string &input_scope, const Value &display_start,
                                               const Value &display_end, vector<unique_ptr<TableRef>> sources);

} // namespace ossie
} // namespace duckdb
