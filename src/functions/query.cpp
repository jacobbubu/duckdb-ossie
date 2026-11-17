#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "ossie/compile.hpp"
#include "ossie/describe_functions.hpp"
#include "ossie/functions.hpp"
#include "ossie/state.hpp"

namespace duckdb {
namespace ossie {

namespace {

// bind_replace runs before binding, so the generated SELECT is planned as though it had been
// written by hand: join order, filter pushdown and projection pruning all come from the optimizer.
unique_ptr<TableRef> QueryBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	auto &ossie_state = OssieState::Get(context);
	auto snapshot = ossie_state.GetSnapshot();
	auto &model = snapshot.model;
	if (!model) {
		throw InvalidInputException("ossie_query: no Ossie model is loaded -- call ossie_load('model.json') first");
	}

	CompileOptions options;
	options.allow_filter_functions = snapshot.allow_filter_functions;
	options.left_joins = snapshot.left_joins;

	Request request;
	if (input.inputs[0].type().id() == LogicalTypeId::VARCHAR) {
		if (input.inputs[0].IsNull()) {
			throw InvalidInputException("ossie: E_INVALID_QUERY: the query is NULL");
		}
		request = ParseRequest(input.inputs[0].GetValue<string>());
	} else {
		auto argument = [&](idx_t index) {
			return index < input.inputs.size() ? ToStringList(input.inputs[index], "ossie_query") : vector<string>();
		};
		request.metrics = argument(0);
		request.dimensions = argument(1);
		request.filters = argument(2);
	}
	for (auto &entry : input.named_parameters) {
		if (StringUtil::CIEquals(entry.first.GetIdentifierName(), "metric_filters")) {
			request.metric_filters = ToStringList(entry.second, "ossie_query");
		}
	}
	auto statement = Compile(*model, request, options);
	return make_uniq<SubqueryRef>(std::move(statement));
}

} // namespace

void RegisterQueryFunction(ExtensionLoader &loader) {
	auto list = LogicalType::LIST(LogicalType::ANY);
	auto string_list = LogicalType::LIST(LogicalType::VARCHAR);
	TableFunctionSet ossie_query("ossie_query");
	for (idx_t arity = 1; arity <= 3; arity++) {
		TableFunction variant("ossie_query", vector<LogicalType>(arity, list), nullptr);
		variant.bind_replace = QueryBindReplace;
		NameParameters(variant.GetSignature(), {"metrics", "dimensions", "filters"});
		variant.GetSignature().AddKeywordOnly("metric_filters", string_list, Value::LIST(LogicalType::VARCHAR, {}));
		ossie_query.AddFunction(std::move(variant));
	}
	TableFunction json_variant("ossie_query", {LogicalType::VARCHAR}, nullptr);
	json_variant.bind_replace = QueryBindReplace;
	NameParameters(json_variant.GetSignature(), {"query"});
	json_variant.GetSignature().AddKeywordOnly("metric_filters", string_list, Value::LIST(LogicalType::VARCHAR, {}));
	ossie_query.AddFunction(std::move(json_variant));
	CreateTableFunctionInfo info(std::move(ossie_query));
	auto describe = [&list](vector<string> params) {
		vector<LogicalType> types(params.size(), list);
		return Describe(std::move(params),
		                "Answer a question against the loaded semantic model: compile the request and "
		                "execute it. Returns one column per dimension followed by one per metric, "
		                "grouped by the dimensions. The compiled statement is handed to DuckDB before "
		                "binding, so join reordering, filter pushdown, and projection pruning come from "
		                "the optimizer. Metrics whose aggregates sit at different grains are each aggregated "
		                "over their own dataset and joined on the dimension values. Each entry of filters is "
		                "a condition on the rows of one dataset, applied before aggregation; a condition on "
		                "a metric goes in the named parameter metric_filters and is applied after. Refuses "
		                "rather than guesses where the model underdetermines the query -- joins that would "
		                "fan out, or two join paths that could give two different numbers.",
		                {"SELECT * FROM ossie_query(['total_sales'])",
		                 "SELECT * FROM ossie_query(['total_sales'], ['item.i_brand'])",
		                 "SELECT * FROM ossie_query(['total_sales'], ['item.i_brand'], "
		                 "['date_dim.d_year = 2001'])",
		                 "SELECT * FROM ossie_query(['total_sales'], ['item.i_brand'], "
		                 "metric_filters => ['total_sales > 1000'])"},
		                std::move(types));
	};
	info.descriptions.push_back(describe({"metrics"}));
	info.descriptions.push_back(describe({"metrics", "dimensions"}));
	info.descriptions.push_back(describe({"metrics", "dimensions", "filters"}));
	info.descriptions.push_back(
	    Describe({"query"},
	             "Answer a question written as one JSON object, the query form of the Ossie Wide Table "
	             "specification: metrics and dimensions are arrays and filter is one condition on the rows of a "
	             "dataset. An entry is a reference, or an object that names its output column -- {\"metric\": "
	             "reference, \"alias\": name} in metrics, {\"field\": reference, \"alias\": name} in "
	             "dimensions. A key the form does not define, or a null, is refused rather than ignored. "
	             "Conditions on metrics stay in the named parameter metric_filters.",
	             {"SELECT * FROM ossie_query('{\"metrics\": [\"total_sales\"], \"dimensions\": "
	              "[\"item.i_brand\"]}')",
	              "SELECT * FROM ossie_query('{\"metrics\": [{\"metric\": \"total_sales\", \"alias\": "
	              "\"sales\"}], \"filter\": \"date_dim.d_year = 2001\"}')"},
	             {LogicalType::VARCHAR}));
	loader.RegisterFunction(std::move(info));
}

} // namespace ossie
} // namespace duckdb
