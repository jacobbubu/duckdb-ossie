#pragma once

#include "duckdb/common/string.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"

namespace duckdb {
namespace ossie {

//! Identifiers as plain strings. The model is keyed by string throughout, so compare and look up through this.
inline vector<string> Names(const vector<Identifier> &identifiers) {
	vector<string> names;
	for (auto &name : identifiers) {
		names.push_back(name.GetIdentifierName());
	}
	return names;
}

//! The parts of a column reference as plain strings. DuckDB holds them as Identifiers behind an accessor.
inline vector<string> ColumnNames(const ColumnRefExpression &colref) {
	return Names(colref.ColumnNames());
}

//! Plain strings back to Identifiers, for the column names a table function hands to DuckDB.
inline vector<Identifier> Identifiers(const vector<string> &names) {
	vector<Identifier> identifiers;
	for (auto &name : names) {
		identifiers.push_back(Identifier(name));
	}
	return identifiers;
}

} // namespace ossie
} // namespace duckdb
