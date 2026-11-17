#include "ossie/parser.hpp"

#include "ossie/compile.hpp"
#include "ossie/dialect.hpp"
#include "ossie/names.hpp"

#include "ossie/graph.hpp"
#include "ossie/validate.hpp"
#include "ossie/yaml.hpp"

#include "duckdb/common/error_data.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include "duckdb/parser/parser.hpp"

#include "yyjson.hpp"

#include <initializer_list>

// Local adapter for DuckDB's vendored C API names; no project-wide naming/check exemption.
using namespace duckdb_yyjson; // NOLINT

namespace duckdb {
namespace ossie {

namespace {

struct JSONDocument {
	explicit JSONDocument(yyjson_doc *doc_p) : doc(doc_p) {
	}
	~JSONDocument() {
		if (doc) {
			yyjson_doc_free(doc);
		}
	}
	JSONDocument(const JSONDocument &) = delete;
	JSONDocument &operator=(const JSONDocument &) = delete;

	//! yyjson_doc is a raw C handle owned by JSONDocument during its lifetime
	yyjson_doc *doc;
};

yyjson_val *Member(yyjson_val *obj, const char *key) {
	if (!obj || !yyjson_is_obj(obj)) {
		return nullptr;
	}
	return yyjson_obj_get(obj, key);
}

string RequiredString(yyjson_val *obj, const char *key, const string &context) {
	auto val = Member(obj, key);
	if (!val || yyjson_is_null(val)) {
		throw InvalidInputException("ossie_load: %s is missing required string field '%s'", context, key);
	}
	if (!yyjson_is_str(val)) {
		throw InvalidInputException("ossie_load: %s field '%s' must be a string", context, key);
	}
	return string(yyjson_get_str(val), yyjson_get_len(val));
}

string RequiredNonEmptyString(yyjson_val *obj, const char *key, const string &context) {
	auto value = RequiredString(obj, key, context);
	if (value.empty()) {
		throw InvalidInputException("ossie_load: %s field '%s' must be a nonempty string", context, key);
	}
	return value;
}

string OptionalString(yyjson_val *obj, const char *key, const string &context) {
	auto val = Member(obj, key);
	if (!val) {
		return string();
	}
	if (!yyjson_is_str(val)) {
		throw InvalidInputException("ossie_load: %s field '%s' must be a string", context, key);
	}
	return string(yyjson_get_str(val), yyjson_get_len(val));
}

vector<string> StringArray(yyjson_val *obj, const char *key, const string &context) {
	vector<string> result;
	auto arr = Member(obj, key);
	if (!arr) {
		return result;
	}
	if (!yyjson_is_arr(arr)) {
		throw InvalidInputException("ossie_load: %s field '%s' must be an array of strings", context, key);
	}
	size_t idx, max;
	yyjson_val *item;
	yyjson_arr_foreach(arr, idx, max, item) {
		if (!yyjson_is_str(item)) {
			throw InvalidInputException("ossie_load: %s field '%s' member at index %s must be a string", context, key,
			                            to_string(idx));
		}
		result.emplace_back(yyjson_get_str(item), yyjson_get_len(item));
	}
	return result;
}

struct JSONTextDeleter {
	void operator()(char *buffer) const {
		free(buffer);
	}
};

string JSONText(yyjson_val *value) {
	if (!value) {
		return string();
	}
	unique_ptr<char, JSONTextDeleter> serialized(yyjson_val_write(value, 0, nullptr));
	if (!serialized) {
		throw InvalidInputException("ossie_load: could not preserve metadata JSON");
	}
	return string(serialized.get());
}

void ValidateKnownFields(yyjson_val *obj, std::initializer_list<const char *> allowed, const string &context) {
	if (!yyjson_is_obj(obj)) {
		throw InvalidInputException("ossie_load: %s must be an object", context);
	}
	size_t idx, max;
	yyjson_val *key, *value;
	yyjson_obj_foreach(obj, idx, max, key, value) {
		string name(yyjson_get_str(key), yyjson_get_len(key));
		bool known = false;
		for (auto field : allowed) {
			if (name == field) {
				known = true;
				break;
			}
		}
		if (!known) {
			throw InvalidInputException("ossie_load: %s has unknown field %s", context, JSONText(key));
		}
	}
}

string ParseNonportableExtensions(yyjson_val *parent, const string &context) {
	auto extensions = Member(parent, "nonportable_extensions");
	if (!extensions) {
		return string();
	}
	if (!yyjson_is_arr(extensions)) {
		throw InvalidInputException("ossie_load: %s field 'nonportable_extensions' must be an array", context);
	}
	size_t idx, max;
	yyjson_val *item;
	yyjson_arr_foreach(extensions, idx, max, item) {
		auto member_context = context + " nonportable_extensions member at index " + to_string(idx);
		ValidateKnownFields(item, {"vendor_name", "data"}, member_context);
		RequiredNonEmptyString(item, "vendor_name", member_context);
		auto data = Member(item, "data");
		if (!data || !yyjson_is_obj(data)) {
			throw InvalidInputException("ossie_load: %s field 'data' must be an object", member_context);
		}
	}
	return JSONText(extensions);
}

string ParseDatatype(yyjson_val *parent, const string &context) {
	auto value = Member(parent, "datatype");
	if (!value) {
		return string();
	}
	if (yyjson_is_str(value)) {
		string datatype(yyjson_get_str(value), yyjson_get_len(value));
		for (auto allowed :
		     {"String", "Integer", "Decimal", "Float", "Boolean", "Date", "Time", "DateTime", "DateTimeTz", "Opaque"}) {
			if (datatype == allowed) {
				return datatype;
			}
		}
	}
	throw InvalidInputException("ossie_load: %s has an unsupported datatype; use the Ossie logical type vocabulary",
	                            context);
}

AIContext ParseAIContext(yyjson_val *parent, const string &context) {
	AIContext result;
	auto ai_context = Member(parent, "ai_context");
	if (!ai_context) {
		return result;
	}
	if (!yyjson_is_str(ai_context) && !yyjson_is_obj(ai_context)) {
		throw InvalidInputException("ossie_load: %s ai_context must be a string or object", context);
	}
	result.raw_json = JSONText(ai_context);
	if (yyjson_is_str(ai_context)) {
		result.instructions = string(yyjson_get_str(ai_context), yyjson_get_len(ai_context));
		return result;
	}
	auto instructions = Member(ai_context, "instructions");
	if (instructions && !yyjson_is_str(instructions)) {
		throw InvalidInputException("ossie_load: %s ai_context.instructions must be a string", context);
	}
	result.synonyms = StringArray(ai_context, "synonyms", context);
	result.examples = StringArray(ai_context, "examples", context);
	result.instructions = OptionalString(ai_context, "instructions", context);
	return result;
}

//! Dialect names are open strings in Ossie; two are executable here: the spec's own OSSIE_SQL_2026,
//! lowered to DuckDB in dialect.cpp, and ANSI_SQL, which DuckDB reads as written. When an expression
//! offers both, OSSIE_SQL_2026 wins, because the spec defines what it means.
const char *const EXECUTABLE_DIALECTS[] = {OSSIE_SQL_2026, ANSI_SQL};

//! DuckDB's text parser stops at NUL; accepting a prefix would change the model formula.
void RefuseNul(const string &sql, const string &context) {
	if (sql.find('\0') != string::npos) {
		throw InvalidInputException("ossie_load: %s expression must not contain NUL", context);
	}
}

ModelExpression ParseModelExpression(yyjson_val *parent, const string &context) {
	auto expression_obj = Member(parent, "expression");
	if (!expression_obj) {
		throw InvalidInputException("ossie_load: %s is missing required field 'expression'", context);
	}
	ValidateKnownFields(expression_obj, {"sql", "dialects"}, context + " expression");
	auto sql = Member(expression_obj, "sql");
	auto dialects = Member(expression_obj, "dialects");
	if (!sql && !dialects) {
		throw InvalidInputException("ossie_load: %s has an 'expression' with neither 'sql' nor 'dialects'", context);
	}

	ModelExpression result;
	vector<pair<string, string>> variants;
	// The top-level `sql` is the authoritative OSSIE_SQL_2026 form: a `dialects` entry for the same
	// dialect is listed as available but never executed over it.
	if (sql) {
		auto text = RequiredString(expression_obj, "sql", context + " expression");
		RefuseNul(text, context);
		result.available_dialects.push_back(OSSIE_SQL_2026);
		variants.emplace_back(OSSIE_SQL_2026, text);
	}
	if (dialects) {
		if (!yyjson_is_arr(dialects)) {
			throw InvalidInputException("ossie_load: %s expression field 'dialects' must be an array", context);
		}
		size_t idx, max;
		yyjson_val *item;
		yyjson_arr_foreach(dialects, idx, max, item) {
			ValidateKnownFields(item, {"dialect", "expression"}, context + " dialect variant");
			auto dialect = RequiredNonEmptyString(item, "dialect", context + " dialect variant");
			auto text = RequiredString(item, "expression", context);
			RefuseNul(text, context);
			if (sql && dialect == OSSIE_SQL_2026) {
				continue;
			}
			result.available_dialects.push_back(dialect);
			variants.emplace_back(dialect, text);
		}
	}
	if (variants.empty()) {
		throw InvalidInputException("ossie_load: %s has an 'expression' with an empty 'dialects' array", context);
	}

	for (auto executable : EXECUTABLE_DIALECTS) {
		for (auto &variant : variants) {
			if (result.dialect.empty() && variant.first == executable) {
				result.dialect = executable;
				result.sql = variant.second;
			}
		}
	}

	if (result.dialect.empty()) {
		throw InvalidInputException("ossie_load: %s has no expression in a dialect this extension can "
		                            "execute (found: %s; expected OSSIE_SQL_2026 or ANSI_SQL).",
		                            context, StringUtil::Join(result.available_dialects, ", "));
	}

	try {
		auto parsed = Parser::GetBuiltinParser().ParseExpressionList(result.sql);
		if (parsed.size() != 1) {
			throw InvalidInputException("ossie_load: %s expression '%s' parsed as %s expressions; "
			                            "expected exactly one",
			                            context, result.sql, to_string(parsed.size()));
		}
		result.tree = std::move(parsed[0]);
		if (result.dialect == OSSIE_SQL_2026) {
			LowerOssieSql2026(result.tree, "ossie_load: " + context);
		}
	} catch (const ParserException &ex) {
		throw InvalidInputException("ossie_load: %s has an unparseable expression '%s': %s", context, result.sql,
		                            ErrorData(ex).RawMessage());
	}
	return result;
}

//! True only when the expression is a bare reference to the field's own name
bool IsPlainReferenceTo(const ParsedExpression &expr, const string &field_name) {
	if (expr.GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		return false;
	}
	auto names = ColumnNames(expr.Cast<ColumnRefExpression>());
	if (names.empty()) {
		return false;
	}
	return StringUtil::CIEquals(names.back(), field_name);
}

bool IsAggCall(const ParsedExpression &expr) {
	return expr.GetExpressionClass() == ExpressionClass::FUNCTION &&
	       StringUtil::CIEquals(expr.Cast<FunctionExpression>().FunctionName().GetIdentifierName(), "agg");
}

//! `AGG(dataset.metric)` is defined for model-scoped metrics only.
void RefuseAgg(const ParsedExpression &expr, const string &context) {
	if (IsAggCall(expr)) {
		throw InvalidInputException("ossie_load: %s calls AGG, which only a model-scoped metric may use to evaluate a "
		                            "dataset-scoped metric as AGG(dataset.metric)",
		                            context);
	}
	ParsedExpressionIterator::EnumerateChildren(expr,
	                                            [&](const ParsedExpression &child) { RefuseAgg(child, context); });
}

Field ParseField(yyjson_val *field_obj, const string &dataset_name) {
	ValidateKnownFields(
	    field_obj,
	    {"name", "expression", "dimension", "label", "description", "datatype", "ai_context", "nonportable_extensions"},
	    StringUtil::Format("dataset \"%s\" field", dataset_name));
	Field result;
	result.name = RequiredNonEmptyString(field_obj, "name", StringUtil::Format("dataset \"%s\" field", dataset_name));
	auto context = StringUtil::Format("field \"%s.%s\"", dataset_name, result.name);

	result.expression = ParseModelExpression(field_obj, context);
	result.datatype = ParseDatatype(field_obj, context);
	result.label = OptionalString(field_obj, "label", context);
	result.description = OptionalString(field_obj, "description", context);
	result.ai_context = ParseAIContext(field_obj, context);
	result.nonportable_extensions_json = ParseNonportableExtensions(field_obj, context);

	result.is_time = result.datatype == "Date" || result.datatype == "Time" || result.datatype == "DateTime" ||
	                 result.datatype == "DateTimeTz";
	auto dimension = Member(field_obj, "dimension");
	if (dimension) {
		auto is_time = Member(dimension, "is_time");
		if (!yyjson_is_obj(dimension) || (is_time && !yyjson_is_bool(is_time))) {
			throw InvalidInputException("ossie_load: %s dimension must be an object with a boolean is_time", context);
		}
		ValidateKnownFields(dimension, {"is_time"}, context + " dimension");
		if (is_time) {
			result.is_time = yyjson_get_bool(is_time);
			result.is_time_explicit = true;
		}
	}

	RefuseAgg(*result.expression.tree, context);
	result.is_computed = !IsPlainReferenceTo(*result.expression.tree, result.name);
	return result;
}

//! `unique_keys` is a list of key lists, each entry is a (possibly composite) key.
vector<vector<string>> ParseUniqueKeys(yyjson_val *dataset_obj, const string &context) {
	vector<vector<string>> result;
	auto arr = Member(dataset_obj, "unique_keys");
	if (!arr) {
		return result;
	}
	if (!yyjson_is_arr(arr)) {
		throw InvalidInputException("ossie_load: %s field 'unique_keys' must be a list of column lists", context);
	}
	size_t idx, max;
	yyjson_val *item;
	yyjson_arr_foreach(arr, idx, max, item) {
		if (!yyjson_is_arr(item)) {
			throw InvalidInputException("ossie_load: %s field 'unique_keys' must be a list of column "
			                            "lists (each entry is itself an array)",
			                            context);
		}
		vector<string> key;
		size_t col_idx, col_max;
		yyjson_val *col;
		yyjson_arr_foreach(item, col_idx, col_max, col) {
			if (!yyjson_is_str(col)) {
				throw InvalidInputException("ossie_load: %s has a non-string column in 'unique_keys'", context);
			}
			key.emplace_back(yyjson_get_str(col), yyjson_get_len(col));
		}
		result.push_back(std::move(key));
	}
	return result;
}

//! Only accept `source` that follows "database.schema.table" format, not a query
void ValidateSourceShape(const string &source, const string &context) {
	if (source.find_first_of(" \t\n(") != string::npos) {
		throw InvalidInputException("ossie_load: %s has a query source ('%s'). Only "
		                            "database.schema.table references are supported: a query source "
		                            "can neither be rebound nor bound as a table.",
		                            context, source);
	}
}

//! A field expression may name another field of the same dataset. Expand those references at load
//! so that by the time the compiler substitutes a field, its tree contains only physical columns.
//!
//! This has to happen here rather than in the compiler. The compiler substitutes a field and then
//! qualifies the result, which rewrites every column reference into a two-part name -- at which
//! point a model-level field name and a physical column name are the same object, and there is no
//! way left to tell whether more expansion is needed. Expanding once at load also turns a reference
//! cycle into an ossie_load error instead of unbounded recursion at query time.
enum class ExpandState : uint8_t { PENDING, ACTIVE, DONE };

void ExpandField(Dataset &dataset, idx_t index, vector<ExpandState> &state, vector<string> &stack,
                 const string &context);

//! Replaces references to *other computed* fields with their (already expanded) expressions.
//! Three cases are deliberately left alone, because in each the reference already names a physical
//! column: a name that is not a declared field at all, a non-computed field (whose name IS its
//! column), and a field referring to itself -- `revenue` defined as `revenue * 2` means the column
//! times two, not a cycle.
void SubstituteFieldRefs(Dataset &dataset, unique_ptr<ParsedExpression> &expr, idx_t self, vector<ExpandState> &state,
                         vector<string> &stack, const string &context) {
	if (expr->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto &colref = expr->Cast<ColumnRefExpression>();
		auto entry = dataset.field_index.find(ColumnNames(colref).back());
		if (entry == dataset.field_index.end()) {
			return;
		}
		auto referenced = entry->second;
		if (referenced == self || !dataset.fields[referenced].is_computed) {
			return;
		}
		ExpandField(dataset, referenced, state, stack, context);
		expr = dataset.fields[referenced].expression.tree->Copy();
		return;
	}
	ParsedExpressionIterator::EnumerateChildren(*expr, [&](unique_ptr<ParsedExpression> &child) {
		SubstituteFieldRefs(dataset, child, self, state, stack, context);
	});
}

void ExpandField(Dataset &dataset, idx_t index, vector<ExpandState> &state, vector<string> &stack,
                 const string &context) {
	if (state[index] == ExpandState::DONE) {
		return;
	}
	if (state[index] == ExpandState::ACTIVE) {
		auto cycle = stack;
		cycle.push_back(dataset.fields[index].name);
		throw InvalidInputException("ossie_load: %s has a cycle in its field definitions: %s. A field may "
		                            "not be defined, directly or indirectly, in terms of itself",
		                            context, StringUtil::Join(cycle, " -> "));
	}
	state[index] = ExpandState::ACTIVE;
	stack.push_back(dataset.fields[index].name);
	if (dataset.fields[index].expression.tree) {
		SubstituteFieldRefs(dataset, dataset.fields[index].expression.tree, index, state, stack, context);
	}
	stack.pop_back();
	state[index] = ExpandState::DONE;
}

void ExpandFieldReferences(Dataset &dataset, const string &context) {
	vector<ExpandState> state(dataset.fields.size(), ExpandState::PENDING);
	vector<string> stack;
	for (idx_t i = 0; i < dataset.fields.size(); i++) {
		ExpandField(dataset, i, state, stack, context);
	}
}

Dataset ParseDataset(yyjson_val *dataset_obj, const RebindMap &rebind) {
	ValidateKnownFields(dataset_obj,
	                    {"name", "source", "primary_key", "unique_keys", "description", "ai_context", "fields",
	                     "metrics", "nonportable_extensions"},
	                    "dataset");
	Dataset result;
	result.name = RequiredNonEmptyString(dataset_obj, "name", "dataset");
	auto context = StringUtil::Format("dataset \"%s\"", result.name);

	result.source_raw = RequiredNonEmptyString(dataset_obj, "source", context);
	ValidateSourceShape(result.source_raw, context);
	result.source_bound = rebind.Apply(result.source_raw);

	result.primary_key = StringArray(dataset_obj, "primary_key", context);
	result.unique_keys = ParseUniqueKeys(dataset_obj, context);
	result.description = OptionalString(dataset_obj, "description", context);
	result.ai_context = ParseAIContext(dataset_obj, context);
	result.nonportable_extensions_json = ParseNonportableExtensions(dataset_obj, context);

	auto fields = Member(dataset_obj, "fields");
	if (fields) {
		if (!yyjson_is_arr(fields)) {
			throw InvalidInputException("ossie_load: %s field 'fields' must be an array", context);
		}
		size_t idx, max;
		yyjson_val *item;
		yyjson_arr_foreach(fields, idx, max, item) {
			auto field = ParseField(item, result.name);
			if (result.field_index.find(field.name) != result.field_index.end()) {
				throw InvalidInputException("ossie_load: %s declares field \"%s\" more than once", context, field.name);
			}
			result.field_index[field.name] = result.fields.size();
			result.fields.push_back(std::move(field));
		}
	}
	// Only safe once every field is present, since expansion resolves references between them.
	ExpandFieldReferences(result, context);
	return result;
}

//! The structure both scopes share. The caller names the metric and resolves its references.
Metric ParseMetricBody(yyjson_val *item, const string &context) {
	ValidateKnownFields(item, {"name", "expression", "description", "datatype", "ai_context", "nonportable_extensions"},
	                    context);
	Metric metric;
	metric.expression = ParseModelExpression(item, context);
	metric.datatype = ParseDatatype(item, context);
	metric.description = OptionalString(item, "description", context);
	metric.ai_context = ParseAIContext(item, context);
	metric.nonportable_extensions_json = ParseNonportableExtensions(item, context);
	return metric;
}

//! A dataset-scoped metric resolves entirely within its dataset: a bare name is one of the dataset's
//! fields and `source.column` is a column of its source. Both are rewritten here into the forms the
//! compiler reads for any metric, `dataset.field` and `dataset.source.column`, so that once parsed a
//! metric reads the same whichever scope declared it.
void QualifyDatasetMetric(const Dataset &dataset, unique_ptr<ParsedExpression> &expr, const string &context) {
	RefuseAgg(*expr, context);
	if (expr->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		ParsedExpressionIterator::EnumerateChildren(
		    *expr, [&](unique_ptr<ParsedExpression> &child) { QualifyDatasetMetric(dataset, child, context); });
		return;
	}
	auto names = ColumnNames(expr->Cast<ColumnRefExpression>());
	if (names.size() == 1) {
		if (!dataset.FindField(names[0])) {
			throw InvalidInputException("ossie_load: %s references \"%s\", which dataset \"%s\" declares no field "
			                            "for. A dataset-scoped metric names the dataset's fields; a column of its "
			                            "source is written source.%s",
			                            context, names[0], dataset.name, names[0]);
		}
		expr = make_uniq<ColumnRefExpression>(Identifier(names[0]), Identifier(dataset.name));
		return;
	}
	// `source` names the field of that name when the dataset declares one, so the source columns are
	// then out of reach, as the spec says.
	if (names.size() == 2 && StringUtil::CIEquals(names[0], "source") && !dataset.FindField("source")) {
		vector<Identifier> parts {Identifier(dataset.name), Identifier("source"), Identifier(names[1])};
		expr = make_uniq<ColumnRefExpression>(std::move(parts));
		return;
	}
	throw InvalidInputException("ossie_load: %s references \"%s\"; a dataset-scoped metric names its dataset's "
	                            "fields by bare name and its source columns as source.column, and cannot reach "
	                            "another dataset",
	                            context, StringUtil::Join(names, "."));
}

void ParseDatasetMetrics(yyjson_val *dataset_obj, const Dataset &dataset, Model &model) {
	auto metrics = Member(dataset_obj, "metrics");
	if (!metrics) {
		return;
	}
	auto context = StringUtil::Format("dataset \"%s\"", dataset.name);
	if (!yyjson_is_arr(metrics)) {
		throw InvalidInputException("ossie_load: %s field 'metrics' must be an array", context);
	}
	size_t idx, max;
	yyjson_val *item;
	yyjson_arr_foreach(metrics, idx, max, item) {
		auto bare_name = RequiredNonEmptyString(item, "name", context + " metric");
		if (dataset.FindField(bare_name)) {
			throw InvalidInputException("ossie_load: %s declares both a field and a metric named \"%s\"; fields and "
			                            "dataset-scoped metrics share one namespace",
			                            context, bare_name);
		}
		auto metric_context = StringUtil::Format("metric \"%s.%s\"", dataset.name, bare_name);
		auto metric = ParseMetricBody(item, metric_context);
		metric.name = dataset.name + "." + bare_name;
		metric.dataset = dataset.name;
		QualifyDatasetMetric(dataset, metric.expression.tree, metric_context);
		if (model.metric_index.find(metric.name) != model.metric_index.end()) {
			throw InvalidInputException("ossie_load: %s declares metric \"%s\" more than once", context, bare_name);
		}
		model.metric_index[metric.name] = model.metrics.size();
		model.metrics.push_back(std::move(metric));
	}
}

bool HasColumnRef(const ParsedExpression &expr) {
	if (expr.GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		return true;
	}
	bool found = false;
	ParsedExpressionIterator::EnumerateChildren(expr, [&](const ParsedExpression &child) {
		if (!found) {
			found = HasColumnRef(child);
		}
	});
	return found;
}

//! An aggregate whose arguments name no column, such as COUNT(*), takes its rows from the metric's own dataset.
//! Inlined into another metric that anchor is gone, and a FILTER does not stand in for it.
bool HasGrainlessAggregate(const ParsedExpression &expr) {
	if (expr.GetExpressionClass() == ExpressionClass::FUNCTION &&
	    IsAggregateName(expr.Cast<FunctionExpression>().FunctionName().GetIdentifierName())) {
		auto &call = expr.Cast<FunctionExpression>();
		for (auto &argument : call.GetArguments()) {
			if (HasColumnRef(argument.GetExpression())) {
				return false;
			}
		}
		if (call.OrderBy()) {
			for (auto &order : call.OrderBy()->orders) {
				if (HasColumnRef(*order.expression)) {
					return false;
				}
			}
		}
		return true;
	}
	bool found = false;
	ParsedExpressionIterator::EnumerateChildren(expr, [&](const ParsedExpression &child) {
		if (!found) {
			found = HasGrainlessAggregate(child);
		}
	});
	return found;
}

//! `AGG(dataset.metric)` in a model-scoped metric stands for the dataset-scoped metric's expression.
void SubstituteAggregateReferences(const Model &model, unique_ptr<ParsedExpression> &expr, const string &context) {
	if (!IsAggCall(*expr)) {
		ParsedExpressionIterator::EnumerateChildren(
		    *expr, [&](unique_ptr<ParsedExpression> &child) { SubstituteAggregateReferences(model, child, context); });
		return;
	}
	auto &call = expr->Cast<FunctionExpression>();
	auto &arguments = call.GetArguments();
	bool ordered = call.OrderBy() && !call.OrderBy()->orders.empty();
	if (arguments.size() != 1 || arguments[0].HasName() || call.Filter() || call.Distinct() || ordered ||
	    arguments[0].GetExpression().GetExpressionClass() != ExpressionClass::COLUMN_REF ||
	    ColumnNames(arguments[0].GetExpression().Cast<ColumnRefExpression>()).size() != 2) {
		throw InvalidInputException("ossie_load: %s calls AGG with something other than a single dataset.metric "
		                            "reference; AGG(dataset.metric) evaluates a dataset-scoped metric and takes "
		                            "no FILTER clause",
		                            context);
	}
	auto reference = StringUtil::Join(ColumnNames(arguments[0].GetExpression().Cast<ColumnRefExpression>()), ".");
	auto metric = model.FindMetric(reference);
	if (!metric || metric->dataset.empty()) {
		throw InvalidInputException("ossie_load: %s calls AGG(%s), but the model declares no dataset-scoped metric "
		                            "of that name",
		                            context, reference);
	}
	if (HasGrainlessAggregate(*metric->expression.tree)) {
		throw InvalidInputException("ossie_load: %s calls AGG(%s), whose expression has an aggregate with no "
		                            "column in its arguments (for example COUNT(*)), so inlined here nothing "
		                            "would say whose rows it counts. Request %s directly instead",
		                            context, reference, reference);
	}
	expr = metric->expression.tree->Copy();
}

Relationship ParseRelationship(yyjson_val *obj) {
	ValidateKnownFields(obj,
	                    {"name", "from", "to", "from_columns", "to_columns", "ai_context", "nonportable_extensions"},
	                    "relationship");
	Relationship result;
	result.name = RequiredNonEmptyString(obj, "name", "relationship");
	auto context = StringUtil::Format("relationship \"%s\"", result.name);

	result.from_dataset = RequiredNonEmptyString(obj, "from", context);
	result.to_dataset = RequiredNonEmptyString(obj, "to", context);
	result.from_columns = StringArray(obj, "from_columns", context);
	result.to_columns = StringArray(obj, "to_columns", context);
	result.ai_context = ParseAIContext(obj, context);
	result.nonportable_extensions_json = ParseNonportableExtensions(obj, context);

	if (result.from_columns.empty() || result.to_columns.empty()) {
		throw InvalidInputException("ossie_load: %s must declare both 'from_columns' and 'to_columns'", context);
	}
	// Refuse an unequal join key since it is not expressible as an equi-join and would produce a wrong row count.
	if (result.from_columns.size() != result.to_columns.size()) {
		throw InvalidInputException("ossie_load: %s joins %s columns to %s columns; the key widths must match", context,
		                            to_string(result.from_columns.size()), to_string(result.to_columns.size()));
	}
	return result;
}

} // namespace

string RebindMap::Apply(const string &source) const {
	const string *replacement = nullptr;
	idx_t matched_length = 0;
	for (auto &rule : rules) {
		auto &prefix = rule.first;
		// Must leave something after the prefix, and must break on a dot so that 'tpcds.pub'
		// cannot match 'tpcds.public.store_sales'.
		if (source.size() <= prefix.size() || source[prefix.size()] != '.') {
			continue;
		}
		if (!StringUtil::CIEquals(source.substr(0, prefix.size()), prefix)) {
			continue;
		}
		if (prefix.size() > matched_length) {
			matched_length = prefix.size();
			replacement = &rule.second;
		}
	}
	if (!replacement) {
		return source;
	}
	return *replacement + source.substr(matched_length);
}

Model ParseModel(const string &json_text, const RebindMap &rebind, MetricReferencePolicy policy) {
	JSONDocument document(yyjson_read(json_text.c_str(), json_text.size(), 0));
	if (!document.doc) {
		throw InvalidInputException("ossie_load: file is not valid JSON");
	}
	auto root = yyjson_doc_get_root(document.doc);
	if (!yyjson_is_obj(root)) {
		throw InvalidInputException("ossie_load: the top level of the document must be a JSON object");
	}

	// Ossie puts exactly one model at the root of each document. Earlier drafts wrapped models in a
	// `semantic_model` array; the spec removed it rather than deprecating it, so an old file is
	// refused with the migration spelled out instead of being read under rules that no longer exist.
	if (Member(root, "semantic_model")) {
		throw InvalidInputException(
		    "ossie_load: this document wraps its model in a 'semantic_model' array, which Ossie 0.2.0.dev0 "
		    "removed. Each document now holds one model at its root: move the model's properties (name, "
		    "datasets, relationships, metrics, ...) to the top level and delete 'semantic_model'. A file "
		    "holding several models becomes one file per model.");
	}

	Model result;
	auto version = Member(root, "version");
	if (!yyjson_is_str(version) || string(yyjson_get_str(version), yyjson_get_len(version)) != OSSIE_SPEC_VERSION) {
		throw InvalidInputException("ossie_load: version must be the supported string '%s'", OSSIE_SPEC_VERSION);
	}
	for (auto key : {"ontology", "concept_mappings", "ontology_ref", "semantic_model_ref", "ontology_mappings"}) {
		if (Member(root, key)) {
			throw InvalidInputException("ossie_load: core model cannot contain ontology/mapping field '%s'", key);
		}
	}
	ValidateKnownFields(root,
	                    {"version", "name", "description", "ai_context", "datasets", "relationships", "metrics",
	                     "nonportable_extensions"},
	                    "semantic model");
	result.spec_version = yyjson_get_str(version);

	auto model_obj = root;
	result.name = RequiredNonEmptyString(model_obj, "name", "the semantic model");
	auto context = StringUtil::Format("semantic model \"%s\"", result.name);
	result.description = OptionalString(model_obj, "description", context);
	result.ai_context = ParseAIContext(model_obj, context);
	result.nonportable_extensions_json = ParseNonportableExtensions(model_obj, context);

	auto datasets = Member(model_obj, "datasets");
	if (!datasets || !yyjson_is_arr(datasets) || yyjson_arr_size(datasets) == 0) {
		throw InvalidInputException("ossie_load: %s requires a nonempty 'datasets' array", context);
	}
	size_t idx, max;
	yyjson_val *item;
	yyjson_arr_foreach(datasets, idx, max, item) {
		auto dataset = ParseDataset(item, rebind);
		if (result.dataset_index.find(dataset.name) != result.dataset_index.end()) {
			throw InvalidInputException("ossie_load: %s declares dataset \"%s\" more than once", context, dataset.name);
		}
		result.dataset_index[dataset.name] = result.datasets.size();
		result.datasets.push_back(std::move(dataset));
		ParseDatasetMetrics(item, result.datasets.back(), result);
	}

	auto relationships = Member(model_obj, "relationships");
	if (relationships) {
		if (!yyjson_is_arr(relationships)) {
			throw InvalidInputException("ossie_load: %s field 'relationships' must be an array", context);
		}
		yyjson_arr_foreach(relationships, idx, max, item) {
			result.relationships.push_back(ParseRelationship(item));
		}
	}

	auto metrics = Member(model_obj, "metrics");
	if (metrics) {
		if (!yyjson_is_arr(metrics)) {
			throw InvalidInputException("ossie_load: %s field 'metrics' must be an array", context);
		}
		yyjson_arr_foreach(metrics, idx, max, item) {
			auto name = RequiredNonEmptyString(item, "name", "metric");
			auto metric_context = StringUtil::Format("metric \"%s\"", name);
			auto metric = ParseMetricBody(item, metric_context);
			metric.name = name;
			if (result.metric_index.find(metric.name) != result.metric_index.end()) {
				throw InvalidInputException("ossie_load: %s declares metric \"%s\" more than once", context,
				                            metric.name);
			}
			result.metric_index[metric.name] = result.metrics.size();
			result.metrics.push_back(std::move(metric));
		}
	}
	for (auto &metric : result.metrics) {
		if (metric.dataset.empty()) {
			SubstituteAggregateReferences(result, metric.expression.tree,
			                              StringUtil::Format("metric \"%s\"", metric.name));
		}
	}

	ValidateModel(result, policy);
	ComputeCardinality(result);
	return result;
}

Model LoadModel(ClientContext &context, const string &path, const RebindMap &rebind) {
	auto &fs = FileSystem::GetFileSystem(context);
	auto handle = fs.OpenFile(path, FileFlags::FILE_FLAGS_READ);
	auto file_size = handle->GetFileSize();
	string contents(file_size, '\0');
	handle->Read(reinterpret_cast<void *>(&contents[0]), file_size);

	// The Ossie ecosystem publishes models as YAML. Converting to JSON here, rather than teaching
	// the parser a second syntax, keeps every validation guard and the whole compiler unaware of
	// how the file was serialized. Sniffing the content rather than the extension means a .txt or
	// extensionless model still works, and JSON stays the fast path.
	if (LooksLikeYaml(contents)) {
		contents = YamlToJson(contents, StringUtil::Format("\"%s\"", path));
	}
	return ParseModel(contents, rebind);
}

} // namespace ossie
} // namespace duckdb
