#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "ossie/compile.hpp"
#include "ossie/describe_functions.hpp"
#include "ossie/functions.hpp"
#include "ossie/state.hpp"

namespace duckdb {
namespace ossie {

namespace {

Request ListRequest(DataChunk &args, idx_t row) {
	Request request;
	request.metrics = ToStringList(args.data[0].GetValue(row), "ossie_compile");
	request.dimensions = ToStringList(args.data[1].GetValue(row), "ossie_compile");
	request.filters = ToStringList(args.data[2].GetValue(row), "ossie_compile");
	if (args.ColumnCount() > 3) {
		request.metric_filters = ToStringList(args.data[3].GetValue(row), "ossie_compile");
	}
	return request;
}

Request JsonRequest(DataChunk &args, idx_t row) {
	auto query = args.data[0].GetValue(row);
	if (query.IsNull()) {
		throw InvalidInputException("ossie: E_INVALID_QUERY: the query is NULL");
	}
	auto request = ParseRequest(query.GetValue<string>());
	if (args.ColumnCount() > 1) {
		request.metric_filters = ToStringList(args.data[1].GetValue(row), "ossie_compile");
	}
	return request;
}

void CompileRows(DataChunk &args, ExpressionState &state, Vector &result, Request (*read)(DataChunk &, idx_t)) {
	auto &context = state.GetContext();
	auto &ossie_state = OssieState::Get(context);
	auto snapshot = ossie_state.GetSnapshot();
	auto &model = snapshot.model;
	if (!model) {
		throw InvalidInputException("ossie_compile: no Ossie model is loaded -- call ossie_load('model.json') first");
	}

	CompileOptions options;
	options.allow_filter_functions = snapshot.allow_filter_functions;
	options.left_joins = snapshot.left_joins;
	for (idx_t row = 0; row < args.size(); row++) {
		result.SetValue(row, Value(Compile(*model, read(args, row), options)->ToString()));
	}
}

void CompileFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	CompileRows(args, state, result, ListRequest);
}

void CompileJsonFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	CompileRows(args, state, result, JsonRequest);
}

} // namespace

vector<string> ToStringList(const Value &value, const char *function_name) {
	vector<string> result;
	if (value.IsNull()) {
		return result;
	}
	for (auto &child : ListValue::GetChildren(value)) {
		if (child.IsNull()) {
			throw InvalidInputException("%s: argument lists may not contain NULL", function_name);
		}
		result.push_back(child.GetValue<string>());
	}
	return result;
}

void RegisterCompileFunction(ExtensionLoader &loader) {
	auto string_list = LogicalType::LIST(LogicalType::VARCHAR);
	ScalarFunction lists("ossie_compile", {string_list, string_list, string_list}, LogicalType::VARCHAR,
	                     CompileFunction);
	NameParameters(lists.GetSignature(), {"metrics", "dimensions", "filters"});
	ScalarFunction json("ossie_compile", {LogicalType::VARCHAR}, LogicalType::VARCHAR, CompileJsonFunction);
	NameParameters(json.GetSignature(), {"query"});

	ScalarFunctionSet ossie_compile("ossie_compile");
	for (auto variant : {&lists, &json}) {
		variant->GetSignature().AddKeywordOnly("metric_filters", string_list, Value::LIST(LogicalType::VARCHAR, {}));

		// Model/policy is captured at execution per scalar chunk, never constant-folded at PREPARE.
		variant->SetVolatile();

		// Every refusal is thrown from here, and its text is part of the interface. A function that throws must
		// say so, or DuckDB reports the error as an INTERNAL one and the message an agent retries on is lost.
		variant->SetFallible();
		ossie_compile.AddFunction(*variant);
	}
	CreateScalarFunctionInfo info(std::move(ossie_compile));
	// duckdb_functions() matches a description to a scalar overload on every parameter's type, the keyword-only
	// one included, so each description lists metric_filters too.
	info.descriptions.push_back(
	    Describe({"metrics", "dimensions", "filters", "metric_filters"},
	             "Compile a semantic request into SQL and return it as text without executing it. Runs the "
	             "identical compiler ossie_query does, so the SQL shown here is the SQL that would run. Useful "
	             "wherever DuckDB is not the executor, and needs no tables to exist. Each entry of filters is "
	             "a condition on the rows of one dataset; a condition on a metric goes in the named parameter "
	             "metric_filters.",
	             {"SELECT ossie_compile(['total_sales'], ['item.i_brand'], [])",
	              "SELECT ossie_compile(['total_sales'], [], ['date_dim.d_year = 2001'])",
	              "SELECT ossie_compile(['total_sales'], ['item.i_brand'], [], "
	              "metric_filters => ['total_sales > 1000'])"},
	             {string_list, string_list, string_list, string_list}));
	info.descriptions.push_back(
	    Describe({"query", "metric_filters"},
	             "Compile a request written as one JSON object, the query form of the Ossie Wide Table "
	             "specification, and return the SQL as text without executing it. Takes exactly what "
	             "ossie_query takes in this form: metrics, dimensions and filter, where an entry may be an "
	             "object that names its output column with an alias.",
	             {"SELECT ossie_compile('{\"metrics\": [\"total_sales\"], \"dimensions\": "
	              "[\"item.i_brand\"]}')",
	              "SELECT ossie_compile('{\"metrics\": [{\"metric\": \"total_sales\", \"alias\": "
	              "\"sales\"}]}')"},
	             {LogicalType::VARCHAR, string_list}));
	loader.RegisterFunction(std::move(info));
}

} // namespace ossie
} // namespace duckdb
