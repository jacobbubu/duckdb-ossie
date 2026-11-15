#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "ossie/catalog.hpp"
#include "ossie/describe_functions.hpp"
#include "ossie/functions.hpp"
#include "ossie/state.hpp"

namespace duckdb {
namespace ossie {

namespace {

//! Holds the model for the duration of the scan so a concurrent ossie_load cannot pull it away.
struct DescribeGlobalState : public GlobalTableFunctionState {
	shared_ptr<const Model> model;
	idx_t offset = 0;
	//! Only ossie_fields uses this, to walk fields nested inside datasets.
	idx_t dataset_offset = 0;
};

unique_ptr<GlobalTableFunctionState> DescribeInit(ClientContext &context, TableFunctionInitInput &input) {
	auto result = make_uniq<DescribeGlobalState>();
	result->model = OssieState::Get(context).GetModel();
	if (!result->model) {
		throw InvalidInputException("no Ossie model is loaded -- call ossie_load('model.json') first");
	}
	return std::move(result);
}

Value StringList(const vector<string> &values) {
	vector<Value> children;
	for (auto &value : values) {
		children.emplace_back(value);
	}
	return Value::LIST(LogicalType::VARCHAR, std::move(children));
}

unique_ptr<FunctionData> RelationshipsBind(ClientContext &context, TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types, vector<Identifier> &names) {
	names = {"name", "from_dataset", "to_dataset", "from_columns", "to_columns", "cardinality"};
	return_types = {LogicalType::VARCHAR,
	                LogicalType::VARCHAR,
	                LogicalType::VARCHAR,
	                LogicalType::LIST(LogicalType::VARCHAR),
	                LogicalType::LIST(LogicalType::VARCHAR),
	                LogicalType::VARCHAR};
	return nullptr;
}

void RelationshipsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &state = data_p.global_state->Cast<DescribeGlobalState>();
	auto &relationships = state.model->relationships;

	idx_t count = 0;
	while (state.offset < relationships.size() && count < STANDARD_VECTOR_SIZE) {
		auto &relationship = relationships[state.offset++];
		output.data[0].Append(Value(relationship.name));
		output.data[1].Append(Value(relationship.from_dataset));
		output.data[2].Append(Value(relationship.to_dataset));
		output.data[3].Append(StringList(relationship.from_columns));
		output.data[4].Append(StringList(relationship.to_columns));
		output.data[5].Append(Value(CardinalityName(relationship.cardinality)));
		count++;
	}
	output.CheckCardinality(count);
}

unique_ptr<FunctionData> DatasetsBind(ClientContext &context, TableFunctionBindInput &input,
                                      vector<LogicalType> &return_types, vector<Identifier> &names) {
	names = {"name", "source", "source_bound", "resolved", "primary_key", "unique_keys", "description", "synonyms"};
	return_types = {LogicalType::VARCHAR,
	                LogicalType::VARCHAR,
	                LogicalType::VARCHAR,
	                LogicalType::BOOLEAN,
	                LogicalType::LIST(LogicalType::VARCHAR),
	                LogicalType::LIST(LogicalType::LIST(LogicalType::VARCHAR)),
	                LogicalType::VARCHAR,
	                LogicalType::LIST(LogicalType::VARCHAR)};
	return nullptr;
}

void DatasetsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &state = data_p.global_state->Cast<DescribeGlobalState>();
	auto &datasets = state.model->datasets;

	idx_t count = 0;
	while (state.offset < datasets.size() && count < STANDARD_VECTOR_SIZE) {
		auto &dataset = datasets[state.offset++];
		vector<Value> unique_keys;
		for (auto &unique_key : dataset.unique_keys) {
			unique_keys.push_back(StringList(unique_key));
		}

		output.data[0].Append(Value(dataset.name));
		output.data[1].Append(Value(dataset.source_raw));
		output.data[2].Append(Value(dataset.source_bound));
		output.data[3].Append(Value::BOOLEAN(SourceResolves(context, dataset.source_bound)));
		output.data[4].Append(StringList(dataset.primary_key));
		output.data[5].Append(Value::LIST(LogicalType::LIST(LogicalType::VARCHAR), std::move(unique_keys)));
		output.data[6].Append(Value(dataset.description));
		output.data[7].Append(StringList(dataset.ai_context.synonyms));
		count++;
	}
	output.CheckCardinality(count);
}

unique_ptr<FunctionData> FieldsBind(ClientContext &context, TableFunctionBindInput &input,
                                    vector<LogicalType> &return_types, vector<Identifier> &names) {
	names = {"dataset", "name", "datatype", "is_time", "is_computed", "expression", "description", "synonyms"};
	return_types = {
	    LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::BOOLEAN,
	    LogicalType::BOOLEAN, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::LIST(LogicalType::VARCHAR)};
	return nullptr;
}

// Fields are emitted as one flat stream across datasets, so the offset indexes the pair.
void FieldsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &state = data_p.global_state->Cast<DescribeGlobalState>();
	auto &datasets = state.model->datasets;

	idx_t count = 0;
	while (state.dataset_offset < datasets.size() && count < STANDARD_VECTOR_SIZE) {
		auto &dataset = datasets[state.dataset_offset];
		if (state.offset >= dataset.fields.size()) {
			state.dataset_offset++;
			state.offset = 0;
			continue;
		}
		auto &field = dataset.fields[state.offset++];

		output.data[0].Append(Value(dataset.name));
		output.data[1].Append(Value(field.name));
		output.data[2].Append(field.datatype.empty() ? Value(LogicalType::VARCHAR) : Value(field.datatype));
		output.data[3].Append(Value::BOOLEAN(field.is_time));
		output.data[4].Append(Value::BOOLEAN(field.is_computed));
		output.data[5].Append(Value(field.expression.sql));
		output.data[6].Append(Value(field.description));
		output.data[7].Append(StringList(field.ai_context.synonyms));
		count++;
	}
	output.CheckCardinality(count);
}

unique_ptr<FunctionData> MetricsBind(ClientContext &context, TableFunctionBindInput &input,
                                     vector<LogicalType> &return_types, vector<Identifier> &names) {
	names = {"name", "datatype", "expression", "description", "synonyms"};
	return_types = {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR,
	                LogicalType::LIST(LogicalType::VARCHAR)};
	return nullptr;
}

void MetricsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &state = data_p.global_state->Cast<DescribeGlobalState>();
	auto &metrics = state.model->metrics;

	idx_t count = 0;
	while (state.offset < metrics.size() && count < STANDARD_VECTOR_SIZE) {
		auto &metric = metrics[state.offset++];
		output.data[0].Append(Value(metric.name));
		output.data[1].Append(metric.datatype.empty() ? Value(LogicalType::VARCHAR) : Value(metric.datatype));
		output.data[2].Append(Value(metric.expression.sql));
		output.data[3].Append(Value(metric.description));
		output.data[4].Append(StringList(metric.ai_context.synonyms));
		count++;
	}
	output.CheckCardinality(count);
}

// Metadata has its own surface so existing discovery table schemas remain stable.
struct MetadataGlobalState : public GlobalTableFunctionState {
	shared_ptr<const Model> model;
	vector<vector<Value>> rows;
	idx_t offset = 0;
};

Value OptionalText(const string &text) {
	return text.empty() ? Value(LogicalType::VARCHAR) : Value(text);
}

void AddMetadataRow(MetadataGlobalState &state, const string &kind, const string &dataset, const string &name,
                    const string &description, const string &label, const AIContext &ai, const string &extensions,
                    const ModelExpression *expression = nullptr, const Field *field = nullptr) {
	state.rows.push_back({Value(kind), OptionalText(dataset), Value(name), Value(description), OptionalText(label),
	                      Value(ai.instructions), StringList(ai.synonyms), StringList(ai.examples),
	                      OptionalText(ai.raw_json), OptionalText(extensions),
	                      expression ? Value(expression->dialect) : Value(LogicalType::VARCHAR),
	                      StringList(expression ? expression->available_dialects : vector<string>()),
	                      field ? Value::BOOLEAN(field->is_time_explicit) : Value(LogicalType::BOOLEAN)});
}

unique_ptr<GlobalTableFunctionState> MetadataInit(ClientContext &context, TableFunctionInitInput &input) {
	auto state = make_uniq<MetadataGlobalState>();
	state->model = OssieState::Get(context).GetModel();
	if (!state->model) {
		throw InvalidInputException("no Ossie model is loaded -- call ossie_load('model.json') first");
	}
	auto &model = *state->model;
	AddMetadataRow(*state, "model", "", model.name, model.description, "", model.ai_context,
	               model.custom_extensions_json);
	for (auto &dataset : model.datasets) {
		AddMetadataRow(*state, "dataset", "", dataset.name, dataset.description, "", dataset.ai_context,
		               dataset.custom_extensions_json);
		for (auto &field : dataset.fields) {
			AddMetadataRow(*state, "field", dataset.name, field.name, field.description, field.label, field.ai_context,
			               field.custom_extensions_json, &field.expression, &field);
		}
	}
	for (auto &relationship : model.relationships) {
		AddMetadataRow(*state, "relationship", "", relationship.name, "", "", relationship.ai_context,
		               relationship.custom_extensions_json);
	}
	for (auto &metric : model.metrics) {
		AddMetadataRow(*state, "metric", "", metric.name, metric.description, "", metric.ai_context,
		               metric.custom_extensions_json, &metric.expression);
	}
	return std::move(state);
}

unique_ptr<FunctionData> MetadataBind(ClientContext &context, TableFunctionBindInput &input,
                                      vector<LogicalType> &return_types, vector<Identifier> &names) {
	names = {"kind",
	         "dataset",
	         "name",
	         "description",
	         "label",
	         "instructions",
	         "synonyms",
	         "examples",
	         "ai_context",
	         "custom_extensions",
	         "expression_dialect",
	         "available_dialects",
	         "is_time_explicit"};
	return_types = {LogicalType::VARCHAR,
	                LogicalType::VARCHAR,
	                LogicalType::VARCHAR,
	                LogicalType::VARCHAR,
	                LogicalType::VARCHAR,
	                LogicalType::VARCHAR,
	                LogicalType::LIST(LogicalType::VARCHAR),
	                LogicalType::LIST(LogicalType::VARCHAR),
	                LogicalType::VARCHAR,
	                LogicalType::VARCHAR,
	                LogicalType::VARCHAR,
	                LogicalType::LIST(LogicalType::VARCHAR),
	                LogicalType::BOOLEAN};
	return nullptr;
}

void MetadataFunction(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<MetadataGlobalState>();
	idx_t count = 0;
	while (state.offset < state.rows.size() && count < STANDARD_VECTOR_SIZE) {
		auto &row = state.rows[state.offset++];
		for (idx_t col = 0; col < row.size(); col++) {
			output.data[col].Append(row[col]);
		}
		count++;
	}
	output.CheckCardinality(count);
}

} // namespace

//! Registers one description table function with the metadata duckdb_functions() exposes.
void RegisterDescribe(ExtensionLoader &loader, TableFunction function, const string &description,
                      const string &example) {
	CreateTableFunctionInfo info(std::move(function));
	info.descriptions.push_back(Describe({}, description, {example}));
	loader.RegisterFunction(std::move(info));
}

void RegisterDescribeFunctions(ExtensionLoader &loader) {
	RegisterDescribe(loader, TableFunction("ossie_metadata", {}, MetadataFunction, MetadataBind, MetadataInit),
	                 "Discover model, dataset, field, relationship and metric metadata, including labels, AI "
	                 "instructions/examples, original AI context and descriptive vendor extensions as JSON text. "
	                 "Extensions are preserved without interpretation. Expression dialects and explicit time roles "
	                 "are reported where applicable.",
	                 "SELECT kind, dataset, name, instructions, examples FROM ossie_metadata()");
	RegisterDescribe(loader,
	                 TableFunction("ossie_relationships", {}, RelationshipsFunction, RelationshipsBind, DescribeInit),
	                 "List the loaded model's relationships with the cardinality derived from the endpoints' declared "
	                 "keys. Ossie does not state cardinality; it is inferred by checking whether a relationship's join "
	                 "columns match the target's primary_key or a unique_keys entry, and it is what makes a fan-out "
	                 "join detectable.",
	                 "SELECT name, from_dataset, to_dataset, cardinality FROM ossie_relationships()");
	RegisterDescribe(loader, TableFunction("ossie_datasets", {}, DatasetsFunction, DatasetsBind, DescribeInit),
	                 "List the loaded model's datasets: the source as written in the model, the source after "
	                 "rebinding, whether that table currently resolves, declared keys, and synonyms.",
	                 "SELECT name, source_bound, resolved FROM ossie_datasets()");
	RegisterDescribe(loader, TableFunction("ossie_fields", {}, FieldsFunction, FieldsBind, DescribeInit),
	                 "List every field in the loaded model with its datatype, whether it is a time dimension, "
	                 "whether it is computed rather than a plain column, its expression, and its synonyms. "
	                 "This is the dimension vocabulary an agent picks names from.",
	                 "SELECT dataset, name, datatype, synonyms FROM ossie_fields()");
	RegisterDescribe(loader, TableFunction("ossie_metrics", {}, MetricsFunction, MetricsBind, DescribeInit),
	                 "List the loaded model's metrics with their datatype, aggregate expression, description "
	                 "and synonyms. This is the measure vocabulary an agent picks names from.",
	                 "SELECT name, description, synonyms FROM ossie_metrics()");
}

} // namespace ossie
} // namespace duckdb
