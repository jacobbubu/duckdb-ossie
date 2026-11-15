#pragma once

#include "duckdb/common/string.hpp"
#include "duckdb/common/vector.hpp"

namespace duckdb {
class ClientContext;

namespace ossie {
struct Model;

//! Splits a 1-, 2- or 3-part "catalog.schema.table" name. Missing parts are left empty.
void SplitSource(const string &source, string &catalog, string &schema, string &name);

//! True if a dataset's bound source names a table or view that exists right now.
bool SourceResolves(ClientContext &context, const string &source);

//! The column names of a dataset's bound source; false when it is not a table or view in the catalog.
bool SourceColumns(ClientContext &context, const string &source, vector<string> &columns);

//! A quoted identifier matches a column exactly under OSSIE_SQL_2026, where DuckDB matches it regardless of
//! case. For each dataset whose source is in the catalog, refuses a quoted column name in a field or metric
//! that matches a column only when case is ignored. Sources that are not in the catalog are not checked.
void CheckQuotedIdentifiers(ClientContext &context, const Model &model);

} // namespace ossie
} // namespace duckdb
