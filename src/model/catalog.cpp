#include "ossie/catalog.hpp"
#include "ossie/names.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/view_catalog_entry.hpp"
#include "duckdb/catalog/entry_lookup_info.hpp"
#include "duckdb/common/constants.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/unordered_set.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include "duckdb/parser/qualified_name.hpp"
#include "ossie/model.hpp"

namespace duckdb {
namespace ossie {

void SplitSource(const string &source, string &catalog, string &schema, string &name) {
	catalog = INVALID_CATALOG;
	schema = INVALID_SCHEMA;
	name = string();

	auto parts = QualifiedName::ParseComponents(source);
	switch (parts.size()) {
	case 3:
		catalog = parts[0].GetIdentifierName();
		schema = parts[1].GetIdentifierName();
		name = parts[2].GetIdentifierName();
		break;
	case 2:
		schema = parts[0].GetIdentifierName();
		name = parts[1].GetIdentifierName();
		break;
	case 1:
		name = parts[0].GetIdentifierName();
		break;
	default:
		break;
	}
}

namespace {

optional_ptr<CatalogEntry> SourceEntry(ClientContext &context, const string &source) {
	string catalog, schema, name;
	SplitSource(source, catalog, schema, name);
	if (name.empty()) {
		return nullptr;
	}

	// Looking up TABLE_ENTRY also finds views, which the spec allows as a source.
	EntryLookupInfo lookup_info(CatalogType::TABLE_ENTRY,
	                            QualifiedName(Identifier(catalog), Identifier(schema), Identifier(name)));
	try {
		return Catalog::GetEntry(context, lookup_info, OnEntryNotFound::RETURN_NULL);
	} catch (const std::exception &) {
		// An unattached database throws rather than returning null.
		return nullptr;
	}
}

//! The double-quoted identifiers in `sql`, as written between the quotes. DuckDB's parser drops the quotes
//! on its way to the tree, so the text is the only record of which names the author meant exactly.
unordered_set<string> QuotedIdentifiers(const string &sql) {
	unordered_set<string> result;
	for (idx_t i = 0; i < sql.size(); i++) {
		auto c = sql[i];
		if (c == '\'') {
			for (i++; i < sql.size(); i++) {
				if (sql[i] != '\'') {
					continue;
				}
				if (i + 1 < sql.size() && sql[i + 1] == '\'') {
					i++;
					continue;
				}
				break;
			}
		} else if (c == '"') {
			string name;
			for (i++; i < sql.size(); i++) {
				if (sql[i] != '"') {
					name += sql[i];
					continue;
				}
				if (i + 1 < sql.size() && sql[i + 1] == '"') {
					name += '"';
					i++;
					continue;
				}
				break;
			}
			result.insert(name);
		} else if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
			while (i < sql.size() && sql[i] != '\n') {
				i++;
			}
		}
	}
	return result;
}

//! Refuses `column` if it was written in quotes and matches a column of `source` only when case is ignored.
void CheckQuotedColumn(const string &column, const unordered_set<string> &quoted, const vector<string> &columns,
                       const string &source, const string &context) {
	if (quoted.find(column) == quoted.end()) {
		return;
	}
	for (auto &candidate : columns) {
		if (candidate == column) {
			return;
		}
	}
	for (auto &candidate : columns) {
		if (StringUtil::CIEquals(candidate, column)) {
			throw InvalidInputException("ossie_load: %s refers to \"%s\" in quotes, which OSSIE_SQL_2026 matches "
			                            "exactly; the column of %s is spelled \"%s\". Write %s without quotes to "
			                            "match it regardless of case",
			                            context, column, source, candidate, candidate);
		}
	}
}

void CollectColumnRefs(const ParsedExpression &expr, vector<reference<const ColumnRefExpression>> &refs) {
	if (expr.GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		refs.push_back(expr.Cast<ColumnRefExpression>());
	}
	auto visit = [&](const ParsedExpression &child) {
		CollectColumnRefs(child, refs);
	};
	ParsedExpressionIterator::EnumerateChildren(expr, visit);
}

vector<reference<const ColumnRefExpression>> ColumnRefs(const ParsedExpression &expr) {
	vector<reference<const ColumnRefExpression>> refs;
	CollectColumnRefs(expr, refs);
	return refs;
}

} // namespace

bool SourceResolves(ClientContext &context, const string &source) {
	return SourceEntry(context, source) != nullptr;
}

bool SourceColumns(ClientContext &context, const string &source, vector<string> &columns) {
	auto entry = SourceEntry(context, source);
	if (!entry) {
		return false;
	}
	if (entry->type == CatalogType::TABLE_ENTRY) {
		columns = entry->Cast<TableCatalogEntry>().GetColumns().GetColumnNames();
		return true;
	}
	if (entry->type == CatalogType::VIEW_ENTRY) {
		auto &view = entry->Cast<ViewCatalogEntry>();
		shared_ptr<ViewColumnInfo> info;
		try {
			view.BindView(context);
			info = view.GetColumnInfo();
		} catch (const std::exception &) {
			// A view over something missing binds at query time, where the message names what is missing.
			return false;
		}
		if (!info) {
			return false;
		}
		columns.clear();
		for (auto &name : info->names) {
			columns.push_back(name.GetIdentifierName());
		}
		for (idx_t i = 0; i < view.aliases.size() && i < columns.size(); i++) {
			columns[i] = view.aliases[i].GetIdentifierName();
		}
		return true;
	}
	return false;
}

void CheckQuotedIdentifiers(ClientContext &context, const Model &model) {
	for (auto &dataset : model.datasets) {
		vector<string> columns;
		if (!SourceColumns(context, dataset.source_bound, columns)) {
			continue;
		}
		for (auto &field : dataset.fields) {
			if (!field.expression.tree) {
				continue;
			}
			auto quoted = QuotedIdentifiers(field.expression.sql);
			if (quoted.empty()) {
				continue;
			}
			auto context_name = StringUtil::Format("field \"%s.%s\"", dataset.name, field.name);
			// Every column reference in a field is a physical column of the field's own dataset.
			for (auto &ref : ColumnRefs(*field.expression.tree)) {
				CheckQuotedColumn(ColumnNames(ref.get()).back(), quoted, columns, dataset.source_bound, context_name);
			}
		}
		for (auto &metric : model.metrics) {
			if (!metric.expression.tree) {
				continue;
			}
			auto quoted = QuotedIdentifiers(metric.expression.sql);
			if (quoted.empty()) {
				continue;
			}
			auto context_name = StringUtil::Format("metric \"%s\"", metric.name);
			// In a metric, dataset.column is physical when column is not a declared field of that dataset.
			for (auto &ref : ColumnRefs(*metric.expression.tree)) {
				auto names = ColumnNames(ref.get());
				if (names.size() != 2 || model.FindDataset(names[0]) != &dataset || dataset.FindField(names[1])) {
					continue;
				}
				CheckQuotedColumn(names[1], quoted, columns, dataset.source_bound, context_name);
			}
		}
	}
}

} // namespace ossie
} // namespace duckdb
