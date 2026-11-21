#pragma once

#include "duckdb/common/string.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/unordered_set.hpp"
#include "duckdb/parser/parsed_expression.hpp"

namespace duckdb {
namespace ossie {

//! Dialects whose expressions this extension can execute, in order of preference when an expression
//! carries more than one. OSSIE_SQL_2026 comes first: the spec defines it, and names it the default.
//! constexpr rather than defined in dialect.cpp: parser.cpp builds a namespace-scope array from these, and
//! a definition in another translation unit could still be uninitialized when that array is.
constexpr const char *OSSIE_SQL_2026 = "OSSIE_SQL_2026";
constexpr const char *ANSI_SQL = "ANSI_SQL";

//! Rewrite an OSSIE_SQL_2026 expression tree in place into one DuckDB executes with the meaning the
//! Ossie expression language gives it. Functions DuckDB lacks or spells differently are replaced by
//! equivalent trees. A construct whose result the language leaves undefined, or that has no faithful
//! DuckDB equivalent, is refused with an InvalidInputException naming it, so a model fails at load
//! rather than returning a number nobody specified. `context` begins every error: the caller
//! prefixes it, "ossie_load: field ..." at load or "ossie: filter ..." for a request. `sql` is the text
//! `expr` was parsed from, which is the only record of the names written in quotes.
void LowerOssieSql2026(unique_ptr<ParsedExpression> &expr, const string &sql, const string &context);

//! ANSI_SQL reaches DuckDB as written, with one thing done for it first. DuckDB reads a bare CURRENT_DATE,
//! CURRENT_TIMESTAMP or CURRENT_TIME as a column, and calls the function only once no column of that name
//! turns up. The compiler qualifies every column of a field with its dataset before DuckDB sees it, and a
//! qualified name is never retried as a function, so the call is made here. A name written in quotes is a
//! column and stays one.
void LowerAnsiSql(unique_ptr<ParsedExpression> &expr, const string &sql);

//! True for CURRENT_DATE, CURRENT_TIMESTAMP and CURRENT_TIME, in any case: the three names the language
//! writes with or without parentheses.
bool IsCurrentDateTime(const string &name);

//! The double-quoted identifiers in `sql`, as written between the quotes. DuckDB's parser drops the quotes
//! on its way to the tree, so the text is the only record of which names the author meant exactly.
unordered_set<string> QuotedIdentifiers(const string &sql);

} // namespace ossie
} // namespace duckdb
