#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/parser.hpp"
#include "ossie/compile.hpp"
#include "ossie/names.hpp"
#include "yyjson.hpp"

#include <algorithm>
#include <memory>

// Local adapter for DuckDB's vendored C API names; no project-wide naming/check exemption.
using namespace duckdb_yyjson; // NOLINT

namespace duckdb {
namespace ossie {

namespace {

string Text(yyjson_val *value) {
	return string(yyjson_get_str(value), yyjson_get_len(value));
}

const char *Kind(yyjson_val *value) {
	if (yyjson_is_null(value)) {
		return "null";
	}
	if (yyjson_is_bool(value)) {
		return "a boolean";
	}
	if (yyjson_is_num(value)) {
		return "a number";
	}
	if (yyjson_is_str(value)) {
		return "a string";
	}
	return yyjson_is_arr(value) ? "an array" : "an object";
}

//! The members of `object` by name. A name outside `allowed` is refused rather than ignored, and so is a name
//! given twice, since which of the two values the caller meant is a guess.
unordered_map<string, yyjson_val *> Members(yyjson_val *object, const vector<string> &allowed, const string &subject) {
	unordered_map<string, yyjson_val *> result;
	size_t idx, max;
	yyjson_val *key, *value;
	yyjson_obj_foreach(object, idx, max, key, value) {
		auto name = Text(key);
		if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
			throw InvalidInputException("ossie: E_INVALID_QUERY: %s does not take \"%s\"; it takes %s", subject, name,
			                            StringUtil::Join(allowed, ", "));
		}
		if (result.count(name)) {
			throw InvalidInputException("ossie: E_INVALID_QUERY: %s gives \"%s\" twice", subject, name);
		}
		result[name] = value;
	}
	return result;
}

//! An alias is one identifier, written the way a model writes a name: bare, or in double quotes.
string ParseAlias(yyjson_val *value, const string &reference) {
	if (!yyjson_is_str(value)) {
		throw InvalidInputException("ossie: E_INVALID_QUERY: the alias of \"%s\" must be a string, not %s", reference,
		                            Kind(value));
	}
	auto alias = Text(value);
	vector<unique_ptr<ParsedExpression>> parsed;
	try {
		parsed = Parser::GetBuiltinParser().ParseExpressionList(alias);
	} catch (const ParserException &) {
		parsed.clear();
	}
	if (parsed.size() == 1 && parsed[0]->GetExpressionClass() == ExpressionClass::COLUMN_REF &&
	    parsed[0]->GetAlias().GetIdentifierName().empty()) {
		auto names = ColumnNames(parsed[0]->Cast<ColumnRefExpression>());
		if (names.size() == 1 && !names[0].empty()) {
			return names[0];
		}
	}
	throw InvalidInputException("ossie: E_INVALID_QUERY: the alias \"%s\" of \"%s\" is not an identifier; a "
	                            "reserved word, or a name with spaces or punctuation, goes in double quotes",
	                            alias, reference);
}

//! `metrics` or `dimensions`: each entry is a reference, or an object holding the reference under `key` with an
//! alias beside it.
void ParseEntries(const unordered_map<string, yyjson_val *> &parts, const string &part, const string &key,
                  vector<string> &references, vector<string> &aliases) {
	auto found = parts.find(part);
	if (found == parts.end()) {
		return;
	}
	if (!yyjson_is_arr(found->second)) {
		throw InvalidInputException("ossie: E_INVALID_QUERY: \"%s\" must be an array, not %s", part,
		                            Kind(found->second));
	}
	auto subject = "an entry of \"" + part + "\"";
	size_t idx, max;
	yyjson_val *entry;
	yyjson_arr_foreach(found->second, idx, max, entry) {
		if (yyjson_is_str(entry)) {
			references.push_back(Text(entry));
			aliases.emplace_back();
			continue;
		}
		if (!yyjson_is_obj(entry)) {
			throw InvalidInputException("ossie: E_INVALID_QUERY: %s must be a reference string, or an object with "
			                            "\"%s\" and \"alias\", not %s",
			                            subject, key, Kind(entry));
		}
		auto properties = Members(entry, {key, "alias"}, subject);
		auto reference = properties.find(key);
		if (reference == properties.end() || !yyjson_is_str(reference->second)) {
			throw InvalidInputException("ossie: E_INVALID_QUERY: %s must give \"%s\" as a reference string", subject,
			                            key);
		}
		references.push_back(Text(reference->second));
		auto alias = properties.find("alias");
		aliases.push_back(alias == properties.end() ? string() : ParseAlias(alias->second, references.back()));
	}
}

} // namespace

Request ParseRequest(const string &json) {
	std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> document(yyjson_read(json.c_str(), json.size(), 0),
	                                                                 yyjson_doc_free);
	if (!document) {
		throw InvalidInputException("ossie: E_INVALID_QUERY: the query is not valid JSON; it is an object such as "
		                            "{\"metrics\": [...], \"dimensions\": [...], \"filter\": \"...\"}");
	}
	auto root = yyjson_doc_get_root(document.get());
	if (!yyjson_is_obj(root)) {
		throw InvalidInputException("ossie: E_INVALID_QUERY: the query must be a JSON object, not %s", Kind(root));
	}

	auto parts = Members(root, {"metrics", "dimensions", "filter"}, "the query");
	Request result;
	ParseEntries(parts, "metrics", "metric", result.metrics, result.metric_aliases);
	ParseEntries(parts, "dimensions", "field", result.dimensions, result.dimension_aliases);
	auto filter = parts.find("filter");
	if (filter != parts.end()) {
		if (!yyjson_is_str(filter->second)) {
			throw InvalidInputException("ossie: E_INVALID_QUERY: \"filter\" must be an expression string, not %s",
			                            Kind(filter->second));
		}
		result.filters.push_back(Text(filter->second));
	}
	return result;
}

} // namespace ossie
} // namespace duckdb
