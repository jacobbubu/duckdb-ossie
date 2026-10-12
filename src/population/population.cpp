#include "ossie/compile.hpp"
#include "ossie/functions.hpp"
#include "ossie/describe_functions.hpp"
#include "ossie/parser.hpp"
#include "ossie/population_testing.hpp"
#include "ossie/relation_program.hpp"
#include "ossie/fact_program.hpp"
#include "ossie/history_program.hpp"
#include "ossie/dialect.hpp"
#include "ossie/yaml.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/entry_lookup_info.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/error_data.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/external_dependencies.hpp"
#include "duckdb/common/types/column/column_data_collection.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/main/materialized_query_result.hpp"
#include "duckdb/main/pending_query_result.hpp"
#include "duckdb/main/prepared_statement.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/expression/case_expression.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/conjunction_expression.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/window_expression.hpp"
#include "duckdb/parser/expression/type_expression.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/qualified_name.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
#include "duckdb/storage/object_cache.hpp"
#include "yyjson.hpp"
#include <atomic>
#include <cmath>
#include <thread>
#include <algorithm>
#include <map>
#include <set>

namespace duckdb {
namespace ossie {
using duckdb_yyjson::yyjson_doc;
using duckdb_yyjson::yyjson_doc_free;
using duckdb_yyjson::yyjson_doc_get_root;
using duckdb_yyjson::yyjson_get_len;
using duckdb_yyjson::yyjson_get_str;
using duckdb_yyjson::yyjson_is_arr;
using duckdb_yyjson::yyjson_is_obj;
using duckdb_yyjson::yyjson_is_str;
using duckdb_yyjson::yyjson_obj_get;
using duckdb_yyjson::yyjson_read;
using duckdb_yyjson::yyjson_val;
namespace {

constexpr const char *PROFILE = "gaap-scalar-population-v1";
constexpr const char *JOIN_PROFILE = "fact-dimension-population-v1";
constexpr const char *OBSERVATION_PROFILE = "daily-observation-population-v1";
constexpr const char *RELATION_PROFILE = "observation-relation-program-v1";
constexpr const char *FACT_PROFILE = "multi-fact-user-program-v1";
constexpr const char *HISTORY_PROFILE = "historical-observation-relation-v1";
constexpr idx_t MAX_ROWS = 10000;
constexpr idx_t DAILY_REQUEST_BYTES = 16384;
constexpr idx_t DAILY_RESULT_BYTES = 1048576;
constexpr idx_t DAILY_MAX_DAYS = 366;
constexpr idx_t DAILY_MAX_PRODUCTS = 64;
constexpr idx_t DAILY_PRODUCT_BYTES = 256;
constexpr idx_t MAX_BYTES = idx_t(16) * 1024 * 1024;
uint64_t NextBatchSequence() {
	static std::atomic<uint64_t> sequence {0};
	return ++sequence;
}

string JSONString(const string &value);
constexpr const char *ERROR_SCHEMA = "ossie-population-error-v1";
constexpr const char *ERROR_MARKER = "ossie_population_error_v1: ";
constexpr const char *ERROR_INFO = "ossie_population_error_json";

string ErrorCode(const string &stage) {
	if (stage == "constraint") {
		return "CONSTRAINT_VIOLATION";
	}
	if (stage == "resource") {
		return "RESOURCE_LIMIT_EXCEEDED";
	}
	if (stage == "capability") {
		return "UNSUPPORTED_CAPABILITY";
	}
	if (stage == "identity") {
		return "IDENTITY_VIOLATION";
	}
	if (stage == "execute") {
		return "EXECUTION_FAILED";
	}
	if (stage == "load") {
		return "LOAD_FAILED";
	}
	if (stage == "snapshot") {
		return "SNAPSHOT_REJECTED";
	}
	if (stage == "mapping") {
		return "MAPPING_INVALID";
	}
	if (stage == "rule") {
		return "RULE_REJECTED";
	}
	if (stage == "parse") {
		return "DEFINITION_INVALID";
	}
	if (stage == "hash") {
		return "DEFINITION_HASH_MISMATCH";
	}
	return "INVALID_REQUEST";
}

[[noreturn]] void PopulationError(const string &object, const string &stage, const string &reason, ExceptionType type,
                                  const string &source_type, unordered_map<string, string> extra = {}) {
	// legacy_message is diagnostic compatibility, not the machine capability field.
	auto legacy = "ossie_population: object=" + object + " stage=" + stage + " reason=" + reason +
	              " missing_capability=bounded population profile";
	auto payload = "{\"schema_version\":" + JSONString(ERROR_SCHEMA) + ",\"code\":" + JSONString(ErrorCode(stage)) +
	               ",\"object\":" + JSONString(object) + ",\"stage\":" + JSONString(stage) +
	               ",\"reason\":" + JSONString(reason) +
	               ",\"missing_capability\":" + (stage == "capability" ? JSONString(reason) : "null") +
	               ",\"exception_type\":" + JSONString(Exception::ExceptionTypeToString(type)) +
	               ",\"source_exception_type\":" + (source_type.empty() ? "null" : JSONString(source_type)) +
	               ",\"legacy_message\":" + JSONString(legacy) + "}";
	extra[ERROR_INFO] = payload;
	throw Exception(extra, type, string(ERROR_MARKER) + payload);
}

[[noreturn]] void Fail(const string &object, const string &stage, const string &reason) {
	PopulationError(object, stage, reason, ExceptionType::INVALID_INPUT, "");
}

bool RecoverableError(ExceptionType type) {
	switch (type) {
	case ExceptionType::INVALID_INPUT:
	case ExceptionType::BINDER:
	case ExceptionType::PARSER:
	case ExceptionType::CATALOG:
	case ExceptionType::CONVERSION:
	case ExceptionType::OUT_OF_RANGE:
	case ExceptionType::IO:
	case ExceptionType::NOT_IMPLEMENTED:
	case ExceptionType::CONSTRAINT:
	case ExceptionType::INVALID_TYPE:
		return true;
	default:
		return false;
	}
}

[[noreturn]] void NormalizeError(const ErrorData &error, const string &object, const string &stage,
                                 ExceptionType reported_type) {
	if (!RecoverableError(error.Type())) {
		error.Throw();
	}
	auto own = error.ExtraInfo().find(ERROR_INFO);
	if (own != error.ExtraInfo().end()) {
		auto doc = yyjson_read(own->second.data(), own->second.size(), 0);
		bool valid = doc && yyjson_is_obj(yyjson_doc_get_root(doc));
		if (valid) {
			auto schema = yyjson_obj_get(yyjson_doc_get_root(doc), "schema_version");
			valid = yyjson_is_str(schema) && string(yyjson_get_str(schema), yyjson_get_len(schema)) == ERROR_SCHEMA;
		}
		if (doc) {
			yyjson_doc_free(doc);
		}
		if (valid) {
			error.Throw();
		}
	}
	PopulationError(object, stage, error.Message(), reported_type, Exception::ExceptionTypeToString(error.Type()),
	                error.ExtraInfo());
}

template <class WORK>
auto ErrorBoundary(const string &object, const string &stage, WORK work,
                   ExceptionType translated_type = ExceptionType::INVALID) -> decltype(work()) {
	try {
		return work();
	} catch (const std::bad_alloc &) {
		throw;
	} catch (const std::exception &error) {
		auto original = std::current_exception();
		ErrorData data;
		try {
			data = ErrorData(error);
		} catch (...) {
			std::rethrow_exception(original);
		}
		// Avoid relying on class RTTI across the extension/host boundary.
		// Unknown standard exceptions, interrupts and serious failures retain their original exception.
		if (!RecoverableError(data.Type())) {
			throw;
		}
		NormalizeError(data, object, stage, translated_type == ExceptionType::INVALID ? data.Type() : translated_type);
	}
}

string Quote(const string &name) {
	return "\"" + StringUtil::Replace(name, "\"", "\"\"") + "\"";
}

template <class QUERY>
unique_ptr<QueryResult> Run(ClientContext &caller, Connection &connection, QUERY sql) {
	auto pending = connection.PendingQuery(std::move(sql));
	if (pending->HasError()) {
		NormalizeError(pending->GetErrorObject(), "query", "bind", ExceptionType::INVALID_INPUT);
	}
	while (true) {
		if (caller.IsInterrupted()) {
			connection.Interrupt();
			// PendingQuery schedules tasks before the first ExecuteTask. Its destructor
			// does not drain them, and ClientContext skips Destroy during unwinding.
			// Finish cancellation while the nested context and task notifiers are alive.
			connection.context->CancelTransaction();
			throw InterruptException();
		}
		auto status = pending->ExecuteTask();
		if (status == PendingExecutionResult::EXECUTION_ERROR) {
			NormalizeError(pending->GetErrorObject(), "query", "execute", ExceptionType::INVALID_INPUT);
		}
		if (PendingQueryResult::IsResultReady(status)) {
			break;
		}
		std::this_thread::yield();
	}
	auto result = pending->Execute();
	if (result->HasError()) {
		NormalizeError(result->GetErrorObject(), "query", "execute", ExceptionType::INVALID_INPUT);
	}
	return result;
}

string Hash(Connection &connection, const string &bytes) {
	auto result = connection.Query("SELECT system.main.sha256(?)", Value(bytes));
	if (result->HasError()) {
		NormalizeError(result->GetErrorObject(), "hash", "load", ExceptionType::INVALID_INPUT);
	}
	return result->Fetch()->GetValue(0, 0).GetValue<string>();
}

string Read(ClientContext &context, const string &path) {
	if (path.find("://") != string::npos) {
		Fail(path, "load", "only explicit local paths are supported");
	}
	auto handle = FileSystem::GetFileSystem(context).OpenFile(path, FileFlags::FILE_FLAGS_READ);
	auto size = handle->GetFileSize();
	if (size > idx_t(1024) * 1024) {
		Fail(path, "load", "definition exceeds 1 MiB limit");
	}
	string bytes(size, '\0');
	if (size) {
		handle->Read(&bytes[0], size);
	}
	return bytes;
}

struct Document {
	yyjson_doc *doc;
	explicit Document(const string &bytes) {
		auto json = LooksLikeYaml(bytes) ? YamlToJson(bytes, "population definition") : bytes;
		doc = yyjson_read(json.data(), json.size(), 0);
		if (!doc || !yyjson_is_obj(yyjson_doc_get_root(doc))) {
			if (doc) {
				yyjson_doc_free(doc);
			}
			Fail("definition", "parse", "expected an object document");
		}
	}
	~Document() {
		yyjson_doc_free(doc);
	}
	Document(const Document &) = delete;
	void NoNUL() const {
		vector<yyjson_val *> pending {Root()};
		while (!pending.empty()) {
			auto value = pending.back();
			pending.pop_back();
			if (yyjson_is_str(value) &&
			    string(yyjson_get_str(value), yyjson_get_len(value)).find('\0') != string::npos) {
				Fail("definition", "parse", "NUL-containing strings prohibited in this profile");
			}
			size_t index, max;
			yyjson_val *child, *key;
			if (yyjson_is_arr(value)) {
				yyjson_arr_foreach(value, index, max, child) {
					pending.push_back(child);
				}
			}
			if (yyjson_is_obj(value)) {
				yyjson_obj_foreach(value, index, max, key, child) {
					pending.push_back(key);
					pending.push_back(child);
				}
			}
		}
	}
	yyjson_val *Root() const {
		return yyjson_doc_get_root(doc);
	}
};

yyjson_val *Member(yyjson_val *object, const char *key) {
	return yyjson_is_obj(object) ? yyjson_obj_get(object, key) : nullptr;
}

string Text(yyjson_val *object, const char *key) {
	auto value = Member(object, key);
	if (!yyjson_is_str(value)) {
		Fail(key, "parse", "required nonempty string");
	}
	string result(yyjson_get_str(value), yyjson_get_len(value));
	if (result.empty() || result.find('\0') != string::npos) {
		Fail(key, "parse", "empty or NUL-containing string");
	}
	return result;
}

vector<yyjson_val *> Array(yyjson_val *object, const char *key, bool required = false) {
	auto value = Member(object, key);
	if (!value && !required) {
		return {};
	}
	if (!yyjson_is_arr(value)) {
		Fail(key, "parse", "expected array");
	}
	vector<yyjson_val *> result;
	size_t index, max;
	yyjson_val *item;
	yyjson_arr_foreach(value, index, max, item) {
		result.push_back(item);
	}
	return result;
}

string ArrayText(yyjson_val *value) {
	if (!yyjson_is_str(value)) {
		Fail("array", "parse", "expected string element");
	}
	return string(yyjson_get_str(value), yyjson_get_len(value));
}

void Keys(yyjson_val *value, std::initializer_list<const char *> allowed, const string &object) {
	if (!yyjson_is_obj(value)) {
		Fail(object, "parse", "expected object");
	}
	size_t index, max;
	yyjson_val *key, *entry;
	yyjson_obj_foreach(value, index, max, key, entry) {
		string name(yyjson_get_str(key), yyjson_get_len(key));
		bool found = false;
		for (auto candidate : allowed) {
			found |= name == candidate;
		}
		if (!found) {
			Fail(object, "capability", "unsupported property " + name);
		}
	}
}

void Empty(yyjson_val *value, const char *key, const string &object) {
	if (!Array(value, key).empty()) {
		Fail(object, "capability", string("unsupported nonempty ") + key);
	}
}

void Version(yyjson_val *root) {
	if (Text(root, "version") != "0.2.0.dev0") {
		Fail("version", "parse", "supported version is 0.2.0.dev0");
	}
}

struct Attribute {
	string name;
	string target;
	string field;
	bool unique;
};

struct CaptureEvidence {
	string dataset, source, sha256;
	idx_t rows;
};

struct HistorySource {
	string dataset, concept;
	vector<string> roles, fields;
};

struct Bundle {
	unique_ptr<HistoryIndex> history_index;
	vector<HistorySource> history_sources;
	vector<shared_ptr<Bundle>> history_children;
	string history_relationship, history_target;
	unique_ptr<FactProgram> fact_program;
	FactProgramInput fact_input;
	vector<shared_ptr<Bundle>> fact_children;
	unique_ptr<RelationProgram> relation_program;
	RelationProgramInput relation_input;
	Model model;
	// Parsed/lowered definition model, before replacing source names and flattening fields.
	Model definition_model;
	// Types come from the successful production load preflight, never from datatype labels.
	case_insensitive_map_t<string> field_types, metric_types;
	string ontology_name, ontology_version, mapping_name, mapping_version;
	vector<CaptureEvidence> inputs;
	string capture_join = "null";
	string profile = PROFILE;
	// Original logical names survive flattening; generated fields are never a public namespace.
	vector<pair<vector<string>, string>> provenance;
	string execution_target = "frozen typed observation batch (AST dependency required)";
	string root, derived, identity, original_source;
	string manifest_hash, core_hash, ontology_hash, mapping_hash;
	string snapshot_id, content_hash, original_rule;
	idx_t rows = 0;
	string user_concept, user_id_relation, identity_relation, amount_field, class_field;
	vector<string> observation_identity, observation_relationships;
	idx_t user_count = 0;
	vector<Attribute> attributes;
	unique_ptr<ParsedExpression> rule;
	weak_ptr<Bundle> owner;
	unique_ptr<ColumnDataCollection> collection;
	vector<string> physical_names;
	weak_ptr<DatabaseInstance> database;
	mutex query_lock;
};

// Cached bundles must not own a connection back to their containing database.
// Each operation owns a short-lived connection; collection lifetime follows the caller DB.
unique_ptr<Connection> Engine(Bundle &bundle) {
	auto database = bundle.database.lock();
	if (!database) {
		Fail("bundle", "execute", "containing database has closed");
	}
	auto engine = make_uniq<Connection>(*database);
	if (bundle.profile == OBSERVATION_PROFILE || bundle.profile == RELATION_PROFILE || bundle.profile == FACT_PROFILE ||
	    bundle.profile == HISTORY_PROFILE) {
		// Pin the private connection only: global/caller defaults must not merge raw identities.
		auto configured = engine->Query("SET SESSION default_collation='binary'");
		if (configured->HasError()) {
			NormalizeError(configured->GetErrorObject(), "observation collation", "snapshot",
			               ExceptionType::INVALID_INPUT);
		}
	}
	return engine;
}

class PopulationState : public ObjectCacheEntry {
public:
	static string ObjectType() {
		return "ossie_population";
	}
	static PopulationState &Get(ClientContext &context) {
		return *ObjectCache::GetObjectCache(context).GetOrCreate<PopulationState>("ossie_population");
	}
	string GetObjectType() override {
		return "ossie_population";
	}
	optional_idx GetEstimatedCacheMemory() const override {
		return optional_idx();
	}
	shared_ptr<Bundle> GetBundle() {
		lock_guard<mutex> guard(lock);
		return bundle;
	}
	void SetBundle(shared_ptr<Bundle> value) {
		lock_guard<mutex> guard(lock);
		bundle = std::move(value);
	}

private:
	mutex lock;
	shared_ptr<Bundle> bundle;
};

// A syntactic guard, not a guarantee of exact or finite floating-point results.
bool ZeroProtectedDenominator(const ParsedExpression &expression) {
	if (expression.GetExpressionClass() != ExpressionClass::FUNCTION) {
		return false;
	}
	auto &function = expression.Cast<FunctionExpression>();
	if (function.is_operator || !function.catalog.empty() || !function.schema.empty() ||
	    !StringUtil::CIEquals(function.function_name, "nullif") || function.children.size() != 2) {
		return false;
	}
	auto &zero = *function.children[1];
	if (zero.GetExpressionClass() != ExpressionClass::CONSTANT) {
		return false;
	}
	auto &value = zero.Cast<ConstantExpression>().value;
	return !value.IsNull() && value.type().IsNumeric() && value == Value::INTEGER(0);
}

// A separate strict profile checker never inherits the core filter-function flag.
void Strict(const ParsedExpression &expression, bool aggregates = false, bool inside_aggregate = false, idx_t depth = 0,
            idx_t *visited = nullptr) {
	idx_t nodes = 0;
	if (!visited) {
		visited = &nodes;
	}
	if (++(*visited) > 1024 || depth > 32) {
		Fail(expression.ToString(), "rule", "expression exceeds 1024 nodes or depth 32");
	}
	switch (expression.GetExpressionClass()) {
	case ExpressionClass::COLUMN_REF:
	case ExpressionClass::CONSTANT:
	case ExpressionClass::COMPARISON:
	case ExpressionClass::CONJUNCTION:
	case ExpressionClass::CASE:
	case ExpressionClass::BETWEEN:
		break;
	case ExpressionClass::CAST: {
		auto &cast = expression.Cast<CastExpression>();
		auto target = cast.cast_type;
		if (target.IsUnbound()) {
			auto &type = UnboundType::GetTypeExpression(target)->Cast<TypeExpression>();
			if (!type.GetCatalog().empty() || !type.GetSchema().empty()) {
				Fail(expression.ToString(), "rule", "qualified casts not supported");
			}
			target = UnboundType::TryDefaultBind(target);
		}
		auto id = target.id();
		if (id != LogicalTypeId::DATE && id != LogicalTypeId::INTEGER && id != LogicalTypeId::BIGINT &&
		    id != LogicalTypeId::DECIMAL && id != LogicalTypeId::DOUBLE && id != LogicalTypeId::VARCHAR &&
		    id != LogicalTypeId::BOOLEAN) {
			Fail(expression.ToString(), "rule", "unsupported cast");
		}
		break;
	}
	case ExpressionClass::OPERATOR:
		if (expression.GetExpressionType() != ExpressionType::OPERATOR_NOT &&
		    expression.GetExpressionType() != ExpressionType::OPERATOR_IS_NULL &&
		    expression.GetExpressionType() != ExpressionType::OPERATOR_IS_NOT_NULL &&
		    expression.GetExpressionType() != ExpressionType::OPERATOR_COALESCE) {
			Fail(expression.ToString(), "rule", "unsupported operator");
		}
		break;
	case ExpressionClass::FUNCTION: {
		auto &function = expression.Cast<FunctionExpression>();
		auto name = StringUtil::Lower(function.function_name);
		bool aggregate = name == "sum" || name == "count" || name == "avg";
		bool division = function.is_operator && name == "/";
		bool arithmetic = function.is_operator && (name == "+" || name == "-" || name == "*" || division);
		bool nullif = !function.is_operator && name == "nullif" && function.children.size() == 2;
		if (!function.catalog.empty() || !function.schema.empty() || function.filter ||
		    (function.order_bys && !function.order_bys->orders.empty()) || function.export_state ||
		    (!arithmetic && !aggregate && !nullif) || (aggregate && (!aggregates || inside_aggregate)) ||
		    (!aggregate && function.distinct) || (aggregate && function.children.size() != 1)) {
			Fail(expression.ToString(), "rule", "unsupported function or aggregate in scalar population");
		}
		if (division && (function.children.size() != 2 || !ZeroProtectedDenominator(*function.children[1]))) {
			Fail(expression.ToString(), "rule",
			     "division requires a direct NULLIF(expr, numeric zero constant) denominator");
		}
		for (auto &child : function.children) {
			Strict(*child, aggregates, inside_aggregate || aggregate, depth + 1, visited);
		}
		return;
	}
	default:
		Fail(expression.ToString(), "rule", "unsupported AST construct (subquery/window/parameter prohibited)");
	}
	ParsedExpressionIterator::EnumerateChildren(expression, [&](const ParsedExpression &child) {
		Strict(child, aggregates, inside_aggregate, depth + 1, visited);
	});
}

bool HasAggregate(const ParsedExpression &expression) {
	if (expression.GetExpressionClass() == ExpressionClass::FUNCTION) {
		auto name = StringUtil::Lower(expression.Cast<FunctionExpression>().function_name);
		if (name == "sum" || name == "count" || name == "avg") {
			return true;
		}
	}
	bool found = false;
	ParsedExpressionIterator::EnumerateChildren(
	    expression, [&](const ParsedExpression &child) { found = found || HasAggregate(child); });
	return found;
}

unique_ptr<ParsedExpression> Parse(const string &sql) {
	if (sql.empty() || sql.find('\0') != string::npos) {
		Fail("expression", "rule", "nonempty SQL without NUL required");
	}
	if (sql.size() > 16384) {
		Fail("expression", "rule", "request/rule exceeds 16 KiB SQL limit");
	}
	vector<unique_ptr<ParsedExpression>> expressions;
	try {
		expressions = Parser::ParseExpressionList(sql);
	} catch (const ParserException &error) {
		NormalizeError(ErrorData(error), sql, "rule", ExceptionType::INVALID_INPUT);
	}
	if (expressions.size() != 1) {
		Fail(sql, "rule", "exactly one expression required");
	}
	return std::move(expressions[0]);
}

void Qualify(unique_ptr<ParsedExpression> &expression, const string &dataset) {
	if (expression->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto &column = expression->Cast<ColumnRefExpression>();
		if (column.column_names.size() > 2 ||
		    (column.column_names.size() == 2 && !StringUtil::CIEquals(column.column_names[0], dataset))) {
			Fail(expression->ToString(), "bind", "physical reference crosses dataset");
		}
		expression = make_uniq<ColumnRefExpression>(column.column_names.back(), dataset);
		return;
	}
	ParsedExpressionIterator::EnumerateChildren(*expression,
	                                            [&](unique_ptr<ParsedExpression> &child) { Qualify(child, dataset); });
}

unique_ptr<ParsedExpression> FieldTree(const Bundle &bundle, const string &field_name) {
	auto &dataset = bundle.model.datasets[0];
	auto field = dataset.FindField(field_name);
	if (!field) {
		Fail(field_name, "bind", "mapping/request must reference a declared logical field");
	}
	auto expression = field->expression.tree->Copy();
	Strict(*expression);
	Qualify(expression, dataset.name);
	return expression;
}

string LogicalName(const Bundle &bundle, const vector<string> &names) {
	if (!bundle.provenance.empty()) {
		for (auto &entry : bundle.provenance) {
			if (names.size() == 2 && StringUtil::CIEquals(names[0], entry.first[0]) &&
			    StringUtil::CIEquals(names[1], entry.first[1])) {
				return entry.second;
			}
		}
		Fail("reference", "bind", "unknown original dataset.field");
	}
	if (names.size() != 2 || !StringUtil::CIEquals(names[0], bundle.model.datasets[0].name) ||
	    !bundle.model.datasets[0].FindField(names[1])) {
		Fail("reference", "bind", "mapping must reference a declared field in the single root dataset");
	}
	return bundle.model.datasets[0].FindField(names[1])->name;
}

string OriginalName(const Bundle &bundle, const string &field) {
	for (auto &entry : bundle.provenance) {
		if (entry.second == field) {
			return entry.first[0] + "." + entry.first[1];
		}
	}
	return bundle.model.datasets[0].name + "." + field;
}

string LogicalField(const Bundle &bundle, const string &sql) {
	auto expression = Parse(sql);
	if (expression->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		Fail(sql, "mapping", "mapping must be a single dataset.field reference");
	}
	auto &names = expression->Cast<ColumnRefExpression>().column_names;
	return LogicalName(bundle, names);
}

void Resolve(const Bundle &bundle, unique_ptr<ParsedExpression> &expression, bool ontology_only = false) {
	if (expression->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto &names = expression->Cast<ColumnRefExpression>().column_names;
		if (names.size() != 2) {
			Fail(expression->ToString(), "rule", "scalar references must be qualified");
		}
		string field;
		if (StringUtil::CIEquals(names[0], bundle.root) || StringUtil::CIEquals(names[0], bundle.derived)) {
			for (auto &attribute : bundle.attributes) {
				if (StringUtil::CIEquals(names[1], attribute.name)) {
					field = attribute.field;
				}
			}
		} else if (!ontology_only) {
			field = LogicalName(bundle, names);
		}
		if (field.empty()) {
			Fail(expression->ToString(), "rule", "unknown or unmapped concept attribute");
		}
		expression = FieldTree(bundle, field);
		return;
	}
	ParsedExpressionIterator::EnumerateChildren(
	    *expression, [&](unique_ptr<ParsedExpression> &child) { Resolve(bundle, child, ontology_only); });
}

string ObjectIdentity(const Bundle &bundle, yyjson_val *object, const string &identity_relationship) {
	Keys(object, {"concept", "referent_mappings"}, "root object mapping");
	if (Member(object, "concept") && Text(object, "concept") != bundle.root) {
		Fail(bundle.root, "mapping", "root concept override");
	}
	auto referents = Array(object, "referent_mappings", true);
	if (referents.size() != 1) {
		Fail(bundle.root, "mapping", "identity must have one referent");
	}
	Keys(referents[0], {"relationship", "expression"}, "identity referent");
	if (Text(referents[0], "relationship") != identity_relationship) {
		Fail(bundle.root, "mapping", "identity relationship mismatch");
	}
	return LogicalField(bundle, Text(referents[0], "expression"));
}

void Definitions(Bundle &bundle, yyjson_val *ontology, yyjson_val *mapping) {
	Keys(ontology, {"version", "name", "description", "ai_context", "ontology", "requires"}, "ontology");
	Version(ontology);
	Empty(ontology, "requires", "ontology");
	auto components = Array(ontology, "ontology", true);
	yyjson_val *parent = nullptr, *child = nullptr;
	for (auto component : components) {
		Keys(component,
		     {"concept", "type", "description", "ai_context", "identify_by", "extends", "derived_by", "requires",
		      "relationships"},
		     "concept");
		Empty(component, "requires", "concept");
		if (Text(component, "type") != "EntityType") {
			Fail(Text(component, "concept"), "capability", "explicit value types are not supported in this profile");
		}
		if (Array(component, "extends").empty()) {
			if (parent) {
				Fail("ontology", "capability", "exactly one root entity required");
			}
			parent = component;
		} else {
			if (child) {
				Fail("ontology", "capability", "exactly one derived entity required");
			}
			child = component;
		}
	}
	if (!parent || !child || components.size() != 2) {
		Fail("ontology", "capability", "profile requires one root and one derived entity");
	}
	bundle.root = Text(parent, "concept");
	bundle.derived = Text(child, "concept");
	if (StringUtil::CIEquals(bundle.root, bundle.derived)) {
		Fail(bundle.root, "bind", "duplicate concept name");
	}
	auto extends = Array(child, "extends");
	if (extends.size() != 1 || ArrayText(extends[0]) != bundle.root) {
		Fail(bundle.derived, "capability", "single inheritance from root required");
	}
	Empty(child, "identify_by", bundle.derived);
	Empty(child, "relationships", bundle.derived);
	Empty(parent, "derived_by", bundle.root);
	auto identity = Array(parent, "identify_by", true);
	if (identity.size() != 1) {
		Fail(bundle.root, "capability", "single identity relationship required");
	}
	auto id_relation = ArrayText(identity[0]);
	bundle.identity_relation = id_relation;
	case_insensitive_set_t relation_names;
	for (auto relationship : Array(parent, "relationships", true)) {
		Keys(relationship,
		     {"name", "roles", "multiplicity", "description", "ai_context", "verbalizes", "requires", "derived_by"},
		     "relationship");
		Empty(relationship, "requires", "relationship");
		Empty(relationship, "derived_by", "relationship");
		auto name = Text(relationship, "name");
		if (!relation_names.insert(name).second) {
			Fail(name, "bind", "duplicate relationship");
		}
		auto roles = Array(relationship, "roles", true);
		if (roles.size() != 1) {
			Fail(name, "capability", "only binary scalar relationships supported");
		}
		Keys(roles[0], {"concept"}, name);
		auto target = Text(roles[0], "concept");
		if (target != "String" && target != "Date" && target != "Decimal" && target != "Integer" && target != "Float" &&
		    target != "Boolean") {
			Fail(name, "capability", "unsupported target value type");
		}
		auto multiplicity = Text(relationship, "multiplicity");
		if (multiplicity != "ManyToOne" && multiplicity != "OneToOne") {
			Fail(name, "capability", "scalar multiplicity required");
		}
		if (name == id_relation && (multiplicity != "OneToOne" || target != "String")) {
			Fail(name, "capability", "identity requires OneToOne String");
		}
		bundle.attributes.push_back({name, target, "", multiplicity == "OneToOne"});
	}
	Keys(mapping,
	     {"version", "name", "description", "ontology_ref", "semantic_model_ref", "concept_mappings",
	      "custom_extensions"},
	     "mapping");
	Version(mapping);
	Keys(Member(mapping, "ontology_ref"), {"name", "iri"}, "ontology reference");
	Keys(Member(mapping, "semantic_model_ref"), {"name", "iri"}, "core reference");
	if (Text(Member(mapping, "ontology_ref"), "name") != Text(ontology, "name") ||
	    Text(Member(mapping, "semantic_model_ref"), "name") != bundle.model.name) {
		Fail("mapping", "bind", "document reference name mismatch");
	}
	auto mappings = Array(mapping, "concept_mappings", true);
	if (mappings.size() != 1) {
		Fail("mapping", "capability", "only root concept may have a mapping");
	}
	auto root_mapping = mappings[0];
	Keys(root_mapping, {"concept", "object_mappings", "link_mappings"}, "concept mapping");
	if (Text(root_mapping, "concept") != bundle.root) {
		Fail("mapping", "capability", "derived concept cannot have independent population mapping");
	}
	auto objects = Array(root_mapping, "object_mappings", true);
	if (objects.size() != 1) {
		Fail(bundle.root, "mapping", "single object mapping required");
	}
	bundle.identity = ObjectIdentity(bundle, objects[0], id_relation);
	auto links = Array(root_mapping, "link_mappings", true);
	if (links.size() != 1) {
		Fail(bundle.root, "mapping", "single link mapping tree required");
	}
	Keys(links[0], {"object_mapping", "children"}, "root link mapping");
	if (ObjectIdentity(bundle, Member(links[0], "object_mapping"), id_relation) != bundle.identity) {
		Fail(bundle.root, "mapping", "link identity override");
	}
	case_insensitive_set_t bound;
	for (auto link : Array(links[0], "children", true)) {
		Keys(link, {"relationship", "object_mapping"}, "scalar link");
		auto name = Text(link, "relationship");
		if (!bound.insert(name).second) {
			Fail(name, "mapping", "duplicate attribute binding");
		}
		Attribute *attribute = nullptr;
		for (auto &candidate : bundle.attributes) {
			if (candidate.name == name) {
				attribute = &candidate;
			}
		}
		if (!attribute) {
			Fail(name, "mapping", "undeclared relationship");
		}
		auto object = Member(link, "object_mapping");
		Keys(object, {"concept", "expression"}, "attribute object mapping");
		if (Text(object, "concept") != attribute->target) {
			Fail(name, "mapping", "target concept mismatch");
		}
		attribute->field = LogicalField(bundle, Text(object, "expression"));
		if (bundle.model.datasets[0].FindField(attribute->field)->datatype != attribute->target) {
			Fail(name, "mapping", "logical datatype does not match target concept");
		}
		if (name == id_relation && attribute->field != bundle.identity) {
			Fail(name, "mapping", "identity attribute override");
		}
	}
	if (bound.size() != bundle.attributes.size() || !bound.count(id_relation)) {
		Fail(bundle.root, "mapping", "every declared scalar relationship including identity must be bound");
	}
	auto id = FieldTree(bundle, bundle.identity);
	if (id->GetExpressionClass() != ExpressionClass::COLUMN_REF ||
	    bundle.model.datasets[0].primary_key != vector<string> {id->Cast<ColumnRefExpression>().column_names.back()}) {
		Fail(bundle.root, "identity", "identity must be a plain declared single-column primary key");
	}
	auto derivations = Array(child, "derived_by", true);
	if (derivations.size() != 1) {
		Fail(bundle.derived, "rule", "exactly one scalar derivation required");
	}
	bundle.original_rule = ArrayText(derivations[0]);
	bundle.rule = Parse(bundle.original_rule);
	Strict(*bundle.rule);
	Resolve(bundle, bundle.rule, true);
	Strict(*bundle.rule);
}

bool Portable(const LogicalType &type) {
	switch (type.id()) {
	case LogicalTypeId::VARCHAR:
	case LogicalTypeId::BOOLEAN:
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
	case LogicalTypeId::DECIMAL:
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE:
	case LogicalTypeId::DATE:
		return true;
	default:
		return false;
	}
}

bool Matches(const string &datatype, const LogicalType &type) {
	if (datatype.empty()) {
		return Portable(type) || type.id() == LogicalTypeId::HUGEINT;
	}
	auto id = type.id();
	if (datatype == "String") {
		return id == LogicalTypeId::VARCHAR;
	}
	if (datatype == "Boolean") {
		return id == LogicalTypeId::BOOLEAN;
	}
	if (datatype == "Date") {
		return id == LogicalTypeId::DATE;
	}
	if (datatype == "Decimal") {
		return id == LogicalTypeId::DECIMAL;
	}
	if (datatype == "Float") {
		return id == LogicalTypeId::FLOAT || id == LogicalTypeId::DOUBLE;
	}
	if (datatype == "Integer") {
		return id == LogicalTypeId::TINYINT || id == LogicalTypeId::SMALLINT || id == LogicalTypeId::INTEGER ||
		       id == LogicalTypeId::BIGINT || id == LogicalTypeId::HUGEINT;
	}
	return false;
}

void ExpandMetric(const Bundle &bundle, unique_ptr<ParsedExpression> &expression) {
	if (expression->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto &names = expression->Cast<ColumnRefExpression>().column_names;
		if (names.size() != 2 || !StringUtil::CIEquals(names[0], bundle.model.datasets[0].name)) {
			Fail(expression->ToString(), "metric", "single root dataset required");
		}
		expression = FieldTree(bundle, names[1]);
		return;
	}
	ParsedExpressionIterator::EnumerateChildren(
	    *expression, [&](unique_ptr<ParsedExpression> &child) { ExpandMetric(bundle, child); });
}

struct SnapshotDependency : DependencyItem {
	explicit SnapshotDependency(shared_ptr<Bundle> bundle) : bundle(std::move(bundle)) {
	}
	shared_ptr<Bundle> bundle;
};
struct FrozenBind : TableFunctionData {
	shared_ptr<Bundle> bundle, program_parent;
};
struct FrozenScan : GlobalTableFunctionState {
	ColumnDataScanState state;
};
unique_ptr<FunctionData> FrozenBindFunction(ClientContext &, TableFunctionBindInput &input, vector<LogicalType> &types,
                                            vector<string> &names) {
	auto dependency =
	    input.ref.external_dependency ? input.ref.external_dependency->GetDependency("ossie_batch") : nullptr;
	if (!dependency) {
		Fail("_ossie_snapshot_rows", "bind",
		     "internal frozen AST dependency required; rendered SQL is diagnostic only");
	}
	auto result = make_uniq<FrozenBind>();
	result->bundle = dependency->Cast<SnapshotDependency>().bundle;
	auto parent = input.ref.external_dependency->GetDependency("ossie_program_parent");
	if (parent) {
		result->program_parent = parent->Cast<SnapshotDependency>().bundle;
	}
	types = result->bundle->collection->Types();
	names = result->bundle->physical_names;
	return std::move(result);
}
unique_ptr<GlobalTableFunctionState> FrozenInit(ClientContext &, TableFunctionInitInput &input) {
	auto result = make_uniq<FrozenScan>();
	input.bind_data->Cast<FrozenBind>().bundle->collection->InitializeScan(result->state);
	return std::move(result);
}
void FrozenScanFunction(ClientContext &, TableFunctionInput &input, DataChunk &output) {
	input.bind_data->Cast<FrozenBind>().bundle->collection->Scan(input.global_state->Cast<FrozenScan>().state, output);
}
unique_ptr<TableRef> SnapshotTable(Bundle &bundle) {
	auto table = make_uniq<TableFunctionRef>();
	table->function = make_uniq<FunctionExpression>("_ossie_snapshot_rows", vector<unique_ptr<ParsedExpression>>());
	table->function->Cast<FunctionExpression>().catalog = "system";
	table->function->Cast<FunctionExpression>().schema = "main";
	table->alias = bundle.model.datasets[0].name;
	table->external_dependency = make_shared_ptr<ExternalDependency>();
	table->external_dependency->AddDependency("ossie_batch", make_shared_ptr<SnapshotDependency>(bundle.owner.lock()));
	return std::move(table);
}
void EngineFunctions(ParsedExpression &expression) {
	if (expression.GetExpressionClass() == ExpressionClass::FUNCTION) {
		auto &function = expression.Cast<FunctionExpression>();
		function.catalog = "system";
		function.schema = "main";
	}
	ParsedExpressionIterator::EnumerateChildren(expression, [&](ParsedExpression &child) { EngineFunctions(child); });
}
unique_ptr<SQLStatement> FrozenSQL(Bundle &bundle, const string &sql) {
	Parser parser;
	parser.ParseQuery(sql);
	auto statement = std::move(parser.statements[0]);
	auto &select = statement->Cast<SelectStatement>().node->Cast<SelectNode>();
	select.from_table = SnapshotTable(bundle);
	for (auto &expression : select.select_list) {
		EngineFunctions(*expression);
	}
	return statement;
}

void CheckBooleanType(Bundle &bundle, const ParsedExpression &expression) {
	auto sql =
	    "SELECT " + expression.ToString() + " FROM main.ossie_snapshot AS " + Quote(bundle.model.datasets[0].name);
	auto engine = Engine(bundle);
	auto prepared = engine->Prepare(FrozenSQL(bundle, sql));
	if (prepared->HasError()) {
		NormalizeError(prepared->GetErrorObject(), expression.ToString(), "bind", ExceptionType::INVALID_INPUT);
	}
	if (prepared->GetTypes()[0] != LogicalType::BOOLEAN) {
		Fail(expression.ToString(), "rule", "population predicate must have BOOLEAN type without implicit coercion");
	}
}

void BooleanContexts(Bundle &bundle, const ParsedExpression &expression) {
	if (expression.GetExpressionClass() == ExpressionClass::CONJUNCTION ||
	    expression.GetExpressionType() == ExpressionType::OPERATOR_NOT) {
		ParsedExpressionIterator::EnumerateChildren(
		    expression, [&](const ParsedExpression &child) { CheckBooleanType(bundle, child); });
	}
	if (expression.GetExpressionClass() == ExpressionClass::CASE) {
		for (auto &check : expression.Cast<CaseExpression>().case_checks) {
			CheckBooleanType(bundle, *check.when_expr);
		}
	}
	ParsedExpressionIterator::EnumerateChildren(expression,
	                                            [&](const ParsedExpression &child) { BooleanContexts(bundle, child); });
}

void CheckBoolean(Bundle &bundle, const ParsedExpression &expression) {
	BooleanContexts(bundle, expression);
	CheckBooleanType(bundle, expression);
}

struct CapturedTable {
	vector<string> names;
	vector<LogicalType> types;
	vector<vector<Value>> rows;
	string canonical;
};

void Frame(string &output, const string &value) {
	output += to_string(value.size()) + ":" + value;
}

string JSONString(const string &value) {
	string result = "\"";
	const char *hex = "0123456789abcdef";
	for (char original : value) {
		const auto c = static_cast<unsigned char>(original);
		if (c == '"' || c == '\\') {
			result += '\\';
			result += original;
		} else if (c < 32) {
			result += "\\u00";
			result += hex[c >> 4];
			result += hex[c & 15];
		} else {
			result += original;
		}
	}
	return result + "\"";
}

idx_t PhysicalColumn(const CapturedTable &table, const string &name) {
	for (idx_t i = 0; i < table.names.size(); i++) {
		if (StringUtil::CIEquals(table.names[i], name)) {
			return i;
		}
	}
	Fail(name, "snapshot", "undeclared physical column in captured source");
}

CapturedTable CaptureTable(ClientContext &context, Connection &source, const Dataset &dataset,
                           optional_ptr<Catalog> &catalog, optional_ptr<CatalogEntry> &previous,
                           idx_t byte_budget = MAX_BYTES) {
	auto parts = QualifiedName::ParseComponents(dataset.source_bound);
	if (parts.size() != 3) {
		Fail(dataset.name, "snapshot", "fully qualified catalog.schema.table source required");
	}
	auto entry = Catalog::GetEntry(*source.context, parts[0], parts[1],
	                               EntryLookupInfo(CatalogType::TABLE_ENTRY, parts[2]), OnEntryNotFound::RETURN_NULL);
	if (!entry || entry->type != CatalogType::TABLE_ENTRY || entry->temporary ||
	    !entry->Cast<TableCatalogEntry>().IsDuckTable()) {
		Fail(dataset.name, "snapshot", "only committed native tables supported; TEMP/view/external source refused");
	}
	if (catalog && &catalog->GetAttached() != &entry->ParentCatalog().GetAttached()) {
		Fail(dataset.name, "snapshot", "both sources must belong to the same underlying database");
	}
	if (previous == entry) {
		Fail(dataset.name, "snapshot", "two distinct native source tables required");
	}
	previous = entry;
	catalog = entry->ParentCatalog();
	for (auto &column : entry->Cast<TableCatalogEntry>().GetColumns().Logical()) {
		if (column.Generated()) {
			Fail(dataset.name, "snapshot", "generated source columns not supported");
		}
	}
	auto result = Run(context, source,
	                  "SELECT * FROM " + Quote(parts[0]) + "." + Quote(parts[1]) + "." + Quote(parts[2]) + " LIMIT " +
	                      to_string(MAX_ROWS + 1));
	CapturedTable capture;
	capture.names = result->names;
	capture.types = result->types;
	auto count = result->Cast<MaterializedQueryResult>().RowCount();
	if (count > MAX_ROWS || capture.types.empty() || capture.types.size() > 256) {
		Fail(dataset.name, "snapshot", "batch exceeds 10000 rows or 256 physical columns; no truncation performed");
	}
	capture.canonical = "ossie-join-input-v1:";
	auto capture_frame = [&](const string &value) {
		auto prefix = to_string(value.size()) + ":";
		if (capture.canonical.size() > byte_budget || prefix.size() > byte_budget - capture.canonical.size() ||
		    value.size() > byte_budget - capture.canonical.size() - prefix.size()) {
			Fail(dataset.name, "snapshot",
			     byte_budget == MAX_BYTES ? "batch exceeds 16 MiB canonical value limit including metadata"
			                              : "combined captures exceed16MiB canonical limit including metadata");
		}
		capture.canonical += prefix;
		capture.canonical += value;
	};
	capture_frame(dataset.name);
	capture_frame(dataset.source_bound);
	capture_frame(to_string(count));
	for (idx_t c = 0; c < capture.types.size(); c++) {
		if (!Portable(capture.types[c]) ||
		    (capture.types[c].id() == LogicalTypeId::VARCHAR && !StringType::GetCollation(capture.types[c]).empty())) {
			Fail(capture.names[c], "snapshot", "unsupported physical column type or collation");
		}
		capture_frame(capture.names[c]);
		capture_frame(capture.types[c].ToString());
	}
	while (auto chunk = result->Fetch()) {
		for (idx_t r = 0; r < chunk->size(); r++) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			vector<Value> row;
			for (idx_t c = 0; c < chunk->ColumnCount(); c++) {
				auto value = chunk->GetValue(c, r);
				capture_frame(value.ToSQLString());
				if (capture.canonical.size() > byte_budget) {
					Fail(dataset.name, "snapshot", "batch exceeds 16 MiB canonical value limit");
				}
				row.push_back(std::move(value));
			}
			capture.rows.push_back(std::move(row));
		}
	}
	return capture;
}

void ExpandOriginal(const Model &model, unique_ptr<ParsedExpression> &expression) {
	if (expression->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto names = expression->Cast<ColumnRefExpression>().column_names;
		auto dataset = names.size() == 2 ? model.FindDataset(names[0]) : nullptr;
		auto field = dataset ? dataset->FindField(names[1]) : nullptr;
		if (!field) {
			Fail(expression->ToString(), "metric", "qualified original logical field required");
		}
		expression = field->expression.tree->Copy();
		Strict(*expression);
		Qualify(expression, dataset->name);
		return;
	}
	ParsedExpressionIterator::EnumerateChildren(
	    *expression, [&](unique_ptr<ParsedExpression> &child) { ExpandOriginal(model, child); });
}

void FlattenPhysical(unique_ptr<ParsedExpression> &expression, const Dataset &dataset, const CapturedTable &capture,
                     idx_t offset, const string &root) {
	if (expression->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto names = expression->Cast<ColumnRefExpression>().column_names;
		if (names.empty() || names.size() > 2 || (names.size() == 2 && !StringUtil::CIEquals(names[0], dataset.name))) {
			Fail(expression->ToString(), "bind", "physical reference crosses dataset");
		}
		auto index = PhysicalColumn(capture, names.back()) + offset;
		expression = make_uniq<ColumnRefExpression>("_ossie_column_" + to_string(index), root);
		return;
	}
	ParsedExpressionIterator::EnumerateChildren(*expression, [&](unique_ptr<ParsedExpression> &child) {
		FlattenPhysical(child, dataset, capture, offset, root);
	});
}

void FlattenLogical(const Bundle &bundle, unique_ptr<ParsedExpression> &expression) {
	if (expression->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto field = LogicalName(bundle, expression->Cast<ColumnRefExpression>().column_names);
		expression = make_uniq<ColumnRefExpression>(field, bundle.model.datasets[0].name);
		return;
	}
	ParsedExpressionIterator::EnumerateChildren(
	    *expression, [&](unique_ptr<ParsedExpression> &child) { FlattenLogical(bundle, child); });
}

void ObserveLoad(ClientContext &context, PopulationLoadCheckpoint checkpoint) {
	auto observer = context.registered_state->Get<PopulationLoadObserver>(PopulationLoadObserver::KEY);
	if (observer) {
		observer->Observe(checkpoint);
	}
}

void ObserveDaily(ClientContext &context, PopulationDailyCheckpoint checkpoint) {
	auto observer = context.registered_state->Get<PopulationDailyObserver>(PopulationDailyObserver::KEY);
	if (observer) {
		observer->Observe(checkpoint);
	}
}

void ObserveDailyState(ClientContext &context, PopulationDailyStateCheckpoint checkpoint) {
	auto observer = context.registered_state->Get<PopulationDailyStateObserver>(PopulationDailyStateObserver::KEY);
	if (observer) {
		observer->Observe(checkpoint);
	}
	if (context.IsInterrupted()) {
		throw InterruptException();
	}
}

void ObserveRelation(ClientContext &context, PopulationRelationCheckpoint checkpoint) {
	auto observer = context.registered_state->Get<PopulationRelationObserver>(PopulationRelationObserver::KEY);
	if (observer) {
		observer->Observe(checkpoint);
	}
	if (context.IsInterrupted()) {
		throw InterruptException();
	}
}

// Capture and validation use original datasets. Only the published execution model is flattened.
void FreezeJoin(ClientContext &context, Bundle &bundle, yyjson_val *policy, Connection &engine) {
	Keys(policy, {"fact", "dimension", "relationship", "type", "missing"}, "join policy");
	if (Text(policy, "type") != "INNER" || Text(policy, "missing") != "reject") {
		Fail("join", "capability", "explicit INNER/reject complete-match policy required");
	}
	const auto &original = bundle.definition_model;
	if (original.datasets.size() != 2 || original.relationships.size() != 1) {
		Fail("join", "capability", "exactly two datasets and one relationship required");
	}
	auto fact = original.FindDataset(Text(policy, "fact"));
	auto dimension = original.FindDataset(Text(policy, "dimension"));
	auto &relation = original.relationships[0];
	if (!fact || !dimension || fact == dimension || relation.name != Text(policy, "relationship") ||
	    !StringUtil::CIEquals(relation.from_dataset, fact->name) ||
	    !StringUtil::CIEquals(relation.to_dataset, dimension->name) || relation.from_columns.size() != 1 ||
	    relation.to_columns.size() != 1 ||
	    (relation.cardinality != Cardinality::MANY_TO_ONE && relation.cardinality != Cardinality::ONE_TO_ONE)) {
		Fail("join", "capability", "one forward single-column fact-to-unique-dimension relationship required");
	}
	if (fact->primary_key.size() != 1) {
		Fail(fact->name, "identity", "plain single-column fact primary key required");
	}
	idx_t fields = 0;
	for (auto &dataset : original.datasets) {
		fields += dataset.fields.size();
		if (dataset.name.empty() || dataset.fields.empty()) {
			Fail(dataset.name, "capability", "nonempty datasets and declared fields required");
		}
		for (auto &field : dataset.fields) {
			if (field.name.empty()) {
				Fail(dataset.name, "bind", "empty field name");
			}
			Strict(*field.expression.tree);
			auto expression = field.expression.tree->Copy();
			Qualify(expression, dataset.name);
		}
	}
	if (fields > 256 || original.metrics.empty() || original.metrics.size() > 64) {
		Fail("join", "capability", "require at most 256 fields and 1..64 metrics");
	}
	for (auto &metric : original.metrics) {
		if (metric.name.empty() || StringUtil::StartsWith(StringUtil::Lower(metric.name), "_ossie_")) {
			Fail(metric.name, "bind", "empty or reserved metric name");
		}
		Strict(*metric.expression.tree, true);
		auto expanded = metric.expression.tree->Copy();
		ExpandOriginal(original, expanded);
		Strict(*expanded, true);
		RequireMetricGrain(original, {metric.name}, fact->name);
		CompileToSQL(original, {metric.name}, {}, {}, CompileOptions());
	}
	Connection source(DatabaseInstance::GetDatabase(context));
	Run(context, source, "BEGIN TRANSACTION");
	optional_ptr<Catalog> catalog;
	optional_ptr<CatalogEntry> previous;
	auto facts = CaptureTable(context, source, *fact, catalog, previous);
	ObserveLoad(context, PopulationLoadCheckpoint::AFTER_FACT_CAPTURE);
	auto dims = CaptureTable(context, source, *dimension, catalog, previous);
	Run(context, source, "COMMIT");
	if (facts.types.size() + dims.types.size() > 256) {
		Fail("join", "snapshot", "joined batch exceeds 256 physical columns");
	}
	auto fk = PhysicalColumn(facts, relation.from_columns[0]);
	auto dk = PhysicalColumn(dims, relation.to_columns[0]);
	auto id = PhysicalColumn(facts, fact->primary_key[0]);
	if (facts.types[fk] != LogicalType::INTEGER || dims.types[dk] != LogicalType::INTEGER) {
		Fail("join", "identity", "both join keys must be native INTEGER; coercion refused");
	}
	if (facts.types[id] != LogicalType::VARCHAR) {
		Fail("join", "identity", "fact identity must be native uncollated String");
	}
	unordered_map<int32_t, idx_t> lookup;
	for (idx_t r = 0; r < dims.rows.size(); r++) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		auto &key = dims.rows[r][dk];
		if (key.IsNull() || !lookup.emplace(key.GetValue<int32_t>(), r).second) {
			Fail(dimension->name, "identity", "entire dimension batch requires nonnull unique key");
		}
	}
	unordered_set<string> identities;
	vector<idx_t> matches;
	for (auto &row : facts.rows) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		if (row[id].IsNull() || !identities.insert(row[id].GetValue<string>()).second) {
			Fail(fact->name, "identity", "entire fact batch requires nonnull unique identity");
		}
		if (row[fk].IsNull()) {
			Fail(fact->name, "identity", "nonnull fact foreign key required before filtering");
		}
		auto found = lookup.find(row[fk].GetValue<int32_t>());
		if (found == lookup.end()) {
			Fail(fact->name, "identity", "every fact foreign key must match a dimension before filtering");
		}
		matches.push_back(found->second);
	}
	vector<LogicalType> types = facts.types;
	types.insert(types.end(), dims.types.begin(), dims.types.end());
	bundle.collection = make_uniq<ColumnDataCollection>(context, types);
	DataChunk chunk;
	chunk.Initialize(Allocator::Get(context), types);
	string joined = "ossie-join-result-v1:";
	for (idx_t r = 0; r < facts.rows.size(); r++) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		idx_t slot = r % STANDARD_VECTOR_SIZE;
		for (idx_t c = 0; c < types.size(); c++) {
			auto &value = c < facts.types.size() ? facts.rows[r][c] : dims.rows[matches[r]][c - facts.types.size()];
			chunk.SetValue(c, slot, value);
			Frame(joined, value.ToSQLString());
			if (joined.size() > MAX_BYTES) {
				Fail("join", "snapshot", "joined batch exceeds 16 MiB canonical value limit");
			}
		}
		chunk.SetCardinality(slot + 1);
		if (slot + 1 == STANDARD_VECTOR_SIZE || r + 1 == facts.rows.size()) {
			bundle.collection->Append(chunk);
			chunk.Reset();
		}
	}
	// Construction appends one row for every unique fact identity, with no filtering or fan-out.
	if (bundle.collection->Count() != facts.rows.size() || identities.size() != facts.rows.size()) {
		Fail("join", "identity", "joined fact identity multiset differs from captured facts");
	}
	unordered_set<string> joined_identities;
	ColumnDataScanState scan;
	bundle.collection->InitializeScan(scan);
	DataChunk verification;
	bundle.collection->InitializeScanChunk(verification);
	while (bundle.collection->Scan(scan, verification)) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		for (idx_t r = 0; r < verification.size(); r++) {
			auto value = verification.GetValue(id, r);
			if (value.IsNull() || !joined_identities.insert(value.GetValue<string>()).second) {
				Fail("join", "identity", "joined identity multiplicity is not one");
			}
		}
	}
	if (identities != joined_identities) {
		Fail("join", "identity", "joined identity set differs from captured facts");
	}
	bundle.rows = facts.rows.size();
	Dataset flattened = *fact;
	flattened.fields.clear();
	flattened.field_index.clear();
	flattened.unique_keys.clear();
	flattened.primary_key = {"_ossie_column_" + to_string(id)};
	flattened.source_bound = "memory.main.ossie_snapshot";
	for (auto dataset : {fact, dimension}) {
		auto &capture = dataset == fact ? facts : dims;
		auto offset = dataset == fact ? 0 : facts.types.size();
		for (auto field : dataset->fields) {
			auto original_name = field.name;
			field.name = "_ossie_field_" + to_string(flattened.fields.size());
			FlattenPhysical(field.expression.tree, *dataset, capture, offset, fact->name);
			field.expression.sql = field.expression.tree->ToString();
			field.is_computed = true;
			bundle.provenance.push_back({{dataset->name, original_name}, field.name});
			flattened.AddField(std::move(field));
		}
	}
	bundle.model.datasets = {std::move(flattened)};
	bundle.model.dataset_index.clear();
	bundle.model.dataset_index[fact->name] = 0;
	bundle.model.relationships.clear();
	for (auto &metric : bundle.model.metrics) {
		FlattenLogical(bundle, metric.expression.tree);
		metric.expression.sql = metric.expression.tree->ToString();
	}
	for (idx_t c = 0; c < types.size(); c++) {
		bundle.physical_names.push_back("_ossie_column_" + to_string(c));
	}
	bundle.original_source = fact->source_bound + " INNER " + dimension->source_bound;
	string evidence = "ossie-frozen-join-v1:";
	for (const auto &value :
	     {bundle.profile, bundle.manifest_hash, bundle.core_hash, bundle.ontology_hash, bundle.mapping_hash,
	      Hash(engine, facts.canonical), Hash(engine, dims.canonical), Hash(engine, joined)}) {
		Frame(evidence, value);
	}
	for (auto &entry : bundle.provenance) {
		Frame(evidence, entry.first[0]);
		Frame(evidence, entry.first[1]);
		Frame(evidence, entry.second);
	}
	bundle.content_hash = Hash(engine, evidence);
	bundle.snapshot_id = bundle.content_hash + ":" + to_string(NextBatchSequence());
	bundle.execution_target = "{\"target\":\"frozen fact-dimension batch (AST dependency required)\","
	                          "\"join_policy\":\"INNER/reject\",\"mapping_version\":1,\"row_order_sensitive\":true,"
	                          "\"metric_grain\":" +
	                          JSONString(fact->name) + ",\"relationship\":" + JSONString(relation.name) +
	                          ",\"fact_foreign_key\":" + JSONString(relation.from_columns[0]) +
	                          ",\"dimension_key\":" + JSONString(relation.to_columns[0]) +
	                          ",\"fact_identity_column\":" + JSONString(fact->primary_key[0]) + ",\"inputs\":[";
	for (auto dataset : {fact, dimension}) {
		auto &capture = dataset == fact ? facts : dims;
		if (dataset == dimension) {
			bundle.execution_target += ",";
		}
		bundle.execution_target += "{\"dataset\":" + JSONString(dataset->name) +
		                           ",\"source\":" + JSONString(dataset->source_bound) +
		                           ",\"rows\":" + to_string(capture.rows.size()) +
		                           ",\"sha256\":" + JSONString(Hash(engine, capture.canonical)) + "}";
	}
	bundle.execution_target += "],\"logical_fields\":[";
	for (idx_t i = 0; i < bundle.provenance.size(); i++) {
		auto &entry = bundle.provenance[i];
		if (i) {
			bundle.execution_target += ",";
		}
		bundle.execution_target += "{\"dataset\":" + JSONString(entry.first[0]) +
		                           ",\"field\":" + JSONString(entry.first[1]) +
		                           ",\"frozen_field\":" + JSONString(entry.second) + "}";
	}
	bundle.execution_target += "],\"physical_columns\":[";
	idx_t index = 0;
	for (auto dataset : {fact, dimension}) {
		auto &capture = dataset == fact ? facts : dims;
		for (auto &name : capture.names) {
			if (index) {
				bundle.execution_target += ",";
			}
			bundle.execution_target += "{\"dataset\":" + JSONString(dataset->name) + ",\"column\":" + JSONString(name) +
			                           ",\"frozen_column\":" + JSONString(bundle.physical_names[index++]) + "}";
		}
	}
	bundle.execution_target += "]}";
	bundle.capture_join =
	    "{\"relationship\":" + JSONString(relation.name) + ",\"from_dataset\":" + JSONString(fact->name) +
	    ",\"to_dataset\":" + JSONString(dimension->name) + ",\"from_column\":" + JSONString(relation.from_columns[0]) +
	    ",\"to_column\":" + JSONString(relation.to_columns[0]) +
	    ",\"type\":\"INNER\",\"missing\":\"reject\","
	    "\"validation_scope\":\"entire captured inputs before request filters\","
	    "\"execution_stage\":\"load-time frozen lookup; no live query join\"}";
	for (auto dataset : {fact, dimension}) {
		auto &capture = dataset == fact ? facts : dims;
		bundle.inputs.push_back(
		    {dataset->name, dataset->source_bound, Hash(engine, capture.canonical), capture.rows.size()});
	}
}

// Opt-in observation profile: typed tuple identity, with a referent-only User.
// Keep scalar and joined profile validation unchanged.
void ObservationDefinitions(Bundle &b, yyjson_val *ontology, yyjson_val *mapping) {
	bool program = b.profile == RELATION_PROFILE;
	Keys(ontology, {"version", "name", "description", "ai_context", "ontology", "requires"}, "ontology");
	Version(ontology);
	Empty(ontology, "requires", "ontology");
	yyjson_val *user = nullptr, *observation = nullptr, *derived = nullptr;
	auto concepts = Array(ontology, "ontology", true);
	case_insensitive_set_t names;
	for (auto c : concepts) {
		Keys(c,
		     {"concept", "type", "description", "ai_context", "identify_by", "extends", "derived_by", "requires",
		      "relationships"},
		     "concept");
		if (!program || Array(c, "identify_by").size() != 3) {
			Empty(c, "requires", "concept");
		}
		if (Text(c, "type") != "EntityType" || !names.insert(Text(c, "concept")).second) {
			Fail("ontology", "capability", "three distinct entity concepts required");
		}
		if (!Array(c, "extends").empty()) {
			if (derived) {
				Fail("ontology", "capability", "single derived observation required");
			}
			derived = c;
		} else if (Array(c, "identify_by").size() == 1) {
			if (user) {
				Fail("ontology", "capability", "single referenced User required");
			}
			user = c;
		} else if (Array(c, "identify_by").size() == 3) {
			if (observation) {
				Fail("ontology", "capability", "single observation root required");
			}
			observation = c;
		} else {
			Fail("ontology", "capability", "User and three-component observation identities required");
		}
	}
	if (concepts.size() != 3 || !user || !observation || !derived) {
		Fail("ontology", "capability", "profile requires User, observation and derived observation");
	}
	b.user_concept = Text(user, "concept");
	b.root = Text(observation, "concept");
	b.derived = Text(derived, "concept");
	if (program) {
		for (auto constraint : Array(observation, "requires")) {
			b.relation_input.requires.push_back(ArrayText(constraint));
		}
	}
	b.user_id_relation = ArrayText(Array(user, "identify_by", true)[0]);
	auto inheritance = Array(derived, "extends", true);
	if (inheritance.size() != 1 || ArrayText(inheritance[0]) != b.root) {
		Fail(b.derived, "capability", "single inheritance from observation required");
	}
	Empty(derived, "identify_by", b.derived);
	Empty(derived, "relationships", b.derived);
	Empty(user, "derived_by", b.user_concept);
	Empty(observation, "derived_by", b.root);
	auto relationship_target = [&](yyjson_val *r, const string &multiplicity) {
		Keys(r, {"name", "roles", "multiplicity", "description", "ai_context", "verbalizes", "requires", "derived_by"},
		     "relationship");
		Empty(r, "requires", "relationship");
		Empty(r, "derived_by", "relationship");
		auto roles = Array(r, "roles", true);
		if (roles.size() != 1 || Text(r, "multiplicity") != multiplicity) {
			Fail(Text(r, "name"), "capability", "unsupported relationship role or multiplicity");
		}
		Keys(roles[0], {"concept"}, "role");
		return Text(roles[0], "concept");
	};
	auto ur = Array(user, "relationships", true);
	if (ur.size() != 1 || Text(ur[0], "name") != b.user_id_relation ||
	    relationship_target(ur[0], "OneToOne") != "String") {
		Fail(b.user_concept, "capability", "referenced User has only a OneToOne String identity");
	}
	vector<string> identity_relations;
	for (auto id : Array(observation, "identify_by", true)) {
		identity_relations.push_back(ArrayText(id));
	}
	b.observation_relationships = identity_relations;
	case_insensitive_set_t relation_names, targets;
	for (auto r : Array(observation, "relationships", true)) {
		if (program && !relation_names.insert(Text(r, "name")).second) {
			Fail(Text(r, "name"), "capability", "duplicate mapped or derived relationship name");
		}
		if (program && !Array(r, "derived_by").empty()) {
			Keys(r,
			     {"name", "roles", "multiplicity", "description", "ai_context", "verbalizes", "requires", "derived_by"},
			     "derived relationship");
			auto roles = Array(r, "roles", true);
			if (roles.size() != 1) {
				Fail(Text(r, "name"), "capability", "single User role required");
			}
			Keys(roles[0], {"concept"}, "role");
			RelationDeclaration declaration;
			declaration.name = Text(r, "name");
			declaration.role_concept = Text(roles[0], "concept");
			declaration.multiplicity = Text(r, "multiplicity");
			for (auto rule : Array(r, "derived_by", true)) {
				declaration.derived_by.push_back(ArrayText(rule));
			}
			for (auto constraint : Array(r, "requires")) {
				declaration.requires.push_back(ArrayText(constraint));
			}
			b.relation_input.declarations.push_back(std::move(declaration));
			continue;
		}
		auto name = Text(r, "name"), target = relationship_target(r, "ManyToOne");
		if ((!program && !relation_names.insert(name).second) || !targets.insert(target).second ||
		    (target != b.user_concept && target != "Date" && target != "String" && target != "Decimal" &&
		     target != "Integer")) {
			Fail(name, "capability", "observation requires one User, Date, String, Decimal and Integer relationship");
		}
		b.attributes.push_back({name, target, "", false});
	}
	if (b.attributes.size() != 5) {
		Fail(b.root, "capability", "five observation relationships required");
	}
	vector<string> identity_targets;
	for (auto &name : identity_relations) {
		string target;
		for (auto &a : b.attributes) {
			if (a.name == name) {
				target = a.target;
			}
		}
		identity_targets.push_back(target);
	}
	if (identity_targets != vector<string> {b.user_concept, "Date", "String"}) {
		Fail(b.root, "identity", "observation identity order must be User, Date, product String");
	}
	Keys(mapping,
	     {"version", "name", "description", "ontology_ref", "semantic_model_ref", "concept_mappings",
	      "custom_extensions"},
	     "mapping");
	Version(mapping);
	Keys(Member(mapping, "ontology_ref"), {"name", "iri"}, "ontology reference");
	Keys(Member(mapping, "semantic_model_ref"), {"name", "iri"}, "core reference");
	if (Text(Member(mapping, "ontology_ref"), "name") != Text(ontology, "name") ||
	    Text(Member(mapping, "semantic_model_ref"), "name") != b.model.name) {
		Fail("mapping", "bind", "document reference name mismatch");
	}
	// A nested User referent has exactly its id. It is not another link-tree layer.
	auto user_referent = [&](yyjson_val *object) {
		auto refs = Array(object, "referent_mappings", true);
		if (refs.size() != 1) {
			Fail(b.user_concept, "mapping", "single User identity referent required");
		}
		Keys(refs[0], {"relationship", "expression"}, "User referent");
		if (Text(refs[0], "relationship") != b.user_id_relation) {
			Fail(b.user_concept, "mapping", "User identity relationship mismatch");
		}
		return LogicalField(b, Text(refs[0], "expression"));
	};
	auto observation_referent = [&](yyjson_val *object) {
		Keys(object, {"concept", "referent_mappings"}, "observation object");
		if (Member(object, "concept") && Text(object, "concept") != b.root) {
			Fail(b.root, "mapping", "observation concept override");
		}
		auto refs = Array(object, "referent_mappings", true);
		if (refs.size() != 3) {
			Fail(b.root, "mapping", "three observation referents required");
		}
		vector<string> fields;
		for (idx_t i = 0; i < refs.size(); i++) {
			if (Text(refs[i], "relationship") != identity_relations[i]) {
				Fail(b.root, "mapping", "observation referent order mismatch");
			}
			if (i == 0) {
				Keys(refs[i], {"relationship", "referent_mappings"}, "nested User referent");
				fields.push_back(user_referent(refs[i]));
			} else {
				Keys(refs[i], {"relationship", "expression"}, "scalar identity referent");
				fields.push_back(LogicalField(b, Text(refs[i], "expression")));
			}
		}
		return fields;
	};
	auto mappings = Array(mapping, "concept_mappings", true);
	if (mappings.size() != 2) {
		Fail("mapping", "capability", "only User and root observation mappings required");
	}
	case_insensitive_set_t mapped;
	string user_field;
	for (auto cm : mappings) {
		Keys(cm, {"concept", "object_mappings", "link_mappings"}, "concept mapping");
		auto concept = Text(cm, "concept");
		if (!mapped.insert(concept).second || (concept != b.root && concept != b.user_concept)) {
			Fail(concept, "mapping", "unexpected concept mapping");
		}
		auto objects = Array(cm, "object_mappings", true), links = Array(cm, "link_mappings", true);
		if (objects.size() != 1 || links.size() != 1) {
			Fail(concept, "mapping", "single object and link tree required");
		}
		Keys(links[0], {"object_mapping", "children"}, "root link tree");
		auto children = Array(links[0], "children", true);
		if (concept == b.user_concept) {
			for (auto object : {objects[0], Member(links[0], "object_mapping")}) {
				Keys(object, {"concept", "referent_mappings"}, "User object");
				if (Member(object, "concept") && Text(object, "concept") != b.user_concept) {
					Fail(concept, "mapping", "User concept override");
				}
				auto field = user_referent(object);
				if (!user_field.empty() && user_field != field) {
					Fail(concept, "mapping", "User identity override");
				}
				user_field = field;
			}
			if (children.size() != 1) {
				Fail(concept, "mapping", "User has only its identity link");
			}
			Keys(children[0], {"relationship", "object_mapping"}, "User identity link");
			auto object = Member(children[0], "object_mapping");
			Keys(object, {"concept", "expression"}, "User id value");
			if (Text(children[0], "relationship") != b.user_id_relation || Text(object, "concept") != "String" ||
			    LogicalField(b, Text(object, "expression")) != user_field) {
				Fail(concept, "mapping", "User id link mismatch");
			}
		} else {
			b.observation_identity = observation_referent(objects[0]);
			if (observation_referent(Member(links[0], "object_mapping")) != b.observation_identity) {
				Fail(concept, "mapping", "observation identity override");
			}
			case_insensitive_set_t bound;
			for (auto link : children) {
				Keys(link, {"relationship", "object_mapping"}, "observation link");
				auto name = Text(link, "relationship");
				Attribute *a = nullptr;
				for (auto &candidate : b.attributes) {
					if (candidate.name == name) {
						a = &candidate;
					}
				}
				if (!a || !bound.insert(name).second) {
					Fail(name, "mapping", "unknown or duplicate observation link");
				}
				auto object = Member(link, "object_mapping");
				if (a->target == b.user_concept) {
					Keys(object, {"concept", "referent_mappings"}, "User link object");
					a->field = user_referent(object);
				} else {
					Keys(object, {"concept", "expression"}, "scalar link object");
					a->field = LogicalField(b, Text(object, "expression"));
				}
				if (Text(object, "concept") != a->target || b.model.datasets[0].FindField(a->field)->datatype !=
				                                                (a->target == b.user_concept ? "String" : a->target)) {
					Fail(name, "mapping", "target concept or logical datatype mismatch");
				}
			}
			if (bound.size() != b.attributes.size()) {
				Fail(concept, "mapping", "every observation relationship must be bound");
			}
		}
	}
	if (user_field != b.observation_identity[0]) {
		Fail(b.user_concept, "mapping", "nested and root User identities must agree");
	}
	for (auto &a : b.attributes) {
		for (idx_t i = 0; i < identity_relations.size(); i++) {
			if (a.name == identity_relations[i] && a.field != b.observation_identity[i]) {
				Fail(a.name, "mapping", "identity link override");
			}
		}
		if (a.target == "Decimal") {
			b.amount_field = a.field;
		}
		if (a.target == "Integer") {
			b.class_field = a.field;
		}
	}
	b.identity = user_field;
	// Reject casts/computed identity; compare native typed columns, never concatenated strings.
	vector<string> physical;
	case_insensitive_set_t distinct_fields;
	for (auto &field : b.observation_identity) {
		auto tree = FieldTree(b, field);
		if (tree->GetExpressionClass() != ExpressionClass::COLUMN_REF || !distinct_fields.insert(field).second) {
			Fail(field, "identity", "identity components must be distinct plain columns");
		}
		physical.push_back(tree->Cast<ColumnRefExpression>().column_names.back());
	}
	if (b.model.datasets[0].primary_key != physical) {
		Fail(b.root, "identity", "declared primary key must match typed observation tuple");
	}
	case_insensitive_set_t bound_fields, physical_fields;
	for (auto &a : b.attributes) {
		auto tree = FieldTree(b, a.field);
		if (tree->GetExpressionClass() != ExpressionClass::COLUMN_REF || !bound_fields.insert(a.field).second ||
		    !physical_fields.insert(tree->Cast<ColumnRefExpression>().column_names.back()).second) {
			Fail(a.field, "capability", "observation roles require distinct untransformed physical columns");
		}
	}
	if (b.model.datasets[0].fields.size() != 5) {
		Fail(b.root, "capability", "observation profile declares only its five bound fields");
	}
	// Each allowed aggregate form must be present exactly once; names are not capability checks.
	case_insensitive_set_t metric_shapes;
	for (auto &m : b.model.metrics) {
		auto tree = m.expression.tree->Copy();
		ExpandMetric(b, tree);
		if (tree->GetExpressionClass() != ExpressionClass::FUNCTION) {
			Fail(m.name, "capability", "only five direct observation aggregate forms supported");
		}
		auto &f = tree->Cast<FunctionExpression>();
		auto name = StringUtil::Lower(f.function_name);
		if (f.children.size() != 1) {
			Fail(m.name, "capability", "single aggregate argument required");
		}
		string field;
		for (const auto &candidate : {b.identity, b.amount_field}) {
			if (f.children[0]->Equals(*FieldTree(b, candidate))) {
				field = candidate;
			}
		}
		bool valid = (name == "count" && field == b.identity) ||
		             (!f.distinct && field == b.amount_field && (name == "count" || name == "sum" || name == "avg"));
		auto shape = name + ":" + (f.distinct ? "distinct:" : "all:") + field;
		if (!valid || !metric_shapes.insert(shape).second) {
			Fail(m.name, "capability", "unsupported or repeated observation metric shape");
		}
	}
	if (metric_shapes.size() != 5) {
		Fail("metrics", "capability", "all five observation aggregate forms required");
	}
	auto rules = Array(derived, "derived_by", true);
	if (rules.size() != 1) {
		Fail(b.derived, "rule", "single observation qualification rule required");
	}
	b.original_rule = ArrayText(rules[0]);
	b.rule = Parse(b.original_rule);
	Strict(*b.rule);
	Resolve(b, b.rule, true);
	if (b.rule->GetExpressionType() != ExpressionType::COMPARE_GREATERTHANOREQUALTO) {
		Fail(b.derived, "capability", "qualification must be class integer >= integer constant");
	}
	auto &comparison = b.rule->Cast<ComparisonExpression>();
	if (!comparison.left->Equals(*FieldTree(b, b.class_field)) ||
	    comparison.right->GetExpressionClass() != ExpressionClass::CONSTANT ||
	    !comparison.right->Cast<ConstantExpression>().value.type().IsIntegral() ||
	    comparison.right->Cast<ConstantExpression>().value.IsNull()) {
		Fail(b.derived, "capability", "qualification must be class integer >= integer constant");
	}
}

void ObservationIdentityPreflight(ClientContext &context, Bundle &b, Connection &engine) {
	auto &dataset = b.model.datasets[0];
	auto u = FieldTree(b, b.observation_identity[0])->ToString();
	auto d = FieldTree(b, b.observation_identity[1])->ToString();
	auto p = FieldTree(b, b.observation_identity[2])->ToString();
	auto source = " FROM main.ossie_snapshot AS " + Quote(dataset.name);
	auto bad =
	    Run(context, engine,
	        FrozenSQL(b, "SELECT count(*)" + source + " WHERE " + u + " IS NULL OR " + u + " = '' OR " + d +
	                         " IS NULL OR NOT system.main.isfinite(" + d + ") OR " + p + " IS NULL OR " + p + " = ''"))
	        ->Fetch();
	if (bad->GetValue(0, 0).GetValue<int64_t>() != 0) {
		Fail(b.root, "identity", "entire batch requires nonempty raw String User/product and finite nonnull DATE");
	}
	auto duplicates = Run(context, engine,
	                      FrozenSQL(b, "SELECT " + u + ", " + d + ", " + p + source + " GROUP BY " + u + ", " + d +
	                                       ", " + p + " HAVING system.main.count(*) > 1"));
	if (duplicates->Cast<MaterializedQueryResult>().RowCount() != 0) {
		Fail(b.root, "identity",
		     "entire batch contains duplicate observation tuple; identical and conflicting records both refused");
	}
	auto users = Run(context, engine, FrozenSQL(b, "SELECT count(DISTINCT " + u + ")" + source))->Fetch();
	b.user_count = static_cast<idx_t>(users->GetValue(0, 0).GetValue<int64_t>());
}

// The new profile has an independent loader path; it never relaxes the legacy compiler.
void FactExpressionBudget(const ParsedExpression &expression, idx_t depth, idx_t &nodes) {
	if (depth > 32 || ++nodes > 1024) {
		Fail("fact program", "capability", "expression depth32/node1024 before model lowering/copy");
	}
	ParsedExpressionIterator::EnumerateChildren(
	    expression, [&](const ParsedExpression &child) { FactExpressionBudget(child, depth + 1, nodes); });
}
void FactCoreBudget(yyjson_val *core) {
	auto datasets = Array(core, "datasets", true), metrics = Array(core, "metrics", true);
	if (datasets.size() != 2 || metrics.empty() || metrics.size() > 48) {
		Fail("fact program", "capability", "two datasets and1..48 total declarations required");
	}
	idx_t bytes = 0;
	auto declarations = [&](yyjson_val *declaration, bool field) {
		for (auto dialect : Array(Member(declaration, "expression"), "dialects", true)) {
			auto sql = Text(dialect, "expression");
			if (sql.size() > 16384 || sql.size() > 131072 - bytes) {
				Fail("fact program", "capability", "bounded rule bytes required before core parsing");
			}
			bytes += sql.size();
			auto language = Text(dialect, "dialect");
			if (language == OSSIE_SQL_2026 || language == ANSI_SQL) {
				auto parsed = Parser::ParseExpressionList(sql);
				if (parsed.size() != 1 || !parsed[0]->GetAlias().empty()) {
					Fail("fact program", "capability", "one unaliased expression required");
				}
				idx_t nodes = 0;
				FactExpressionBudget(*parsed[0], 1, nodes);
				if (field && parsed[0]->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
					Fail(Text(declaration, "name"), "capability",
					     "direct physical field required before model lowering");
				}
			}
		}
	};
	for (auto dataset : datasets) {
		auto fields = Array(dataset, "fields", true);
		if (fields.empty() || fields.size() > 256) {
			Fail("fact program", "capability", "1..256 fields per fact required");
		}
		for (auto field : fields) {
			declarations(field, true);
		}
	}
	for (auto metric : metrics) {
		declarations(metric, false);
	}
}

string FactLogicalField(const Dataset &dataset, const string &sql) {
	if (sql.find('"') != string::npos) {
		Fail(sql, "mapping", "unquoted dataset.field required");
	}
	auto expr = Parse(sql);
	if (expr->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		Fail(sql, "mapping", "direct declared field required");
	}
	auto &names = expr->Cast<ColumnRefExpression>().column_names;
	if (names.size() != 2 || !StringUtil::CIEquals(names[0], dataset.name) || !dataset.FindField(names[1])) {
		Fail(sql, "mapping", "declared field in this fact required");
	}
	return dataset.FindField(names[1])->name;
}
string FactPhysicalField(const Dataset &dataset, const Field &field) {
	if (field.expression.tree->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		Fail(field.name, "capability", "fact field must directly map a physical column");
	}
	auto &names = field.expression.tree->Cast<ColumnRefExpression>().column_names;
	if (names.empty() || names.size() > 2 || (names.size() == 2 && !StringUtil::CIEquals(names[0], dataset.name))) {
		Fail(field.name, "bind", "own physical column required");
	}
	return names.back();
}
void FactDefinitions(Bundle &b, yyjson_val *ontology, yyjson_val *mapping, yyjson_val *config) {
	Keys(config, {"facts", "windows"}, "fact program config");
	auto configs = Array(config, "facts", true);
	if (configs.size() != 2 || b.model.datasets.size() != 2 || !b.model.relationships.empty()) {
		Fail("fact program", "capability", "two independent facts and no core join required");
	}
	case_insensitive_set_t datasets, roots;
	for (auto config : configs) {
		Keys(config, {"dataset", "concept", "currency"}, "fact config");
		FactSource source;
		source.dataset = Text(config, "dataset");
		source.concept = Text(config, "concept");
		source.currency = Text(config, "currency");
		if (source.currency.size() != 3 ||
		    source.currency.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != string::npos) {
			Fail(source.concept, "capability", "explicit uppercase three-letter currency required");
		}
		if (!b.model.FindDataset(source.dataset) || !datasets.insert(source.dataset).second ||
		    !roots.insert(source.concept).second) {
			Fail("facts", "bind", "distinct declared datasets and root concepts required");
		}
		b.fact_input.sources.push_back(std::move(source));
	}
	if (b.fact_input.sources[0].currency != b.fact_input.sources[1].currency) {
		Fail("fact program", "capability", "same explicit currency required");
	}
	Keys(ontology, {"version", "name", "description", "ai_context", "ontology", "requires"}, "fact ontology");
	Version(ontology);
	Empty(ontology, "requires", "ontology");
	auto concepts = Array(ontology, "ontology", true);
	if (concepts.size() != 3) {
		Fail("ontology", "capability", "referent User and two root entities required");
	}
	yyjson_val *user = nullptr;
	case_insensitive_map_t<yyjson_val *> root_objects;
	case_insensitive_set_t allconcepts;
	for (auto concept : concepts) {
		Keys(concept,
		     {"concept", "type", "description", "ai_context", "identify_by", "extends", "derived_by", "requires",
		      "relationships"},
		     "fact concept");
		auto name = Text(concept, "concept");
		if (!allconcepts.insert(name).second || Text(concept, "type") != "EntityType") {
			Fail(name, "capability", "distinct EntityType concepts required");
		}
		Empty(concept, "extends", name);
		Empty(concept, "derived_by", name);
		Empty(concept, "requires", name);
		if (roots.count(name)) {
			root_objects[name] = concept;
		} else {
			if (user) {
				Fail(name, "capability", "one User referent required");
			}
			user = concept;
			b.user_concept = name;
		}
	}
	if (!user || root_objects.size() != 2) {
		Fail("ontology", "bind", "configured fact roots and one User referent required");
	}
	auto user_ids = Array(user, "identify_by", true), user_relations = Array(user, "relationships", true);
	if (user_ids.size() != 1 || user_relations.size() != 1) {
		Fail(b.user_concept, "identity", "single User identity required");
	}
	b.user_id_relation = ArrayText(user_ids[0]);
	auto target = [&](yyjson_val *relation, const string &multiplicity) {
		Keys(relation,
		     {"name", "roles", "multiplicity", "description", "ai_context", "verbalizes", "requires", "derived_by"},
		     "fact relationship");
		Empty(relation, "requires", "relationship");
		Empty(relation, "derived_by", "relationship");
		auto roles = Array(relation, "roles", true);
		if (roles.size() != 1 || Text(relation, "multiplicity") != multiplicity) {
			Fail(Text(relation, "name"), "capability", "single role and declared multiplicity required");
		}
		Keys(roles[0], {"concept"}, "fact role");
		return Text(roles[0], "concept");
	};
	if (Text(user_relations[0], "name") != b.user_id_relation || target(user_relations[0], "OneToOne") != "String") {
		Fail(b.user_concept, "identity", "User identity must be OneToOne String");
	}
	b.fact_input.user_concept = b.user_concept;
	b.fact_input.user_identity_relationship = b.user_id_relation;
	Keys(mapping,
	     {"version", "name", "description", "ontology_ref", "semantic_model_ref", "concept_mappings",
	      "custom_extensions"},
	     "fact mapping");
	Version(mapping);
	Keys(Member(mapping, "ontology_ref"), {"name", "iri"}, "ontology reference");
	Keys(Member(mapping, "semantic_model_ref"), {"name", "iri"}, "core reference");
	if (Text(Member(mapping, "ontology_ref"), "name") != Text(ontology, "name") ||
	    Text(Member(mapping, "semantic_model_ref"), "name") != b.model.name) {
		Fail("mapping", "bind", "definition references must agree");
	}
	auto mappings = Array(mapping, "concept_mappings", true);
	if (mappings.size() != 2) {
		Fail("mapping", "capability", "only two fact root mappings; User is a nested referent");
	}
	case_insensitive_map_t<yyjson_val *> mapped;
	for (auto cm : mappings) {
		Keys(cm, {"concept", "object_mappings", "link_mappings"}, "fact concept mapping");
		auto name = Text(cm, "concept");
		if (!roots.count(name) || !mapped.emplace(name, cm).second) {
			Fail(name, "mapping", "distinct configured root mappings required");
		}
	}
	for (auto &source : b.fact_input.sources) {
		auto &dataset = *b.model.FindDataset(source.dataset);
		auto concept = root_objects.at(source.concept);
		source.dataset = dataset.name;
		source.concept = Text(concept, "concept");
		source.user_concept = b.user_concept;
		source.target_identity_relationship = b.user_id_relation;
		for (auto &other : b.fact_input.sources) {
			if (StringUtil::CIEquals(dataset.name, other.concept) ||
			    StringUtil::CIEquals(dataset.name, b.user_concept)) {
				Fail(dataset.name, "bind", "dataset and concept namespaces must be distinct");
			}
		}
		if (!dataset.unique_keys.empty()) {
			Fail(dataset.name, "capability", "only full primary key supported for fact profile");
		}
		case_insensitive_map_t<string> relations;
		case_insensitive_set_t targets;
		for (auto relation : Array(concept, "relationships", true)) {
			auto name = Text(relation, "name"), t = target(relation, "ManyToOne");
			if (!relations.emplace(name, t).second || !targets.insert(t).second ||
			    (t != b.user_concept && t != "Date" && t != "String" && t != "Decimal")) {
				Fail(name, "capability", "one User/Date[/product String]/Decimal role required");
			}
		}
		vector<string> identity_relations;
		for (auto id : Array(concept, "identify_by", true)) {
			identity_relations.push_back(ArrayText(id));
		}
		if (identity_relations.size() < 2 || identity_relations.size() > 3 ||
		    relations.size() != identity_relations.size() + 1) {
			Fail(source.concept, "identity", "ordered User/date[/product] identity and amount required");
		}
		for (idx_t i = 0; i < identity_relations.size(); i++) {
			auto it = relations.find(identity_relations[i]);
			auto expected = i == 0 ? b.user_concept : i == 1 ? "Date" : "String";
			if (it == relations.end() || it->second != expected) {
				Fail(source.concept, "identity", "ordered User/date[/product String] identity required");
			}
		}
		auto user_ref = [&](yyjson_val *object) {
			auto refs = Array(object, "referent_mappings", true);
			if (refs.size() != 1) {
				Fail(source.concept, "mapping", "single nested User id required");
			}
			Keys(refs[0], {"relationship", "expression"}, "fact User id");
			if (Text(refs[0], "relationship") != b.user_id_relation) {
				Fail(source.concept, "mapping", "same declared User id required");
			}
			return FactLogicalField(dataset, Text(refs[0], "expression"));
		};
		auto identity = [&](yyjson_val *object) {
			Keys(object, {"concept", "referent_mappings"}, "fact root object");
			if (Member(object, "concept") && !StringUtil::CIEquals(Text(object, "concept"), source.concept)) {
				Fail(source.concept, "mapping", "root concept override refused");
			}
			auto refs = Array(object, "referent_mappings", true);
			if (refs.size() != identity_relations.size()) {
				Fail(source.concept, "identity", "full root tuple required");
			}
			vector<string> fields;
			for (idx_t i = 0; i < refs.size(); i++) {
				if (!StringUtil::CIEquals(Text(refs[i], "relationship"), identity_relations[i])) {
					Fail(source.concept, "identity", "referent order must agree with root identity");
				}
				Keys(refs[i],
				     i == 0 ? std::initializer_list<const char *> {"relationship", "referent_mappings"}
				            : std::initializer_list<const char *> {"relationship", "expression"},
				     "fact root referent");
				fields.push_back(i == 0 ? user_ref(refs[i]) : FactLogicalField(dataset, Text(refs[i], "expression")));
			}
			return fields;
		};
		auto cm = mapped.at(source.concept);
		auto objects = Array(cm, "object_mappings", true), links = Array(cm, "link_mappings", true);
		if (objects.size() != 1 || links.size() != 1) {
			Fail(source.concept, "mapping", "one root object and full link tree required");
		}
		Keys(links[0], {"object_mapping", "children"}, "fact link tree");
		source.identity_fields = identity(objects[0]);
		if (identity(Member(links[0], "object_mapping")) != source.identity_fields) {
			Fail(source.concept, "identity", "object and link identity differ");
		}
		case_insensitive_set_t bound;
		for (auto link : Array(links[0], "children", true)) {
			Keys(link, {"relationship", "object_mapping"}, "fact scalar/User link");
			auto name = Text(link, "relationship");
			auto it = relations.find(name);
			if (it == relations.end() || !bound.insert(name).second) {
				Fail(name, "mapping", "unknown or duplicate mapped relationship");
			}
			auto object = Member(link, "object_mapping");
			auto t = it->second;
			Keys(object,
			     t == b.user_concept ? std::initializer_list<const char *> {"concept", "referent_mappings"}
			                         : std::initializer_list<const char *> {"concept", "expression"},
			     "fact value object");
			if (Text(object, "concept") != t) {
				Fail(name, "mapping", "mapped role target differs");
			}
			auto field = t == b.user_concept ? user_ref(object) : FactLogicalField(dataset, Text(object, "expression"));
			if (dataset.FindField(field)->datatype != (t == b.user_concept ? "String" : t)) {
				Fail(field, "bind", "declared logical datatype differs from role");
			}
			if (t == b.user_concept) {
				source.user_field = field;
			} else if (t == "Date") {
				source.date_field = field;
			} else if (t == "String") {
				source.product_field = field;
			} else {
				source.amount_field = field;
			}
		}
		if (bound.size() != relations.size() ||
		    (source.identity_fields != vector<string>({source.user_field, source.date_field}) &&
		     source.identity_fields != vector<string>({source.user_field, source.date_field, source.product_field}))) {
			Fail(source.concept, "mapping", "all links and full mapped identity must agree");
		}
		vector<string> physical_identity;
		for (auto &name : source.identity_fields) {
			physical_identity.push_back(FactPhysicalField(dataset, *dataset.FindField(name)));
		}
		if (dataset.primary_key.size() != physical_identity.size()) {
			Fail(dataset.name, "identity", "full physical primary key required");
		}
		for (idx_t i = 0; i < physical_identity.size(); i++) {
			if (!StringUtil::CIEquals(dataset.primary_key[i], physical_identity[i])) {
				Fail(dataset.name, "identity", "physical key order must agree with mapped identity");
			}
		}
		if (dataset.fields.size() != relations.size()) {
			Fail(dataset.name, "capability", "only complete mapped role fields supported");
		}
		for (auto &field : dataset.fields) {
			source.fields.push_back({field.name, FactPhysicalField(dataset, field), LogicalType::INVALID});
		}
	}
	case_insensitive_set_t windows;
	for (auto name : Array(config, "windows", true)) {
		auto text = ArrayText(name);
		auto metric = b.model.FindMetric(text);
		if (!metric || !windows.insert(metric->name).second) {
			Fail(text, "capability", "distinct declared window names required");
		}
	}
	for (auto &metric : b.model.metrics) {
		if (metric.expression.dialect != OSSIE_SQL_2026) {
			Fail(metric.name, "capability", "fact program declarations require OSSIE_SQL_2026");
		}
		if (windows.count(metric.name)) {
			b.fact_input.windows.push_back({metric.name, metric.expression.sql});
		} else {
			b.fact_input.metrics.push_back({metric.name, metric.expression.sql});
		}
	}
}
vector<unique_ptr<TableRef>> FactSnapshotSources(Bundle &b) {
	vector<unique_ptr<TableRef>> sources;
	for (auto &child : b.fact_children) {
		auto source = SnapshotTable(*child);
		source->external_dependency->AddDependency("ossie_program_parent",
		                                           make_shared_ptr<SnapshotDependency>(b.owner.lock()));
		sources.push_back(std::move(source));
	}
	return sources;
}
void FreezeFacts(ClientContext &context, Bundle &b, Connection &engine) {
	Connection source(DatabaseInstance::GetDatabase(context));
	Run(context, source, "BEGIN TRANSACTION");
	optional_ptr<Catalog> catalog;
	optional_ptr<CatalogEntry> previous;
	vector<CapturedTable> captures;
	idx_t bytes = 0;
	for (auto &fact : b.fact_input.sources) {
		auto &dataset = *b.model.FindDataset(fact.dataset);
		auto capture = CaptureTable(context, source, dataset, catalog, previous, MAX_BYTES - bytes);
		bytes += capture.canonical.size();
		captures.push_back(std::move(capture));
		if (captures.size() == 1) {
			ObserveLoad(context, PopulationLoadCheckpoint::AFTER_FACT_CAPTURE);
		}
	}
	Run(context, source, "COMMIT");
	string evidence = "ossie-frozen-fact-program-v1:";
	for (auto &hash : {b.profile, b.manifest_hash, b.core_hash, b.ontology_hash, b.mapping_hash}) {
		Frame(evidence, hash);
	}
	for (idx_t i = 0; i < captures.size(); i++) {
		auto &capture = captures[i];
		auto &fact = b.fact_input.sources[i];
		auto &dataset = *b.model.FindDataset(fact.dataset);
		for (auto &field : fact.fields) {
			field.type = capture.types[PhysicalColumn(capture, field.physical_column)];
		}
		// Validate actual roles before GetValue or tuple hashing; a string label is not an actual type.
		auto actual = [&](const string &name) {
			return capture.types[PhysicalColumn(capture, FactPhysicalField(dataset, *dataset.FindField(name)))];
		};
		if (actual(fact.user_field) != LogicalType::VARCHAR || actual(fact.date_field) != LogicalType::DATE ||
		    actual(fact.amount_field) != LogicalType::DECIMAL(18, 2) ||
		    (!fact.product_field.empty() && actual(fact.product_field) != LogicalType::VARCHAR)) {
			Fail(fact.concept, "bind",
			     "actual User/DATE[/product]/money types must be VARCHAR/DATE[/VARCHAR]/DECIMAL18,2");
		}
		vector<idx_t> ids;
		for (auto &name : fact.identity_fields) {
			ids.push_back(PhysicalColumn(capture, FactPhysicalField(dataset, *dataset.FindField(name))));
		}
		unordered_set<string> keys;
		for (auto &row : capture.rows) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			string key;
			for (idx_t j = 0; j < ids.size(); j++) {
				auto &value = row[ids[j]];
				if (value.IsNull() ||
				    (j == 1 ? !Date::IsFinite(value.GetValue<date_t>()) : value.GetValue<string>().empty())) {
					Fail(fact.concept, "identity",
					     "full capture requires nonempty raw User/product and finite nonnull DATE");
				}
				Frame(key, j == 1 ? to_string(value.GetValue<date_t>().days) : value.GetValue<string>());
			}
			if (!keys.insert(key).second) {
				Fail(fact.concept, "identity",
				     "duplicate full source identity; identical and conflicting rows both refused");
			}
		}
		auto child = make_shared_ptr<Bundle>();
		child->owner = child;
		child->database = b.database;
		child->profile = FACT_PROFILE;
		child->model.datasets.push_back(dataset);
		child->model.datasets[0].source_bound = "memory.main.ossie_snapshot";
		child->physical_names = capture.names;
		child->rows = capture.rows.size();
		child->collection = make_uniq<ColumnDataCollection>(context, capture.types);
		DataChunk chunk;
		chunk.Initialize(Allocator::Get(context), capture.types);
		for (idx_t r = 0; r < capture.rows.size(); r++) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			idx_t slot = r % STANDARD_VECTOR_SIZE;
			for (idx_t c = 0; c < capture.types.size(); c++) {
				chunk.SetValue(c, slot, capture.rows[r][c]);
			}
			chunk.SetCardinality(slot + 1);
			if (slot + 1 == STANDARD_VECTOR_SIZE || r + 1 == capture.rows.size()) {
				child->collection->Append(chunk);
				chunk.Reset();
			}
		}
		child->content_hash = Hash(engine, capture.canonical);
		b.rows += child->rows;
		b.inputs.push_back({fact.dataset, dataset.source_bound, child->content_hash, child->rows});
		for (auto &value : {fact.dataset, fact.concept, fact.user_concept, fact.target_identity_relationship,
		                    fact.currency, child->content_hash}) {
			Frame(evidence, value);
		}
		for (auto &field : fact.identity_fields) {
			Frame(evidence, field);
		}
		b.fact_children.push_back(std::move(child));
	}
	b.fact_program =
	    ErrorBoundary("fact program", "rule", [&] { return make_uniq<FactProgram>(BindFactProgram(b.fact_input)); });
	vector<string> metrics;
	for (auto &metric : b.fact_program->metrics) {
		metrics.push_back(metric.name);
	}
	auto prepared =
	    engine.Prepare(CompileFactUsers(*b.fact_program, metrics, "common", {"all", "", {}}, FactSnapshotSources(b)));
	if (prepared->HasError()) {
		NormalizeError(prepared->GetErrorObject(), "fact program", "bind", ExceptionType::INVALID_INPUT);
	}
	for (idx_t i = 0; i < metrics.size(); i++) {
		if (!Matches(b.model.FindMetric(metrics[i])->datatype, prepared->GetTypes()[i + 1])) {
			Fail(metrics[i], "bind", "declared metric datatype differs from reduced/combined output");
		}
		b.metric_types[metrics[i]] = prepared->GetTypes()[i + 1].ToString();
	}
	if (!b.fact_program->windows.empty()) {
		vector<string> windows;
		for (auto &window : b.fact_program->windows) {
			windows.push_back(window.name);
		}
		auto window = engine.Prepare(
		    CompileFactWindows(*b.fact_program, windows, "entire_capture", Value(), Value(), FactSnapshotSources(b)));
		if (window->HasError()) {
			NormalizeError(window->GetErrorObject(), "fact windows", "bind", ExceptionType::INVALID_INPUT);
		}
		for (idx_t i = 0; i < windows.size(); i++) {
			if (!Matches(b.model.FindMetric(windows[i])->datatype, window->GetTypes()[i + 2])) {
				Fail(windows[i], "bind", "declared window datatype differs from actual output");
			}
			b.metric_types[windows[i]] = window->GetTypes()[i + 2].ToString();
		}
	}
	b.definition_model = b.model;
	b.content_hash = Hash(engine, evidence);
	b.snapshot_id = b.content_hash + ":" + to_string(NextBatchSequence());
}

void HistoryCoreBudget(yyjson_val *core) {
	if (Array(core, "datasets", true).size() != 2 || !Array(core, "metrics").empty() ||
	    !Array(core, "relationships").empty()) {
		Fail("history", "capability", "exactly two datasets, no core joins or metrics");
	}
	idx_t bytes = 0;
	for (auto dataset : Array(core, "datasets", true)) {
		auto fields = Array(dataset, "fields", true);
		if (fields.empty() || fields.size() > 256) {
			Fail("history", "capability", "bounded fields required");
		}
		for (auto field : fields) {
			for (auto dialect : Array(Member(field, "expression"), "dialects", true)) {
				auto sql = Text(dialect, "expression");
				if (sql.size() > 16384 || sql.size() > 131072 - bytes) {
					Fail("history", "resource", "bounded field rules required");
				}
				bytes += sql.size();
				if (Text(dialect, "dialect") != OSSIE_SQL_2026) {
					Fail("history", "capability", "OSSIE_SQL_2026 direct fields only");
				}
				auto parsed = Parser::ParseExpressionList(sql);
				if (parsed.size() != 1 || !parsed[0]->GetAlias().empty() ||
				    parsed[0]->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
					Fail("history", "capability", "one direct unaliased field before model lowering");
				}
			}
		}
	}
}
void HistoryDefinitions(Bundle &b, yyjson_val *ontology, yyjson_val *mapping, yyjson_val *program) {
	Keys(program, {"operator", "observation", "history", "relationship"}, "history program");
	if (Text(program, "operator") != "history_at_observation") {
		Fail("history", "capability", "explicit history_at_observation operator required");
	}
	b.history_relationship = Text(program, "relationship");
	for (auto name : {"observation", "history"}) {
		auto config = Member(program, name);
		Keys(config, {"dataset", "concept"}, name);
		HistorySource source;
		source.dataset = Text(config, "dataset");
		source.concept = Text(config, "concept");
		if (!b.model.FindDataset(source.dataset)) {
			Fail(source.dataset, "bind", "declared source required");
		}
		source.dataset = b.model.FindDataset(source.dataset)->name;
		b.history_sources.push_back(std::move(source));
	}
	if (StringUtil::CIEquals(b.history_sources[0].dataset, b.history_sources[1].dataset) ||
	    StringUtil::CIEquals(b.history_sources[0].concept, b.history_sources[1].concept)) {
		Fail("history", "bind", "distinct datasets and roots required");
	}
	Keys(ontology, {"version", "name", "description", "ai_context", "ontology", "requires"}, "history ontology");
	Version(ontology);
	Empty(ontology, "requires", "ontology");
	case_insensitive_map_t<yyjson_val *> concepts;
	case_insensitive_set_t builtins = {"Any", "Boolean", "Date", "DateTime", "Decimal", "Float", "Integer", "String"};
	for (auto c : Array(ontology, "ontology", true)) {
		Keys(c,
		     {"concept", "type", "description", "ai_context", "identify_by", "extends", "derived_by", "requires",
		      "relationships"},
		     "history concept");
		if (builtins.count(Text(c, "concept")) || Text(c, "type") != "EntityType" ||
		    !concepts.emplace(Text(c, "concept"), c).second) {
			Fail("history", "bind", "distinct EntityTypes required");
		}
		for (auto k : {"extends", "derived_by", "requires"}) {
			Empty(c, k, "history concept");
		}
	}
	if (concepts.size() != 4) {
		Fail("history", "capability", "two roots, User and target referents required");
	}
	auto relation_map = [&](const string &concept) {
		if (!concepts.count(concept)) {
			Fail(concept, "bind", "declared concept required");
		}
		case_insensitive_map_t<string> roles;
		for (auto r : Array(concepts.at(concept), "relationships", true)) {
			Keys(r,
			     {"name", "roles", "multiplicity", "verbalizes", "derived_by", "requires", "description", "ai_context"},
			     "history relationship");
			Empty(r, "derived_by", "history relationship");
			Empty(r, "requires", "history relationship");
			auto rr = Array(r, "roles", true);
			auto name = Text(r, "name");
			if (rr.size() != 1) {
				Fail(name, "capability", "one target role required");
			}
			Keys(rr[0], {"concept"}, "history role");
			if (Text(r, "multiplicity") != (name == "id" || name == "record_id" ? "OneToOne" : "ManyToOne") ||
			    !roles.emplace(name, Text(rr[0], "concept")).second) {
				Fail(name, "capability", "declared multiplicity and distinct role names required");
			}
			for (auto verbal : Array(r, "verbalizes", true)) {
				ArrayText(verbal);
			}
		}
		return roles;
	};
	auto obs_roles = relation_map(b.history_sources[0].concept),
	     hist_roles = relation_map(b.history_sources[1].concept);
	if (!obs_roles.count("user") || !obs_roles.count(b.history_relationship)) {
		Fail("history", "bind", "user and generated relationship required");
	}
	b.user_concept = obs_roles.at("user");
	b.history_target = obs_roles.at(b.history_relationship);
	case_insensitive_set_t expected_concepts = {b.history_sources[0].concept, b.history_sources[1].concept,
	                                            b.user_concept, b.history_target};
	if (expected_concepts.size() != 4) {
		Fail("history", "bind", "distinct typed roots/User/target required");
	}
	for (auto &c : concepts) {
		if (!expected_concepts.count(c.first)) {
			Fail(c.first, "bind", "unexpected concept");
		}
	}
	for (const auto &name : {b.user_concept, b.history_target}) {
		auto roles = relation_map(name);
		auto ids = Array(concepts.at(name), "identify_by", true);
		if (roles.size() != 1 || !roles.count("id") || roles.at("id") != "String" || ids.size() != 1 ||
		    ArrayText(ids[0]) != "id") {
			Fail(name, "identity", "single id OneToOne String referent required");
		}
	}
	if (obs_roles != case_insensitive_map_t<string>({{"user", b.user_concept},
	                                                 {"date", "Date"},
	                                                 {"product", "String"},
	                                                 {b.history_relationship, b.history_target}}) ||
	    hist_roles != case_insensitive_map_t<string>({{"record_id", "String"},
	                                                  {"user", b.user_concept},
	                                                  {"team", b.history_target},
	                                                  {"starts", "Date"},
	                                                  {"ends", "Date"}})) {
		Fail("history", "bind", "exact named and typed roles required");
	}
	Keys(mapping,
	     {"version", "name", "description", "ontology_ref", "semantic_model_ref", "concept_mappings",
	      "custom_extensions"},
	     "history mapping");
	Version(mapping);
	Keys(Member(mapping, "ontology_ref"), {"name", "iri"}, "ontology reference");
	Keys(Member(mapping, "semantic_model_ref"), {"name", "iri"}, "core reference");
	if (Text(Member(mapping, "ontology_ref"), "name") != Text(ontology, "name") ||
	    Text(Member(mapping, "semantic_model_ref"), "name") != b.model.name) {
		Fail("mapping", "bind", "matching document references required");
	}
	case_insensitive_map_t<yyjson_val *> mappings;
	for (auto cm : Array(mapping, "concept_mappings", true)) {
		Keys(cm, {"concept", "object_mappings", "link_mappings"}, "history concept mapping");
		if (!mappings.emplace(Text(cm, "concept"), cm).second) {
			Fail("mapping", "bind", "duplicate root mapping");
		}
	}
	if (mappings.size() != 2) {
		Fail("mapping", "capability", "two source root mappings only");
	}
	for (idx_t s = 0; s < 2; s++) {
		auto &src = b.history_sources[s];
		auto &dataset = *b.model.FindDataset(src.dataset);
		if (!mappings.count(src.concept)) {
			Fail(src.concept, "mapping", "source root mapping required");
		}
		for (auto &c : concepts) {
			if (StringUtil::CIEquals(dataset.name, c.first)) {
				Fail(dataset.name, "bind", "dataset/concept namespaces must differ");
			}
		}
		if (!dataset.unique_keys.empty()) {
			Fail(dataset.name, "capability", "only explicit primary key supported");
		}
		src.roles = s == 0 ? vector<string> {"user", "date", "product"}
		                   : vector<string> {"record_id", "user", "team", "starts", "ends"};
		vector<string> ids = s == 0 ? src.roles : vector<string> {"record_id"};
		auto declared_ids = Array(concepts.at(src.concept), "identify_by", true);
		if (declared_ids.size() != ids.size()) {
			Fail(src.concept, "identity", "full fixed source identity required");
		}
		for (idx_t i = 0; i < ids.size(); i++) {
			if (ArrayText(declared_ids[i]) != ids[i]) {
				Fail(src.concept, "identity", "identity role order differs");
			}
		}
		auto scalar = [&](yyjson_val *o) {
			Keys(o, {"relationship", "expression"}, "scalar referent");
			return FactLogicalField(dataset, Text(o, "expression"));
		};
		auto referent = [&](yyjson_val *o) {
			auto refs = Array(o, "referent_mappings", true);
			if (refs.size() != 1 || Text(refs[0], "relationship") != "id") {
				Fail(src.concept, "mapping", "single declared id referent required");
			}
			return scalar(refs[0]);
		};
		auto identity = [&](yyjson_val *o) {
			Keys(o, {"concept", "referent_mappings"}, "history root object");
			if (Text(o, "concept") != src.concept) {
				Fail(src.concept, "mapping", "root override refused");
			}
			auto refs = Array(o, "referent_mappings", true);
			if (refs.size() != ids.size()) {
				Fail(src.concept, "identity", "full root tuple required");
			}
			vector<string> fields;
			for (idx_t i = 0; i < ids.size(); i++) {
				if (Text(refs[i], "relationship") != ids[i]) {
					Fail(src.concept, "identity", "referent order differs");
				}
				if (s == 0 && i == 0) {
					Keys(refs[i], {"relationship", "referent_mappings"}, "User referent");
					fields.push_back(referent(refs[i]));
				} else {
					fields.push_back(scalar(refs[i]));
				}
			}
			return fields;
		};
		auto cm = mappings.at(src.concept);
		auto objs = Array(cm, "object_mappings", true), links = Array(cm, "link_mappings", true);
		if (objs.size() != 1 || links.size() != 1) {
			Fail(src.concept, "mapping", "one full source object/link tree required");
		}
		Keys(links[0], {"object_mapping", "children"}, "history link tree");
		auto id_fields = identity(objs[0]);
		if (identity(Member(links[0], "object_mapping")) != id_fields) {
			Fail(src.concept, "identity", "object/link identities differ");
		}
		case_insensitive_map_t<string> fields;
		for (auto link : Array(links[0], "children", true)) {
			Keys(link, {"relationship", "object_mapping"}, "source link");
			auto role = Text(link, "relationship");
			auto expected = s == 0 ? obs_roles : hist_roles;
			if (std::find(src.roles.begin(), src.roles.end(), role) == src.roles.end()) {
				Fail(role, "mapping", "unknown/direct generated link refused");
			}
			auto o = Member(link, "object_mapping");
			bool entity = role == "user" || role == "team";
			Keys(o,
			     entity ? std::initializer_list<const char *> {"concept", "referent_mappings"}
			            : std::initializer_list<const char *> {"concept", "expression"},
			     "mapped target");
			if (Text(o, "concept") != expected.at(role)) {
				Fail(role, "mapping", "declared typed target differs");
			}
			auto field = entity ? referent(o) : FactLogicalField(dataset, Text(o, "expression"));
			if (!fields.emplace(role, field).second) {
				Fail(role, "mapping", "duplicate mapped role");
			}
			if (dataset.FindField(field)->datatype != (entity ? "String" : expected.at(role))) {
				Fail(field, "bind", "declared field datatype differs");
			}
		}
		if (fields.size() != src.roles.size() || dataset.fields.size() != src.roles.size()) {
			Fail(src.concept, "mapping", "every source role/field must be mapped");
		}
		case_insensitive_set_t physical;
		for (auto &role : src.roles) {
			auto f = fields.at(role);
			auto column = FactPhysicalField(dataset, *dataset.FindField(f));
			if (!physical.insert(column).second) {
				Fail(role, "mapping", "distinct physical role columns required");
			}
			src.fields.push_back(f);
		}
		vector<string> expected_ids;
		for (auto &id : ids) {
			expected_ids.push_back(fields.at(id));
		}
		if (id_fields != expected_ids || dataset.primary_key.size() != ids.size()) {
			Fail(src.concept, "identity", "mapped identity/physical key differs");
		}
		for (idx_t i = 0; i < ids.size(); i++) {
			if (!StringUtil::CIEquals(dataset.primary_key[i],
			                          FactPhysicalField(dataset, *dataset.FindField(id_fields[i])))) {
				Fail(src.concept, "identity", "primary key order differs");
			}
		}
	}
	b.root = b.history_sources[0].concept;
}
void FreezeHistory(ClientContext &context, Bundle &b, Connection &engine) {
	Connection source(DatabaseInstance::GetDatabase(context));
	Run(context, source, "BEGIN TRANSACTION");
	optional_ptr<Catalog> catalog;
	optional_ptr<CatalogEntry> previous;
	vector<CapturedTable> captures;
	idx_t bytes = 0;
	for (auto &src : b.history_sources) {
		auto capture =
		    CaptureTable(context, source, *b.model.FindDataset(src.dataset), catalog, previous, MAX_BYTES - bytes);
		bytes += capture.canonical.size();
		captures.push_back(std::move(capture));
		if (captures.size() == 1) {
			ObserveLoad(context, PopulationLoadCheckpoint::AFTER_HISTORY_OBSERVATION_CAPTURE);
		}
	}
	Run(context, source, "COMMIT");
	std::vector<HistoryRecord> records;
	string evidence = "ossie-frozen-history-v1:";
	for (auto &v : {b.profile, b.manifest_hash, b.core_hash, b.ontology_hash, b.mapping_hash, b.history_relationship,
	                b.user_concept, b.history_target}) {
		Frame(evidence, v);
	}
	for (idx_t s = 0; s < 2; s++) {
		auto &capture = captures[s];
		auto &src = b.history_sources[s];
		auto &ds = *b.model.FindDataset(src.dataset);
		vector<idx_t> columns;
		for (idx_t i = 0; i < src.fields.size(); i++) {
			auto col = PhysicalColumn(capture, FactPhysicalField(ds, *ds.FindField(src.fields[i])));
			auto type = (src.roles[i] == "date" || src.roles[i] == "starts" || src.roles[i] == "ends")
			                ? LogicalType::DATE
			                : LogicalType::VARCHAR;
			if (capture.types[col] != type) {
				Fail(src.roles[i], "bind", "actual history role type differs from declared VARCHAR/DATE");
			}
			columns.push_back(col);
		}
		unordered_set<string> obs_keys;
		for (auto &row : capture.rows) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			string key;
			for (idx_t i = 0; i < columns.size(); i++) {
				auto &value = row[columns[i]];
				bool date = src.roles[i] == "date" || src.roles[i] == "starts" || src.roles[i] == "ends";
				if (value.IsNull() ||
				    (date ? !Date::IsFinite(value.GetValue<date_t>()) : value.GetValue<string>().empty())) {
					Fail(src.roles[i], "identity",
					     "full capture requires nonnull/nonempty raw identity and finite DATE");
				}
				if (s == 0) {
					Frame(key, date ? to_string(value.GetValue<date_t>().days) : value.GetValue<string>());
				}
			}
			if (s == 0) {
				if (!obs_keys.insert(key).second) {
					Fail(src.concept, "identity", "duplicate full observation tuple");
				}
			} else {
				records.push_back({row[columns[0]].GetValue<string>(), row[columns[1]].GetValue<string>(),
				                   row[columns[2]].GetValue<string>(), row[columns[3]].GetValue<date_t>(),
				                   row[columns[4]].GetValue<date_t>()});
			}
		}
		auto child = make_shared_ptr<Bundle>();
		child->owner = child;
		child->database = b.database;
		child->profile = HISTORY_PROFILE;
		child->model.datasets.push_back(ds);
		child->model.datasets[0].source_bound = "memory.main.ossie_snapshot";
		child->physical_names = capture.names;
		child->rows = capture.rows.size();
		child->collection = make_uniq<ColumnDataCollection>(context, capture.types);
		DataChunk chunk;
		chunk.Initialize(Allocator::Get(context), capture.types);
		for (idx_t r = 0; r < capture.rows.size(); r++) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			idx_t slot = r % STANDARD_VECTOR_SIZE;
			for (idx_t c = 0; c < capture.types.size(); c++) {
				chunk.SetValue(c, slot, capture.rows[r][c]);
			}
			chunk.SetCardinality(slot + 1);
			if (slot + 1 == STANDARD_VECTOR_SIZE || r + 1 == capture.rows.size()) {
				child->collection->Append(chunk);
				chunk.Reset();
			}
		}
		child->content_hash = Hash(engine, capture.canonical);
		b.rows += child->rows;
		b.inputs.push_back({src.dataset, ds.source_bound, child->content_hash, child->rows});
		Frame(evidence, src.dataset);
		Frame(evidence, src.concept);
		Frame(evidence, child->content_hash);
		for (auto &f : src.fields) {
			Frame(evidence, f);
		}
		b.history_children.push_back(std::move(child));
	}
	try {
		b.history_index = make_uniq<HistoryIndex>(BuildHistoryIndex(records, [&] {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
		}));
	} catch (const HistoryValidationError &error) {
		Fail("history", error.stage, error.what());
	}
	b.definition_model = b.model;
	b.content_hash = Hash(engine, evidence);
	b.snapshot_id = b.content_hash + ":" + to_string(NextBatchSequence());
}

shared_ptr<Bundle> LoadBundle(ClientContext &context, const string &manifest_path) {
	if (!context.transaction.IsAutoCommit()) {
		Fail("bundle", "snapshot", "load requires committed native tables outside an explicit transaction");
	}
	auto bundle = make_shared_ptr<Bundle>();
	bundle->owner = bundle;
	bundle->database = context.db;
	auto manifest_bytes = Read(context, manifest_path);
	Document manifest(manifest_bytes);
	manifest.NoNUL();
	auto root = manifest.Root();
	bundle->profile = Text(root, "profile");
	Keys(root,
	     (bundle->profile == FACT_PROFILE || bundle->profile == HISTORY_PROFILE)
	         ? std::initializer_list<const char *> {"profile", "core", "ontology", "mapping", "program"}
	     : bundle->profile == JOIN_PROFILE
	         ? std::initializer_list<const char *> {"profile", "core", "ontology", "mapping", "join"}
	         : std::initializer_list<const char *> {"profile", "core", "ontology", "mapping"},
	     "bundle manifest");
	if (bundle->profile != PROFILE && bundle->profile != JOIN_PROFILE && bundle->profile != OBSERVATION_PROFILE &&
	    bundle->profile != RELATION_PROFILE && bundle->profile != FACT_PROFILE && bundle->profile != HISTORY_PROFILE) {
		Fail("profile", "capability", "unknown implementation profile");
	}
	auto engine = Engine(*bundle);
	bundle->manifest_hash = Hash(*engine, manifest_bytes);
	auto &fs = FileSystem::GetFileSystem(context);
	auto slash = manifest_path.find_last_of("/\\");
	auto directory = slash == string::npos ? string(".") : manifest_path.substr(0, slash);
	auto read_definition = [&](const char *kind, string &digest) {
		auto spec = Member(root, kind);
		Keys(spec, {"path", "name", "sha256"}, kind);
		auto path = Text(spec, "path");
		path = fs.IsPathAbsolute(path) ? path : fs.JoinPath(directory, path);
		auto bytes = Read(context, path);
		digest = Hash(*engine, bytes);
		if (Text(spec, "sha256") != digest) {
			Fail(kind, "hash", "definition SHA-256 mismatch");
		}
		Document document(bytes);
		document.NoNUL();
		if (Text(document.Root(), "name") != Text(spec, "name")) {
			Fail(kind, "bind", "manifest document name mismatch");
		}
		return bytes;
	};
	auto core_bytes = read_definition("core", bundle->core_hash);
	auto ontology_bytes = read_definition("ontology", bundle->ontology_hash);
	auto mapping_bytes = read_definition("mapping", bundle->mapping_hash);
	auto core_json = LooksLikeYaml(core_bytes) ? YamlToJson(core_bytes, "core") : core_bytes;
	if (bundle->profile == HISTORY_PROFILE) {
		Document core(core_json);
		HistoryCoreBudget(core.Root());
		bundle->model = ParseModel(core_json, RebindMap());
		Document ontology(ontology_bytes), mapping(mapping_bytes);
		HistoryDefinitions(*bundle, ontology.Root(), mapping.Root(), Member(root, "program"));
		bundle->ontology_name = Text(ontology.Root(), "name");
		bundle->ontology_version = Text(ontology.Root(), "version");
		bundle->mapping_name = Text(mapping.Root(), "name");
		bundle->mapping_version = Text(mapping.Root(), "version");
		FreezeHistory(context, *bundle, *engine);
		return bundle;
	}
	if (bundle->profile == FACT_PROFILE) {
		Document core(core_json);
		FactCoreBudget(core.Root());
		bundle->model = ParseModel(core_json, RebindMap(), MetricReferencePolicy::DECLARED_METRICS);
		Document ontology(ontology_bytes), mapping(mapping_bytes);
		FactDefinitions(*bundle, ontology.Root(), mapping.Root(), Member(root, "program"));
		bundle->ontology_name = Text(ontology.Root(), "name");
		bundle->ontology_version = Text(ontology.Root(), "version");
		bundle->mapping_name = Text(mapping.Root(), "name");
		bundle->mapping_version = Text(mapping.Root(), "version");
		FreezeFacts(context, *bundle, *engine);
		return bundle;
	}
	bundle->model = ParseModel(core_json, RebindMap());
	bundle->definition_model = bundle->model;
	if (bundle->profile == JOIN_PROFILE) {
		FreezeJoin(context, *bundle, Member(root, "join"), *engine);
	}
	if (bundle->model.datasets.size() != 1 || !bundle->model.relationships.empty()) {
		Fail(bundle->model.name, "capability", "profile supports exactly one dataset and no core joins");
	}
	auto &dataset = bundle->model.datasets[0];
	if (dataset.name.empty() || dataset.fields.empty() || dataset.fields.size() > 256) {
		Fail(dataset.name, "capability", "require 1..256 declared fields");
	}
	for (auto &field : dataset.fields) {
		if (field.name.empty()) {
			Fail(dataset.name, "bind", "empty field name");
		}
		Strict(*field.expression.tree);
	}
	if (bundle->model.metrics.empty() || bundle->model.metrics.size() > 64) {
		Fail("metrics", "capability", "require 1..64 metrics");
	}
	for (auto &metric : bundle->model.metrics) {
		if (metric.name.empty() || StringUtil::StartsWith(StringUtil::Lower(metric.name), "_ossie_")) {
			Fail(metric.name, "bind", "empty or reserved metric name");
		}
		auto expanded = metric.expression.tree->Copy();
		Strict(*expanded, true);
		ExpandMetric(*bundle, expanded);
		Strict(*expanded, true);
		if (!HasAggregate(*expanded)) {
			Fail(metric.name, "capability", "metric requires SUM, COUNT or AVG aggregation");
		}
		CompileToSQL(bundle->model, {metric.name}, {}, {}, CompileOptions());
	}
	Document ontology(ontology_bytes), mapping(mapping_bytes);
	if (bundle->profile == OBSERVATION_PROFILE || bundle->profile == RELATION_PROFILE) {
		ObservationDefinitions(*bundle, ontology.Root(), mapping.Root());
	} else {
		Definitions(*bundle, ontology.Root(), mapping.Root());
	}
	bundle->ontology_name = Text(ontology.Root(), "name");
	bundle->ontology_version = Text(ontology.Root(), "version");
	bundle->mapping_name = Text(mapping.Root(), "name");
	bundle->mapping_version = Text(mapping.Root(), "version");
	for (auto &entry : bundle->provenance) {
		if (StringUtil::CIEquals(entry.first[0], bundle->root) ||
		    StringUtil::CIEquals(entry.first[0], bundle->derived)) {
			Fail(entry.first[0], "bind", "dataset and concept namespaces must be distinct");
		}
	}
	if (StringUtil::CIEquals(dataset.name, bundle->root) || StringUtil::CIEquals(dataset.name, bundle->derived) ||
	    (!bundle->user_concept.empty() && StringUtil::CIEquals(dataset.name, bundle->user_concept))) {
		Fail(dataset.name, "bind", "dataset and concept namespaces must be distinct");
	}
	if (bundle->profile == PROFILE || bundle->profile == OBSERVATION_PROFILE || bundle->profile == RELATION_PROFILE) {
		bundle->original_source = dataset.source_bound;
	}
	if (bundle->profile == PROFILE || bundle->profile == OBSERVATION_PROFILE || bundle->profile == RELATION_PROFILE) {
		auto parts = QualifiedName::ParseComponents(dataset.source_bound);
		if (parts.size() != 3) {
			Fail(dataset.name, "snapshot", "fully qualified catalog.schema.table source required");
		}
		Connection source(DatabaseInstance::GetDatabase(context));
		Run(context, source, "BEGIN TRANSACTION");
		auto entry =
		    Catalog::GetEntry(*source.context, parts[0], parts[1], EntryLookupInfo(CatalogType::TABLE_ENTRY, parts[2]),
		                      OnEntryNotFound::RETURN_NULL);
		if (!entry || entry->type != CatalogType::TABLE_ENTRY || entry->temporary ||
		    !entry->Cast<TableCatalogEntry>().IsDuckTable()) {
			Fail(dataset.name, "snapshot", "only committed native tables supported; TEMP/view/external source refused");
		}
		for (auto &column : entry->Cast<TableCatalogEntry>().GetColumns().Logical()) {
			if (column.Generated()) {
				Fail(dataset.name, "snapshot", "generated source columns not supported");
			}
		}
		auto capture = Run(context, source,
		                   "SELECT * FROM " + Quote(parts[0]) + "." + Quote(parts[1]) + "." + Quote(parts[2]) +
		                       " LIMIT " + to_string(MAX_ROWS + 1));
		Run(context, source, "COMMIT");
		auto &materialized = capture->Cast<MaterializedQueryResult>();
		bundle->rows = materialized.RowCount();
		if (bundle->rows > MAX_ROWS || capture->types.size() > 256) {
			Fail(dataset.name, "snapshot", "batch exceeds 10000 rows or 256 physical columns; no truncation performed");
		}
		bundle->physical_names = capture->names;
		string content =
		    "ossie-observation-v1:" + to_string(bundle->rows) + ":" + to_string(capture->types.size()) + ":";
		auto frame = [&](const string &text) {
			content += to_string(text.size()) + ":" + text;
		};
		for (idx_t col = 0; col < capture->types.size(); col++) {
			if (!Portable(capture->types[col]) || (capture->types[col].id() == LogicalTypeId::VARCHAR &&
			                                       !StringType::GetCollation(capture->types[col]).empty())) {
				Fail(capture->names[col], "snapshot", "unsupported physical column type or collation");
			}
			frame(capture->names[col]);
			frame(capture->types[col].ToString());
		}
		while (auto chunk = capture->Fetch()) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			for (idx_t row = 0; row < chunk->size(); row++) {
				for (idx_t col = 0; col < chunk->ColumnCount(); col++) {
					frame(chunk->GetValue(col, row).ToSQLString());
					if (content.size() > MAX_BYTES) {
						Fail(dataset.name, "snapshot", "batch exceeds 16 MiB canonical value limit");
					}
				}
			}
		}
		bundle->collection = materialized.TakeCollection();
		bundle->content_hash = Hash(*engine, content);
		bundle->snapshot_id = bundle->content_hash + ":" + to_string(NextBatchSequence());
		bundle->inputs.push_back({dataset.name, bundle->original_source, bundle->content_hash, bundle->rows});
	}
	dataset.source_bound = "memory.main.ossie_snapshot";
	string projection = "SELECT ";
	for (idx_t index = 0; index < dataset.fields.size(); index++) {
		if (index) {
			projection += ", ";
		}
		projection += FieldTree(*bundle, dataset.fields[index].name)->ToString();
	}
	projection += " FROM main.ossie_snapshot AS " + Quote(dataset.name);
	auto prepared = engine->Prepare(FrozenSQL(*bundle, projection));
	if (prepared->HasError()) {
		NormalizeError(prepared->GetErrorObject(), dataset.name, "bind", ExceptionType::INVALID_INPUT);
	}
	for (idx_t index = 0; index < dataset.fields.size(); index++) {
		if (!Matches(dataset.fields[index].datatype, prepared->GetTypes()[index])) {
			Fail(dataset.fields[index].name, "bind",
			     "logical datatype incompatible with bound physical expression type");
		}
		bundle->field_types[dataset.fields[index].name] = prepared->GetTypes()[index].ToString();
	}
	for (auto &field : dataset.fields) {
		BooleanContexts(*bundle, *FieldTree(*bundle, field.name));
	}
	for (auto &metric : bundle->model.metrics) {
		auto expanded = metric.expression.tree->Copy();
		ExpandMetric(*bundle, expanded);
		BooleanContexts(*bundle, *expanded);
	}
	for (auto &metric : bundle->model.metrics) {
		auto source_ref = SnapshotTable(*bundle);
		CompileOptions options;
		options.population_source = source_ref.get();
		auto statement = Compile(bundle->model, {metric.name}, {}, {}, options);
		for (auto &expression : statement->node->Cast<SelectNode>().select_list) {
			EngineFunctions(*expression);
		}
		auto bound = engine->Prepare(std::move(statement));
		if (bound->HasError()) {
			NormalizeError(bound->GetErrorObject(), metric.name, "bind", ExceptionType::INVALID_INPUT);
		}
		if (!Matches(metric.datatype, bound->GetTypes()[0])) {
			Fail(metric.name, "bind", "metric datatype incompatible with bound aggregate type");
		}
		bundle->metric_types[metric.name] = bound->GetTypes()[0].ToString();
	}
	CheckBoolean(*bundle, *bundle->rule);
	if (bundle->profile == OBSERVATION_PROFILE || bundle->profile == RELATION_PROFILE) {
		ObservationIdentityPreflight(context, *bundle, *engine);
		if (bundle->profile == RELATION_PROFILE) {
			auto &input = bundle->relation_input;
			input.root = bundle->root;
			input.user_concept = bundle->user_concept;
			input.dataset = dataset.name;
			input.user_column = FieldTree(*bundle, bundle->identity)->Cast<ColumnRefExpression>().column_names.back();
			for (auto &attribute : bundle->attributes) {
				if (attribute.target == bundle->user_concept) {
					input.mapped_user_relation = attribute.name;
				}
				if (attribute.target == "Integer") {
					auto field = dataset.FindField(attribute.field);
					idx_t index = 0;
					while (&dataset.fields[index] != field) {
						index++;
					}
					input.scalars.push_back(
					    {attribute.name,
					     FieldTree(*bundle, attribute.field)->Cast<ColumnRefExpression>().column_names.back(),
					     prepared->GetTypes()[index]});
				}
			}
			bundle->relation_program = ErrorBoundary(
			    bundle->root, "rule", [&]() { return make_uniq<RelationProgram>(BindRelationProgram(input)); });
			auto source = " FROM main.ossie_snapshot AS " + Quote(dataset.name);
			auto check = [&](const ParsedExpression &constraint, const ParsedExpression *population,
			                 const string &object) {
				auto clause = population ? "(" + population->ToString() + ") IS TRUE AND " : string();
				auto query = Run(context, *engine,
				                 FrozenSQL(*bundle, "SELECT count(*)" + source + " WHERE " + clause + "(" +
				                                        constraint.ToString() + ") IS DISTINCT FROM TRUE"))
				                 ->Fetch();
				if (query->GetValue(0, 0).GetValue<int64_t>() != 0) {
					Fail(object, "constraint", "entire declared population violates requires; snapshot not published");
				}
			};
			for (auto &constraint : bundle->relation_program->root_constraints) {
				check(*constraint, nullptr, bundle->root);
			}
			for (auto &relation : bundle->relation_program->relations) {
				CheckBoolean(*bundle, *relation.predicate);
				for (auto &constraint : relation.constraints) {
					check(*constraint, relation.predicate.get(), relation.name);
				}
			}
		}
		return bundle;
	}
	for (auto &attribute : bundle->attributes) {
		auto sql = "SELECT count(*) - count(" + FieldTree(*bundle, attribute.field)->ToString() + "), count(" +
		           FieldTree(*bundle, attribute.field)->ToString() + ") - count(DISTINCT " +
		           FieldTree(*bundle, attribute.field)->ToString() + ") FROM main.ossie_snapshot AS " +
		           Quote(dataset.name);
		auto result = Run(context, *engine, FrozenSQL(*bundle, sql))->Fetch();
		if ((attribute.field == bundle->identity && result->GetValue(0, 0).GetValue<int64_t>() != 0) ||
		    (attribute.unique && result->GetValue(1, 0).GetValue<int64_t>() != 0)) {
			Fail(attribute.name, "identity", "entire batch violates nonnull identity or OneToOne uniqueness");
		}
	}
	return bundle;
}

unique_ptr<ParsedExpression> Population(Bundle &bundle, const string &concept, const vector<string> &filters) {
	if (bundle.profile == OBSERVATION_PROFILE && concept == bundle.user_concept) {
		Fail(concept, "capability", "User is referent-only; direct objects and metrics queries unsupported");
	}
	if (concept != bundle.root && concept != bundle.derived) {
		Fail(concept, "bind", "unknown executable concept");
	}
	unique_ptr<ParsedExpression> predicate;
	if (concept == bundle.derived) {
		predicate = bundle.rule->Copy();
	}
	if (filters.size() > 64) {
		Fail(concept, "capability", "at most 64 request filters supported");
	}
	for (auto &filter : filters) {
		auto expression = Parse(filter);
		Strict(*expression);
		Resolve(bundle, expression);
		Strict(*expression);
		CheckBoolean(bundle, *expression);
		if (predicate) {
			predicate = make_uniq<ConjunctionExpression>(ExpressionType::CONJUNCTION_AND, std::move(predicate),
			                                             std::move(expression));
		} else {
			predicate = std::move(expression);
		}
	}
	return predicate;
}

unique_ptr<SelectStatement> QueryAST(Bundle &bundle, const string &concept, const vector<string> &metrics,
                                     const vector<string> &dimensions, const vector<string> &filters, bool objects) {
	auto predicate = Population(bundle, concept, filters);
	if (!objects) {
		CompileOptions options;
		options.population_predicate = predicate.get();
		auto source = SnapshotTable(bundle);
		options.population_source = source.get();
		vector<string> translated;
		for (auto &dimension : dimensions) {
			if (bundle.profile == OBSERVATION_PROFILE) {
				auto field = LogicalField(bundle, dimension);
				if (field != bundle.observation_identity[1] && field != bundle.observation_identity[2]) {
					Fail(dimension, "capability", "observation grouping supports only date and product fields");
				}
			}
			if (bundle.provenance.empty()) {
				translated.push_back(dimension);
			} else {
				translated.push_back(
				    make_uniq<ColumnRefExpression>(LogicalField(bundle, dimension), bundle.model.datasets[0].name)
				        ->ToString());
			}
		}
		auto statement = Compile(bundle.model, metrics, translated, {}, options);
		if (!bundle.provenance.empty()) {
			auto &select = statement->node->Cast<SelectNode>();
			for (idx_t i = 0; i < dimensions.size(); i++) {
				select.select_list[i]->SetAlias(dimensions[i]);
			}
		}
		return statement;
	}
	auto select = make_uniq<SelectNode>();
	if (bundle.profile != OBSERVATION_PROFILE) {
		for (auto name : {"entity_id", "record_id"}) {
			auto id = FieldTree(bundle, bundle.identity);
			id->SetAlias(name);
			select->select_list.push_back(std::move(id));
		}
	}
	for (auto &field : bundle.model.datasets[0].fields) {
		auto expression = FieldTree(bundle, field.name);
		expression->SetAlias(OriginalName(bundle, field.name));
		select->select_list.push_back(std::move(expression));
	}
	select->where_clause = std::move(predicate);
	select->from_table = SnapshotTable(bundle);
	auto statement = make_uniq<SelectStatement>();
	statement->node = std::move(select);
	return statement;
}

string JSONStrings(const vector<string> &values) {
	string result = "[";
	for (idx_t i = 0; i < values.size(); i++) {
		if (i) {
			result += ",";
		}
		result += JSONString(values[i]);
	}
	return result + "]";
}

string IdentityComponents(const Bundle &b, bool user) {
	vector<string> fields = b.profile == OBSERVATION_PROFILE
	                            ? (user ? vector<string> {b.identity} : b.observation_identity)
	                            : vector<string> {b.identity};
	string json = "[";
	for (idx_t i = 0; i < fields.size(); i++) {
		if (i) {
			json += ",";
		}
		string dataset = b.model.datasets[0].name, field = fields[i];
		for (const auto &entry : b.provenance) {
			if (entry.second == fields[i]) {
				dataset = entry.first[0];
				field = entry.first[1];
			}
		}
		auto relationship = b.profile == OBSERVATION_PROFILE
		                        ? (user ? b.user_id_relation : b.observation_relationships[i])
		                        : b.identity_relation;
		json += "{\"relationship\":" + JSONString(relationship) + ",\"dataset\":" + JSONString(dataset) +
		        ",\"field\":" + JSONString(field) + ",\"bound_type\":" + JSONString(b.field_types.at(fields[i]));
		if (b.profile == OBSERVATION_PROFILE && !user && i == 0) {
			json += ",\"referent\":{\"concept\":" + JSONString(b.user_concept) +
			        ",\"relationship\":" + JSONString(b.user_id_relation) + "}";
		}
		json += "}";
	}
	return json + "]";
}

string ReferencedUserIdentity(const Bundle &b) {
	return "{\"concept\":" + JSONString(b.user_concept) + ",\"components\":" + IdentityComponents(b, true) +
	       ",\"entity_equals_record\":false,\"projection\":\"distinct User IDs in the requested observation "
	       "scope\",\"objects_queryable\":false,\"metrics_queryable\":false}";
}

string DefinitionExpression(const ModelExpression &expression) {
	return "{\"selected_dialect\":" + JSONString(expression.dialect) +
	       ",\"available_dialects\":" + JSONStrings(expression.available_dialects) +
	       ",\"declared_sql\":" + JSONString(expression.sql) +
	       ",\"parsed_lowered_sql\":" + JSONString(expression.tree->ToString()) + "}";
}

// Explain the exact generated statement, not a separately compiled request or physical plan.
string PlanJSON(const Bundle &bundle, const SelectStatement &statement, const string &concept,
                const vector<string> &metrics, const vector<string> &filters, bool objects) {
	auto &select = statement.node->Cast<SelectNode>();
	auto definition = [](const string &name, const string &version, const string &hash) {
		return "{\"name\":" + JSONString(name) + ",\"version\":" + JSONString(version) +
		       ",\"sha256\":" + JSONString(hash) + "}";
	};
	auto derived = concept == bundle.derived;
	string identity_dataset = bundle.model.datasets[0].name, identity_field = bundle.identity;
	for (auto &entry : bundle.provenance) {
		if (entry.second == bundle.identity) {
			identity_dataset = entry.first[0];
			identity_field = entry.first[1];
		}
	}
	auto input_dataset = bundle.definition_model.FindDataset(bundle.model.datasets[0].name);
	if (!input_dataset) {
		Fail("grain", "explain", "population input lacks definition provenance");
	}
	auto rule = bundle.rule->Copy();
	EngineFunctions(*rule);
	string result =
	    "{\"schema_version\":" +
	    JSONString(bundle.profile == OBSERVATION_PROFILE ? "ossie-population-plan-v2" : "ossie-population-plan-v1") +
	    ","
	    "\"phase\":\"generated semantic SELECT AST\",\"executed\":false,\"physical_plan\":false,"
	    "\"sql_replayable\":false,\"request_kind\":" +
	    JSONString(objects ? "objects" : "metrics") + ",\"profile\":" + JSONString(bundle.profile) +
	    ",\"engine\":{\"dialect\":\"DUCKDB\",\"version\":" + JSONString(DuckDB::LibraryVersion()) +
	    ",\"source_id\":" + JSONString(DuckDB::SourceID()) + "},\"definitions\":{\"core\":" +
	    definition(bundle.definition_model.name, bundle.definition_model.spec_version, bundle.core_hash) +
	    ",\"ontology\":" + definition(bundle.ontology_name, bundle.ontology_version, bundle.ontology_hash) +
	    ",\"mapping\":" + definition(bundle.mapping_name, bundle.mapping_version, bundle.mapping_hash) +
	    ",\"manifest_sha256\":" + JSONString(bundle.manifest_hash) +
	    "},\"snapshot\":{\"id\":" + JSONString(bundle.snapshot_id) +
	    ",\"content_sha256\":" + JSONString(bundle.content_hash) +
	    ",\"captured_population_rows\":" + to_string(bundle.rows) +
	    ",\"row_order_sensitive\":true,\"filtered_rows_computed\":false},\"concept\":{\"name\":" + JSONString(concept) +
	    ",\"root\":" + JSONString(bundle.root) + ",\"formal_rule\":{\"owner\":" + JSONString(bundle.derived) +
	    ",\"applied\":" + (derived ? "true" : "false") +
	    ",\"input_language\":\"profile scalar SQL subset (no core dialect lowering)\","
	    "\"declared_sql\":" +
	    JSONString(bundle.original_rule) + ",\"resolved_definition_sql\":" + JSONString(rule->ToString()) +
	    "}},\"identity\":" +
	    (bundle.profile == OBSERVATION_PROFILE
	         ? "{\"concept\":" + JSONString(bundle.root) + ",\"components\":" + IdentityComponents(bundle, false) +
	               ",\"entity_equals_record\":true,\"scope\":\"unique daily observation tuple in the entire captured "
	               "batch\",\"internal_string_comparison\":\"binary\",\"referenced_entity\":" +
	               ReferencedUserIdentity(bundle) + "},"
	         : "{\"field\":" + JSONString(OriginalName(bundle, bundle.identity)) +
	               ",\"dataset\":" + JSONString(identity_dataset) + ",\"logical_field\":" + JSONString(identity_field) +
	               ",\"entity_equals_record\":true,\"scope\":\"one observation per identity in captured batch\"},") +
	    "\"grain\":{\"population_input_dataset\":" + JSONString(bundle.model.datasets[0].name) +
	    ",\"declared_primary_key\":" + JSONStrings(input_dataset->primary_key) +
	    ",\"aggregation_scope\":\"effective WHERE before GROUP BY\",\"basis\":" +
	    JSONString(bundle.profile == JOIN_PROFILE ? "validated fact grain before flattening"
	                                              : "validated sole source dataset") +
	    "},\"capture_join\":" + bundle.capture_join + ",\"inputs\":[";
	for (idx_t i = 0; i < bundle.inputs.size(); i++) {
		auto &input = bundle.inputs[i];
		if (i) {
			result += ",";
		}
		result += "{\"dataset\":" + JSONString(input.dataset) + ",\"source\":" + JSONString(input.source) +
		          ",\"captured_rows\":" + to_string(input.rows) + ",\"sha256\":" + JSONString(input.sha256) + "}";
	}
	result += "],\"metrics\":[";
	for (idx_t i = 0; i < metrics.size(); i++) {
		auto metric = bundle.definition_model.FindMetric(metrics[i]);
		if (!metric) {
			Fail(metrics[i], "explain", "compiled metric lacks definition provenance");
		}
		if (i) {
			result += ",";
		}
		result += "{\"name\":" + JSONString(metric->name) + ",\"declared_datatype\":" + JSONString(metric->datatype) +
		          ",\"expression\":" + DefinitionExpression(metric->expression) + "}";
	}
	result += "],\"declared_fields\":[";
	idx_t field_index = 0;
	for (auto &dataset : bundle.definition_model.datasets) {
		for (auto &field : dataset.fields) {
			if (field_index++) {
				result += ",";
			}
			result += "{\"dataset\":" + JSONString(dataset.name) + ",\"name\":" + JSONString(field.name) +
			          ",\"declared_datatype\":" + JSONString(field.datatype) +
			          ",\"is_time\":" + (field.is_time ? "true" : "false") +
			          ",\"is_time_explicit\":" + (field.is_time_explicit ? "true" : "false") +
			          ",\"expression\":" + DefinitionExpression(field.expression) + "}";
		}
	}
	result += "],\"filters\":{\"input_language\":\"profile scalar SQL subset (no core dialect lowering)\","
	          "\"requested_sql\":" +
	          JSONStrings(filters) + ",\"effective_where_sql\":" +
	          (select.where_clause ? JSONString(select.where_clause->ToString()) : "null") +
	          "},\"time\":{\"range_inferred\":false,\"basis\":\"effective WHERE and declared time roles only\"},"
	          "\"generated_ast\":{\"from_sql\":" +
	          JSONString(select.from_table->ToString()) + ",\"projections\":[";
	for (idx_t i = 0; i < select.select_list.size(); i++) {
		auto &expression = select.select_list[i];
		if (i) {
			result += ",";
		}
		result += "{\"ordinal\":" + to_string(i + 1) + ",\"alias\":" + JSONString(expression->GetAlias()) +
		          ",\"sql\":" + JSONString(expression->ToString()) + "}";
	}
	vector<string> groups;
	for (auto &expression : select.groups.group_expressions) {
		groups.push_back(expression->ToString());
	}
	result += "],\"group_by_sql\":" + JSONStrings(groups) +
	          "},\"result_contract\":{\"ordering\":\"unspecified\",\"groups\":\"sparse\","
	          "\"query_paging\":false,\"query_truncation\":false,\"calendar_completion\":false,"
	          "\"empty_ungrouped_count\":0,\"empty_ungrouped_sum_avg\":null},\"limitations\":["
	          "\"Parser AST explanation, not a bound physical or optimizer plan\","
	          "\"Captured row counts do not measure filtered population, groups or aggregate denominators\","
	          "\"Frozen scan requires its internal AST dependency; SQL text cannot be replayed\","
	          "\"Separate compile/query calls may bind different snapshots after reload\","
	          "\"No guarantee of exact JSON numeric transport or external renderer truncation behavior\"]}";
	return result;
}

struct RawByteLess {
	bool operator()(const string &left, const string &right) const {
		return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(), [](char a, char b) {
			return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
		});
	}
};

void DailyBudget(const string &name, idx_t actual, idx_t maximum) {
	if (actual > maximum) {
		Fail("daily_user_set." + name, "resource", "maximum=" + to_string(maximum) + " actual=" + to_string(actual));
	}
}

struct DailyRequest {
	string start_text, end_text;
	date_t start, end;
	idx_t days = 0;
	bool all_products = false;
	std::set<string, RawByteLess> products;
};

string DailyString(yyjson_val *value, const string &name) {
	if (!yyjson_is_str(value)) {
		Fail(name, "arguments", "string required");
	}
	return string(yyjson_get_str(value), yyjson_get_len(value));
}

void DailyKeys(yyjson_val *object, const vector<string> &allowed) {
	if (!yyjson_is_obj(object)) {
		Fail("request", "arguments", "JSON object required");
	}
	std::set<string> seen;
	size_t index, maximum;
	yyjson_val *key, *value;
	yyjson_obj_foreach(object, index, maximum, key, value) {
		auto name = DailyString(key, "key");
		if (!seen.insert(name).second) {
			Fail("request", "arguments", "duplicate JSON object key");
		}
		if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
			Fail("request", "arguments", "undeclared JSON object key");
		}
	}
}

date_t DailyDate(const string &text, const string &name) {
	if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
		Fail(name, "arguments", "exact YYYY-MM-DD required");
	}
	int32_t parts[3] = {0, 0, 0};
	for (idx_t i = 0; i < text.size(); i++) {
		if (i == 4 || i == 7) {
			continue;
		}
		if (text[i] < '0' || text[i] > '9') {
			Fail(name, "arguments", "exact YYYY-MM-DD required");
		}
		auto part = i < 4 ? 0 : i < 7 ? 1 : 2;
		parts[part] = parts[part] * 10 + (text[i] - '0');
	}
	date_t result;
	if (parts[0] < 1 || !Date::TryFromDate(parts[0], parts[1], parts[2], result)) {
		Fail(name, "arguments", "finite Gregorian date in years 0001..9999 required");
	}
	return result;
}

DailyRequest ParseDailyRequest(const string &bytes, bool states = false) {
	DailyBudget("request_json_utf8_bytes", bytes.size(), DAILY_REQUEST_BYTES);
	auto doc = yyjson_read(bytes.data(), bytes.size(), 0);
	if (!doc) {
		Fail("request", "arguments", "strict JSON request required");
	}
	std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> owner(doc, yyjson_doc_free);
	auto root = yyjson_doc_get_root(doc);
	if (states) {
		DailyKeys(root, {"start_date", "end_date", "products", "user_domain", "transitions"});
		if (DailyString(yyjson_obj_get(root, "user_domain"), "user_domain") != "captured_users" ||
		    DailyString(yyjson_obj_get(root, "transitions"), "transitions") != "within_request_days") {
			Fail("daily_states", "capability", "captured_users and within_request_days required");
		}
	} else {
		DailyKeys(root, {"start_date", "end_date", "products"});
	}
	DailyRequest result;
	result.start_text = DailyString(yyjson_obj_get(root, "start_date"), "start_date");
	result.end_text = DailyString(yyjson_obj_get(root, "end_date"), "end_date");
	result.start = DailyDate(result.start_text, "start_date");
	result.end = DailyDate(result.end_text, "end_date");
	if (result.start > result.end) {
		Fail("date_range", "arguments", "start_date must not exceed end_date");
	}
	result.days = static_cast<idx_t>(result.end.days - result.start.days) + 1;
	DailyBudget("inclusive_days", result.days, DAILY_MAX_DAYS);
	auto products = yyjson_obj_get(root, "products");
	DailyKeys(products, {"mode", "values"});
	auto mode = DailyString(yyjson_obj_get(products, "mode"), "products.mode");
	auto values = yyjson_obj_get(products, "values");
	if (mode == "all") {
		if (values) {
			Fail("products", "arguments", "all must not include values");
		}
		result.all_products = true;
	} else if (mode == "subset") {
		if (!yyjson_is_arr(values) || yyjson_get_len(values) == 0) {
			Fail("products.values", "arguments", "nonempty array of distinct raw strings required");
		}
		DailyBudget("subset_products", yyjson_get_len(values), DAILY_MAX_PRODUCTS);
		size_t index, maximum;
		yyjson_val *value;
		yyjson_arr_foreach(values, index, maximum, value) {
			auto product = DailyString(value, "products.values[" + to_string(index) + "]");
			if (product.empty()) {
				Fail("products.values", "arguments", "nonempty raw strings required");
			}
			DailyBudget("product_decoded_utf8_bytes", product.size(), DAILY_PRODUCT_BYTES);
			if (!result.products.insert(product).second) {
				Fail("products.values", "arguments", "duplicate decoded raw product");
			}
		}
	} else {
		Fail("products.mode", "arguments", "explicit all or subset required");
	}
	return result;
}

// Check remaining capacity before appending even escaped metadata or a large identity.
class DailyJSONWriter {
public:
	explicit DailyJSONWriter(ClientContext &context_p, string budget_p = "result_json_utf8_bytes",
	                         string object_p = "daily_user_set")
	    : context(context_p), budget(std::move(budget_p)), object(std::move(object_p)) {
	}
	void Append(const string &text) {
		if (object == "daily_user_set") {
			DailyBudget(budget, json.size() + text.size(), DAILY_RESULT_BYTES);
		} else if (json.size() + text.size() > DAILY_RESULT_BYTES) {
			Fail(object + "." + budget, "resource", "complete JSON exceeds 1MiB; no truncation");
		}
		json += text;
	}
	void Quoted(const string &text) {
		Append("\"");
		const char *hex = "0123456789abcdef";
		for (idx_t i = 0; i < text.size(); i++) {
			if (i % 1024 == 0 && context.IsInterrupted()) {
				throw InterruptException();
			}
			auto c = static_cast<unsigned char>(text[i]);
			if (c == '"' || c == '\\') {
				Append("\\");
				Append(string(1, text[i]));
			} else if (c < 32) {
				string escaped = "\\u00";
				escaped += hex[c >> 4];
				escaped += hex[c & 15];
				Append(escaped);
			} else {
				Append(string(1, text[i]));
			}
		}
		Append("\"");
	}
	void Field(const string &name, const string &value) {
		Append(",");
		Quoted(name);
		Append(":");
		Quoted(value);
	}
	string Finish() {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		return std::move(json);
	}

private:
	ClientContext &context;
	string budget;
	string object;
	string json;
};

string FactString(yyjson_val *value, const string &name) {
	auto text = DailyString(value, name);
	if (text.find('\0') != string::npos) {
		Fail(name, "arguments", "NUL-containing request string prohibited");
	}
	return text;
}
struct FactRequest {
	bool window = false;
	string bytes;
	vector<string> selected;
	FactCohort cohort;
	Value start, end;
	idx_t window_source = 0;
};
void FactBudget(const string &name, idx_t actual, idx_t limit) {
	if (actual > limit) {
		Fail("fact_program." + name, "resource", "request exceeds declared resource limit");
	}
}
FactCohort ParseFactCohort(yyjson_val *value, idx_t depth, idx_t &nodes) {
	FactBudget("cohort_depth", depth, 16);
	FactBudget("cohort_nodes", ++nodes, 64);
	DailyKeys(value, {"operation", "predicate", "children"});
	FactCohort result;
	result.operation = FactString(yyjson_obj_get(value, "operation"), "cohort.operation");
	if (result.operation == "all") {
		DailyKeys(value, {"operation"});
		return result;
	}
	if (result.operation == "predicate") {
		DailyKeys(value, {"operation", "predicate"});
		result.predicate = FactString(yyjson_obj_get(value, "predicate"), "cohort.predicate");
		if (result.predicate.empty()) {
			Fail("cohort.predicate", "arguments", "nonempty comparison required");
		}
		return result;
	}
	if (result.operation != "intersection" && result.operation != "union" && result.operation != "difference") {
		Fail("cohort.operation", "arguments", "all, predicate or binary set operation required");
	}
	DailyKeys(value, {"operation", "children"});
	auto children = yyjson_obj_get(value, "children");
	if (!yyjson_is_arr(children) || yyjson_get_len(children) != 2) {
		Fail("cohort.children", "arguments", "exactly two ordered children required");
	}
	size_t i, n;
	yyjson_val *child;
	yyjson_arr_foreach(children, i, n, child) result.children.push_back(ParseFactCohort(child, depth + 1, nodes));
	return result;
}
date_t FiniteDisplayDate(const string &text, const string &name) {
	// DuckDB formats the isolated finite INT32_MIN day, but its text cast rejects it.
	const date_t minimum(std::numeric_limits<int32_t>::min());
	if (text == Date::ToString(minimum)) {
		return minimum;
	}
	date_t value;
	idx_t pos = 0;
	bool special = false;
	if (text.empty() || text.size() > 32 ||
	    Date::TryConvertDate(text.data(), text.size(), pos, value, special, true) != DateCastResult::SUCCESS ||
	    pos != text.size() || special || !Date::IsFinite(value) || Date::ToString(value) != text) {
		Fail(name, "arguments", "finite canonical DuckDB DATE text required, including BC/wide years");
	}
	return value;
}
Value FactDisplayDate(yyjson_val *value, const string &name) {
	return Value::DATE(FiniteDisplayDate(FactString(value, name), name));
}
FactRequest ParseFactRequest(const string &bytes, const FactProgram &program, bool window) {
	FactBudget("request_json_utf8_bytes", bytes.size(), DAILY_REQUEST_BYTES);
	auto doc = yyjson_read(bytes.data(), bytes.size(), 0);
	if (!doc) {
		Fail("fact_request", "arguments", "strict JSON object required");
	}
	std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> owner(doc, yyjson_doc_free);
	auto root = yyjson_doc_get_root(doc);
	DailyKeys(root, window ? vector<string> {"input_scope", "windows", "display"}
	                       : vector<string> {"input_scope", "domain", "metrics", "cohort"});
	if (FactString(yyjson_obj_get(root, "input_scope"), "input_scope") != "entire_capture") {
		Fail("input_scope", "capability", "explicit entire_capture required");
	}
	FactRequest request;
	request.window = window;
	request.bytes = bytes;
	auto selection = yyjson_obj_get(root, window ? "windows" : "metrics");
	if (!yyjson_is_arr(selection) || yyjson_get_len(selection) == 0) {
		Fail("selection", "arguments", "nonempty declared name array required");
	}
	FactBudget("selected_names", yyjson_get_len(selection), window ? 16 : 32);
	case_insensitive_set_t seen;
	size_t i, n;
	yyjson_val *item;
	yyjson_arr_foreach(selection, i, n, item) {
		auto name = FactString(item, "selection");
		if (name.empty()) {
			Fail("selection", "arguments", "nonempty name required");
		}
		FactBudget("name_utf8_bytes", name.size(), 128);
		if (!seen.insert(name).second) {
			Fail(name, "arguments", "case duplicate selected name");
		}
		bool found = false;
		if (window) {
			for (auto &w : program.windows) {
				if (StringUtil::CIEquals(w.name, name)) {
					if (!request.selected.empty() && request.window_source != w.source) {
						Fail(name, "capability", "cannot mix source window grains");
					}
					request.window_source = w.source;
					request.selected.push_back(w.name);
					found = true;
					break;
				}
			}
		} else {
			for (auto &m : program.metrics) {
				if (StringUtil::CIEquals(m.name, name)) {
					request.selected.push_back(m.name);
					found = true;
					break;
				}
			}
		}
		if (!found) {
			Fail(name, "arguments", "unknown declared selection or wrong metric/window kind");
		}
	}
	if (window) {
		auto display = yyjson_obj_get(root, "display");
		DailyKeys(display, {"mode", "start_date", "end_date"});
		auto mode = FactString(yyjson_obj_get(display, "mode"), "display.mode");
		if (mode == "all") {
			DailyKeys(display, {"mode"});
		} else if (mode == "date_range") {
			request.start = FactDisplayDate(yyjson_obj_get(display, "start_date"), "display.start_date");
			request.end = FactDisplayDate(yyjson_obj_get(display, "end_date"), "display.end_date");
			if (request.start.GetValue<date_t>().days > request.end.GetValue<date_t>().days) {
				Fail("display", "arguments", "ordered display dates required");
			}
		} else {
			Fail("display.mode", "arguments", "explicit all or date_range required");
		}
	} else {
		if (FactString(yyjson_obj_get(root, "domain"), "domain") != "common") {
			Fail("domain", "capability", "explicit common row-presence User domain required");
		}
		idx_t nodes = 0;
		request.cohort = ParseFactCohort(yyjson_obj_get(root, "cohort"), 1, nodes);
	}
	return request;
}

struct HistoryRequest {
	bool all_dates = false, all_products = false;
	date_t start, end;
	string start_text, end_text;
	std::set<string, RawByteLess> products;
};
HistoryRequest ParseHistoryRequest(const string &bytes) {
	if (bytes.size() > DAILY_REQUEST_BYTES) {
		Fail("history.request_json_utf8_bytes", "resource", "original complete request exceeds16KiB");
	}
	auto doc = yyjson_read(bytes.data(), bytes.size(), 0);
	if (!doc) {
		Fail("history", "arguments", "strict JSON request required");
	}
	std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> owner(doc, yyjson_doc_free);
	auto root = yyjson_doc_get_root(doc);
	DailyKeys(root, {"dates", "products"});
	HistoryRequest r;
	auto dates = yyjson_obj_get(root, "dates");
	DailyKeys(dates, {"mode", "start_date", "end_date"});
	auto mode = DailyString(yyjson_obj_get(dates, "mode"), "dates.mode");
	if (mode == "all") {
		DailyKeys(dates, {"mode"});
		r.all_dates = true;
	} else if (mode == "range") {
		r.start_text = DailyString(yyjson_obj_get(dates, "start_date"), "start_date");
		r.end_text = DailyString(yyjson_obj_get(dates, "end_date"), "end_date");
		r.start = FiniteDisplayDate(r.start_text, "start_date");
		r.end = FiniteDisplayDate(r.end_text, "end_date");
		if (r.start > r.end) {
			Fail("history.dates", "arguments", "start_date must not exceed end_date");
		}
	} else {
		Fail("history.dates", "capability", "explicit all or finite inclusive range required");
	}
	auto products = yyjson_obj_get(root, "products");
	DailyKeys(products, {"mode", "values"});
	mode = DailyString(yyjson_obj_get(products, "mode"), "products.mode");
	if (mode == "all") {
		DailyKeys(products, {"mode"});
		r.all_products = true;
	} else if (mode == "subset") {
		auto values = yyjson_obj_get(products, "values");
		if (!yyjson_is_arr(values) || !yyjson_get_len(values)) {
			Fail("products.values", "arguments", "nonempty raw string array required");
		}
		if (yyjson_get_len(values) > DAILY_MAX_PRODUCTS) {
			Fail("history.products", "resource", "subset exceeds64 products");
		}
		size_t i, n;
		yyjson_val *v;
		yyjson_arr_foreach(values, i, n, v) {
			auto product = DailyString(v, "product");
			if (product.empty() || !r.products.insert(product).second) {
				Fail("products", "arguments", "nonempty distinct raw products required");
			}
			if (product.size() > DAILY_PRODUCT_BYTES) {
				Fail("history.products", "resource", "decoded product exceeds256 UTF8 bytes");
			}
		}
	} else {
		Fail("history.products", "capability", "explicit all or subset required");
	}
	return r;
}

struct PopulationBind : TableFunctionData {
	shared_ptr<Bundle> bundle;
	string sql;
	string plan_json;
	unique_ptr<SelectStatement> statement;
	string path;
	bool load = false;
	bool compile = false;
	bool discover = false;
	bool discover_v2 = false;
	bool encoded = false;
	bool objects = false;
	bool daily = false;
	bool daily_compile = false;
	bool daily_states = false;
	bool daily_state_discovery = false;
	bool fact_query = false;
	bool fact_compile = false;
	bool fact_discovery = false;
	bool history_discovery = false;
	bool history_query = false, history_compile = false;
	HistoryRequest history_request;
	FactRequest fact_request;
	unique_ptr<SelectStatement> fact_domain_statement;
	bool relation_discovery = false;
	bool relation_objects = false;
	bool relation_compile = false;
	idx_t relation_index = 0;
	DailyRequest daily_request;
	vector<LogicalType> daily_input_types;
	string concept;
	vector<LogicalType> result_types;
	vector<string> result_names;
	string vocabulary;
};

struct PopulationScan : GlobalTableFunctionState {
	unique_ptr<QueryResult> result;
	unique_ptr<DataChunk> chunk;
	vector<vector<Value>> rows;
	idx_t offset = 0;
};

shared_ptr<Bundle> Current(ClientContext &context) {
	auto bundle = PopulationState::Get(context).GetBundle();
	if (!bundle) {
		Fail("bundle", "bind", "no bundle loaded; call ossie_bundle_load");
	}
	return bundle;
}

shared_ptr<Bundle> LegacyCurrent(ClientContext &context, const string &operation) {
	auto bundle = Current(context);
	auto observer = context.registered_state->Get<PopulationLegacyBindObserver>(PopulationLegacyBindObserver::KEY);
	if (observer) {
		observer->Observe();
	}
	if (bundle->profile == HISTORY_PROFILE) {
		Fail(operation, "capability",
		     "history profile requires dedicated link/compile/program interfaces; legacy single-root entry refused");
	}
	if (bundle->profile == FACT_PROFILE) {
		Fail(operation, "capability",
		     "multi-fact program requires dedicated interfaces; legacy single-root operations refused");
	}
	if (bundle->profile == RELATION_PROFILE) {
		Fail(operation, "capability",
		     "relation program profile currently exposes only program discovery; relation objects interface pending");
	}
	return bundle;
}

unique_ptr<FunctionData> FactBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                  vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = Current(context);
	auto &b = *result->bundle;
	if (!b.fact_program) {
		Fail("fact_program", "capability", "multi-fact-user-program-v1 required");
	}
	result->fact_discovery = input.table_function.name == "ossie_fact_program";
	if (result->fact_discovery) {
		types = {LogicalType::VARCHAR};
		names = {"program_json"};
		return std::move(result);
	}
	if (input.inputs[0].IsNull()) {
		Fail("fact_request", "arguments", "request JSON required");
	}
	bool window = input.table_function.name == "ossie_fact_windows_json" ||
	              input.table_function.name == "ossie_fact_windows_compile";
	result->fact_compile = input.table_function.name == "ossie_fact_users_compile" ||
	                       input.table_function.name == "ossie_fact_windows_compile";
	result->fact_query = true;
	result->fact_request = ParseFactRequest(input.inputs[0].GetValue<string>(), *b.fact_program, window);
	auto &r = result->fact_request;
	lock_guard<mutex> guard(b.query_lock);
	result->statement =
	    window
	        ? CompileFactWindows(*b.fact_program, r.selected, "entire_capture", r.start, r.end, FactSnapshotSources(b))
	        : CompileFactUsers(*b.fact_program, r.selected, "common", r.cohort, FactSnapshotSources(b));
	if (!window) {
		result->fact_domain_statement = CompileFactCommonCount(*b.fact_program, FactSnapshotSources(b));
	}
	auto engine = Engine(b);
	auto prepared = engine->Prepare(result->statement->Copy());
	if (prepared->HasError()) {
		NormalizeError(prepared->GetErrorObject(), "fact_request", "bind", ExceptionType::INVALID_INPUT);
	}
	result->result_types = prepared->GetTypes();
	result->result_names = prepared->GetNames();
	vector<string> expected = {"user_id"};
	if (window) {
		expected.push_back("ds");
	}
	for (auto &name : r.selected) {
		expected.push_back(name);
	}
	if (result->result_names != expected || result->result_types.size() != expected.size() ||
	    result->result_types[0] != LogicalType::VARCHAR || (window && result->result_types[1] != LogicalType::DATE)) {
		Fail("fact_request", "bind", "canonical typed output schema required");
	}
	for (idx_t i = 0; i < r.selected.size(); i++) {
		if (result->result_types[i + (window ? 2 : 1)].ToString() != b.metric_types.at(r.selected[i])) {
			Fail(r.selected[i], "bind", "output metric type changed from load binding");
		}
	}
	if (!window) {
		auto count = engine->Prepare(result->fact_domain_statement->Copy());
		if (count->HasError()) {
			NormalizeError(count->GetErrorObject(), "common_user_count", "bind", ExceptionType::INVALID_INPUT);
		}
		if (count->GetTypes() != vector<LogicalType> {LogicalType::BIGINT}) {
			Fail("common_user_count", "bind", "BIGINT scalar required");
		}
	}
	types = {LogicalType::VARCHAR};
	names = {result->fact_compile ? "plan_json" : "result_json"};
	return std::move(result);
}

unique_ptr<FunctionData> RelationBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                      vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = Current(context);
	auto &b = *result->bundle;
	if (!b.relation_program) {
		Fail("relation", "capability", "relation program profile required");
	}
	if (input.inputs[0].IsNull()) {
		Fail("relation", "arguments", "relationship name required");
	}
	auto name = input.inputs[0].GetValue<string>();
	bool found = false;
	for (idx_t i = 0; i < b.relation_program->relations.size(); i++) {
		if (StringUtil::CIEquals(name, b.relation_program->relations[i].name)) {
			result->relation_index = i;
			found = true;
			break;
		}
	}
	if (!found) {
		Fail(name, "arguments", "unknown declared derived relationship");
	}
	auto &relation = b.relation_program->relations[result->relation_index];
	result->concept = relation.name;
	result->relation_objects = true;
	result->relation_compile = input.table_function.name == "ossie_relation_compile";
	lock_guard<mutex> guard(b.query_lock);
	string projection = "SELECT ";
	for (idx_t i = 0; i < b.observation_identity.size(); i++) {
		if (i) {
			projection += ",";
		}
		projection += FieldTree(b, b.observation_identity[i])->ToString();
	}
	projection += "," + FieldTree(b, b.identity)->ToString() + " FROM main.ossie_snapshot AS " +
	              Quote(b.model.datasets[0].name) + " WHERE (" + relation.predicate->ToString() + ") IS TRUE";
	result->statement = unique_ptr_cast<SQLStatement, SelectStatement>(FrozenSQL(b, projection));
	auto engine = Engine(b);
	auto prepared = engine->Prepare(result->statement->Copy());
	if (prepared->HasError()) {
		NormalizeError(prepared->GetErrorObject(), relation.name, "bind", ExceptionType::INVALID_INPUT);
	}
	result->result_types = prepared->GetTypes();
	if (result->result_types !=
	    vector<LogicalType> {LogicalType::VARCHAR, LogicalType::DATE, LogicalType::VARCHAR, LogicalType::VARCHAR}) {
		Fail(relation.name, "bind", "full link output must be VARCHAR, DATE, VARCHAR, VARCHAR");
	}
	types = {LogicalType::VARCHAR};
	names = {result->relation_compile ? "plan_json" : "result_json"};
	return std::move(result);
}

string RelationJSON(ClientContext &context, const PopulationBind &data) {
	auto &b = *data.bundle;
	auto &relation = b.relation_program->relations[data.relation_index];
	DailyJSONWriter writer(context, data.relation_compile ? "plan_json_utf8_bytes" : "result_json_utf8_bytes",
	                       "relation");
	writer.Append("{\"schema_version\":");
	writer.Quoted(data.relation_compile ? "ossie-relation-plan-v1" : "ossie-relation-result-v1");
	writer.Field("profile", b.profile);
	writer.Field("relationship", relation.name);
	writer.Field("root_concept", b.root);
	writer.Field("target_concept", b.user_concept);
	writer.Field("target_identity_relationship", b.user_id_relation);
	writer.Field("target_identity_field", b.identity);
	writer.Field("target_identity_bound_type", b.field_types.at(b.identity));
	writer.Field("snapshot_id", b.snapshot_id);
	writer.Field("content_sha256", b.content_hash);
	writer.Field("core_sha256", b.core_hash);
	writer.Field("ontology_sha256", b.ontology_hash);
	writer.Field("mapping_sha256", b.mapping_hash);
	writer.Field("manifest_sha256", b.manifest_hash);
	writer.Append(",\"root_identity_fields\":" + JSONStrings(b.observation_identity) +
	              ",\"root_identity_bound_types\":[\"VARCHAR\",\"DATE\",\"VARCHAR\"],\"scope\":\"entire captured root "
	              "population; selected relationship predicate only\",\"qualification\":\"TRUE "
	              "only\",\"load_constraints_validated\":true,\"dependencies\":" +
	              JSONStrings(relation.dependencies) + ",\"derived_by\":" + JSONStrings(relation.rules));
	writer.Field("bound_predicate", relation.predicate->ToString());
	writer.Append(",\"ordering\":\"source User raw UTF-8 bytes, DATE chronological, product raw UTF-8 bytes, target "
	              "User raw UTF-8 bytes\",\"paging\":false,\"truncated\":false,\"json_byte_limit\":1048576");
	if (data.relation_compile) {
		writer.Append(
		    ",\"executed\":false,\"members_computed\":false,\"full_link_projection_types_bound\":true,\"physical_"
		    "plan\":false,\"sql_replayable\":false");
		writer.Field("projection_sql", data.statement->ToString());
		writer.Append(",\"operators\":[\"frozen full-root projection with relationship TRUE predicate\",\"preserve "
		              "complete typed root plus target User link\",\"raw identity and typed DATE ordering\",\"complete "
		              "bounded JSON encoding\"]}");
		return writer.Finish();
	}
	ObserveRelation(context, PopulationRelationCheckpoint::BEFORE_EXECUTE);
	vector<vector<Value>> links;
	{
		lock_guard<mutex> guard(b.query_lock);
		auto engine = Engine(b);
		auto query = Run(context, *engine, data.statement->Copy());
		if (query->types != data.result_types) {
			Fail(relation.name, "execute", "bound full link types changed");
		}
		while (auto chunk = query->Fetch()) {
			for (idx_t row = 0; row < chunk->size(); row++) {
				if (context.IsInterrupted()) {
					throw InterruptException();
				}
				if (links.size() >= MAX_ROWS) {
					Fail(relation.name, "resource", "complete links exceed 10000");
				}
				vector<Value> link;
				for (idx_t col = 0; col < 4; col++) {
					auto value = chunk->GetValue(col, row);
					if (value.IsNull()) {
						Fail(relation.name, "execute", "NULL link identity prohibited");
					}
					link.push_back(std::move(value));
				}
				links.push_back(std::move(link));
			}
		}
		if (query->HasError()) {
			NormalizeError(query->GetErrorObject(), relation.name, "execute", ExceptionType::INVALID_INPUT);
		}
	}
	ObserveRelation(context, PopulationRelationCheckpoint::BEFORE_SORT);
	RawByteLess less;
	std::sort(links.begin(), links.end(), [&](const vector<Value> &a, const vector<Value> &z) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		for (idx_t i = 0; i < 4; i++) {
			if (i == 1) {
				auto left = a[i].GetValue<date_t>().days, right = z[i].GetValue<date_t>().days;
				if (left != right) {
					return left < right;
				}
			} else {
				const auto &left = StringValue::Get(a[i]);
				const auto &right = StringValue::Get(z[i]);
				if (left != right) {
					return less(left, right);
				}
			}
		}
		return false;
	});
	ObserveRelation(context, PopulationRelationCheckpoint::BEFORE_ENCODE);
	writer.Append(",\"executed\":true,\"members_computed\":true,\"links\":[");
	for (idx_t i = 0; i < links.size(); i++) {
		if (i) {
			writer.Append(",");
		}
		writer.Append("[");
		for (idx_t col = 0; col < 4; col++) {
			if (col) {
				writer.Append(",");
			}
			writer.Quoted(col == 1 ? links[i][col].ToString() : links[i][col].GetValue<string>());
		}
		writer.Append("]");
	}
	writer.Append("],\"link_count\":");
	writer.Quoted(to_string(links.size()));
	writer.Append("}");
	return writer.Finish();
}

unique_ptr<FunctionData> DailyBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                   vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = Current(context);
	result->daily_states = input.table_function.name == "ossie_daily_states_json" ||
	                       input.table_function.name == "ossie_daily_states_compile";
	if (input.inputs[0].IsNull() || input.inputs[1].IsNull()) {
		Fail("daily_user_set", "arguments", "concept and request_json required");
	}
	result->concept = input.inputs[0].GetValue<string>();
	auto &bundle = *result->bundle;
	if (bundle.profile != OBSERVATION_PROFILE || result->concept != bundle.derived) {
		Fail("daily_user_set", "capability", "current registered qualified daily observation concept required");
	}
	result->daily_request = ParseDailyRequest(input.inputs[1].GetValue<string>(), result->daily_states);
	result->daily = true;
	result->daily_compile = input.table_function.name == "ossie_daily_users_compile" ||
	                        input.table_function.name == "ossie_daily_states_compile";
	if (result->daily_states && bundle.user_count > MAX_ROWS / (2 * result->daily_request.days - 1)) {
		Fail("daily_states.combined_objects", "resource", "User*(2*days-1) exceeds 10000 before allocation");
	}
	lock_guard<mutex> guard(bundle.query_lock);
	result->statement = QueryAST(bundle, result->daily_states ? bundle.root : result->concept, {}, {}, {}, true);
	auto &select = result->statement->node->Cast<SelectNode>();
	select.select_list.clear();
	for (auto &field : bundle.observation_identity) {
		auto expression = FieldTree(bundle, field);
		EngineFunctions(*expression);
		select.select_list.push_back(std::move(expression));
	}
	if (result->daily_states) {
		auto rule = bundle.rule->Copy();
		EngineFunctions(*rule);
		select.select_list.push_back(std::move(rule));
	} else {
		EngineFunctions(*select.where_clause);
	}
	auto engine = Engine(bundle);
	auto prepared = engine->Prepare(result->statement->Copy());
	if (prepared->HasError()) {
		NormalizeError(prepared->GetErrorObject(), "daily_user_set", "bind", ExceptionType::INVALID_INPUT);
	}
	result->daily_input_types = prepared->GetTypes();
	auto expected = vector<LogicalType> {LogicalType::VARCHAR, LogicalType::DATE, LogicalType::VARCHAR};
	if (result->daily_states) {
		expected.push_back(LogicalType::BOOLEAN);
	}
	if (result->daily_input_types != expected) {
		Fail("daily_user_set", "bind", "qualified observation identity types changed");
	}
	types = {LogicalType::VARCHAR};
	names = {result->daily_compile ? "plan_json" : "result_json"};
	return std::move(result);
}

unique_ptr<FunctionData> LoadBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                  vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	if (input.inputs[0].IsNull()) {
		Fail("bundle", "load", "manifest path required");
	}
	result->path = input.inputs[0].GetValue<string>();
	result->load = true;
	names = {"profile", "snapshot_id", "content_sha256", "records"};
	types = {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::BIGINT};
	return std::move(result);
}

unique_ptr<FunctionData> DiscoverBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                      vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = LegacyCurrent(context, input.table_function.name);
	result->discover = true;
	result->discover_v2 = input.table_function.name == "ossie_concepts_v2";
	if (!result->discover_v2 && result->bundle->profile == OBSERVATION_PROFILE) {
		Fail("ossie_concepts", "capability", "compound identity requires ossie_concepts_v2");
	}
	if (result->discover_v2) {
		names = {"concept",
		         "parent",
		         "derived_by",
		         "identity_components_json",
		         "referenced_entity_identity_json",
		         "objects_queryable",
		         "metrics_queryable",
		         "captured_observation_rows",
		         "captured_user_count",
		         "profile",
		         "source",
		         "snapshot_id",
		         "content_sha256",
		         "core_sha256",
		         "ontology_sha256",
		         "mapping_sha256",
		         "manifest_sha256"};
		types = vector<LogicalType>(5, LogicalType::VARCHAR);
		types.insert(types.end(),
		             {LogicalType::BOOLEAN, LogicalType::BOOLEAN, LogicalType::BIGINT, LogicalType::BIGINT});
		types.insert(types.end(), 8, LogicalType::VARCHAR);
		return std::move(result);
	}
	names = {"concept",         "parent",         "identity_field", "derived_by",      "profile",
	         "snapshot_id",     "content_sha256", "core_sha256",    "ontology_sha256", "mapping_sha256",
	         "manifest_sha256", "source",         "records"};
	types = vector<LogicalType>(12, LogicalType::VARCHAR);
	types.push_back(LogicalType::BIGINT);
	return std::move(result);
}

// Versioned separately from the Ossie document and diagnostic plan schema.
constexpr const char *POPULATION_API_VERSION = "2.2.0";

unique_ptr<FunctionData> VocabularyBind(ClientContext &context, TableFunctionBindInput &input,
                                        vector<LogicalType> &types, vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = LegacyCurrent(context, input.table_function.name);
	result->vocabulary = input.table_function.name;
	if (result->vocabulary == "ossie_population_metrics") {
		names = {"name",
		         "datatype",
		         "bound_type",
		         "description",
		         "expression",
		         "selected_dialect",
		         "available_dialects_json",
		         "synonyms_json",
		         "examples_json",
		         "instructions",
		         "ai_context_json",
		         "custom_extensions_json",
		         "load_preflight_validated",
		         "execution_verified"};
		types = vector<LogicalType>(12, LogicalType::VARCHAR);
		types.push_back(LogicalType::BOOLEAN);
		types.push_back(LogicalType::BOOLEAN);
	} else if (result->vocabulary == "ossie_population_fields") {
		names = {"dataset",
		         "name",
		         "qualified_name",
		         "object_column",
		         "datatype",
		         "bound_type",
		         "label",
		         "description",
		         "expression",
		         "selected_dialect",
		         "available_dialects_json",
		         "synonyms_json",
		         "examples_json",
		         "instructions",
		         "ai_context_json",
		         "custom_extensions_json",
		         "is_time",
		         "is_time_explicit",
		         "is_computed",
		         "load_preflight_validated",
		         "execution_verified"};
		types = vector<LogicalType>(16, LogicalType::VARCHAR);
		for (idx_t i = 0; i < 5; i++) {
			types.push_back(LogicalType::BOOLEAN);
		}
	} else {
		names = {"api_version", "discovery_schema", "plan_schema", "ossie_spec_version", "capabilities_json"};
		types = vector<LogicalType>(5, LogicalType::VARCHAR);
	}
	for (auto name : {"profile", "snapshot_id", "content_sha256", "core_sha256", "ontology_sha256", "mapping_sha256",
	                  "manifest_sha256"}) {
		names.push_back(name);
		types.push_back(LogicalType::VARCHAR);
	}
	return std::move(result);
}

void VocabularyRows(const PopulationBind &data, vector<vector<Value>> &rows) {
	auto &b = *data.bundle;
	auto append = [&](vector<Value> row) {
		for (const auto &value : {b.profile, b.snapshot_id, b.content_hash, b.core_hash, b.ontology_hash,
		                          b.mapping_hash, b.manifest_hash}) {
			row.push_back(Value(value));
		}
		rows.push_back(std::move(row));
	};
	if (data.vocabulary == "ossie_population_metrics") {
		for (auto &m : b.definition_model.metrics) {
			append({Value(m.name), Value(m.datatype), Value(b.metric_types.at(m.name)), Value(m.description),
			        Value(m.expression.sql), Value(m.expression.dialect),
			        Value(JSONStrings(m.expression.available_dialects)), Value(JSONStrings(m.ai_context.synonyms)),
			        Value(JSONStrings(m.ai_context.examples)), Value(m.ai_context.instructions),
			        Value(m.ai_context.raw_json), Value(m.custom_extensions_json), Value::BOOLEAN(true),
			        Value::BOOLEAN(false)});
		}
	} else if (data.vocabulary == "ossie_population_fields") {
		for (auto &dataset : b.definition_model.datasets) {
			for (auto &f : dataset.fields) {
				auto qualified = make_uniq<ColumnRefExpression>(f.name, dataset.name)->ToString();
				auto internal = LogicalField(b, qualified);
				append({Value(dataset.name),
				        Value(f.name),
				        Value(qualified),
				        Value(OriginalName(b, internal)),
				        Value(f.datatype),
				        Value(b.field_types.at(internal)),
				        Value(f.label),
				        Value(f.description),
				        Value(f.expression.sql),
				        Value(f.expression.dialect),
				        Value(JSONStrings(f.expression.available_dialects)),
				        Value(JSONStrings(f.ai_context.synonyms)),
				        Value(JSONStrings(f.ai_context.examples)),
				        Value(f.ai_context.instructions),
				        Value(f.ai_context.raw_json),
				        Value(f.custom_extensions_json),
				        Value::BOOLEAN(f.is_time),
				        Value::BOOLEAN(f.is_time_explicit),
				        Value::BOOLEAN(f.is_computed),
				        Value::BOOLEAN(true),
				        Value::BOOLEAN(false)});
			}
		}
	} else {
		string capabilities =
		    "{\"scope\":\"bound frozen bundle\",\"concepts\":" +
		    JSONStrings(b.profile == OBSERVATION_PROFILE ? vector<string> {b.user_concept, b.root, b.derived}
		                                                 : vector<string> {b.root, b.derived}) +
		    ",\"engine\":{\"version\":" + JSONString(DuckDB::LibraryVersion()) +
		    ",\"source_id\":" + JSONString(DuckDB::SourceID()) +
		    "},"
		    "\"supported_profiles\":[\"gaap-scalar-population-v1\",\"fact-dimension-population-v1\",\"daily-"
		    "observation-population-v1\"],"
		    "\"validation\":\"all declarations pass load AST/type/individual expression binding preflight; "
		    "fields projected together; each metric without grouping; combined requests require separate binding\","
		    "\"input_limits\":{\"rows_per_input\":" +
		    to_string(MAX_ROWS) +
		    ",\"physical_columns_per_input\":256,\"canonical_bytes_per_input_or_joined_result\":" +
		    to_string(MAX_BYTES) +
		    ",\"peak_memory_bound\":false},"
		    "\"request_limits\":{\"metrics\":64,\"dimensions\":256,\"filters\":64},"
		    "\"execution\":{\"source\":\"committed native DuckDB tables\",\"reload\":\"explicit operator reload\","
		    "\"identity\":" +
		    JSONString(b.profile == OBSERVATION_PROFILE
		                   ? "typed User/date/product observation tuple; repeated User allowed"
		                   : "one observation per entity per batch") +
		    ",\"filters\":\"strict scalar predicates narrow the "
		    "concept rule\","
		    "\"time\":\"DATE; no inferred range or calendar completion\",\"live_query_join\":false,"
		    "\"capture_join\":" +
		    b.capture_join +
		    "},"
		    "\"results\":{\"ordering\":\"unspecified\",\"groups\":\"sparse\",\"paging\":false,"
		    "\"query_truncation\":false,\"empty_result_row_trace\":false},"
		    "\"compile\":{\"executed\":false,\"physical_plan\":false,\"final_output_binding\":false,\"sql_replayable\":"
		    "false},"
		    "\"mcp_example\":{\"list_encoding\":\"JSON-encoded string "
		    "arrays\",\"exact_numeric_transport_verified\":false,"
		    "\"result_schema\":\"ossie-population-result-v1\",\"numeric_encoding\":\"typed strings\","
		    "\"transport_size_and_truncation_verified\":\"fixed fixtures only\"},"
		    "\"uniform_errors\":true,\"error_contract\":{\"schema\":\"ossie-population-error-v1\","
		    "\"scope\":\"recoverable population callback errors and JSON list helper\","
		    "\"external_boundaries\":[\"SQL signature/conversion before callbacks\",\"outer table output binding\","
		    "\"MCP missing argument/unknown tool/protocol\",\"cancellation/fatal/internal/OOM/unknown native "
		    "failures\"]},"
		    "\"arbitrary_ontology_execution\":false,\"cross_call_reload_atomicity\":false}";
		capabilities.pop_back();
		capabilities += ",\"concept_discovery\":{\"function\":" +
		                JSONString(b.profile == OBSERVATION_PROFILE ? "ossie_concepts_v2" : "ossie_concepts") +
		                ",\"schema\":" +
		                JSONString(b.profile == OBSERVATION_PROFILE ? "ossie-population-concepts-v2"
		                                                            : "ossie-population-discovery-v1") +
		                "}";
		if (b.profile == OBSERVATION_PROFILE) {
			capabilities +=
			    ",\"observation_contract\":{\"groupable_fields\":" +
			    JSONStrings({OriginalName(b, b.observation_identity[1]), OriginalName(b, b.observation_identity[2])}) +
			    ",\"internal_string_comparison\":\"binary\",\"user_queryable\":false,\"user_count_scope\":"
			    "\"existential distinct projection of qualifying "
			    "observations in requested scope\",\"mean_denominator\":\"nonnull observation "
			    "amounts\",\"metric_forms\":\"COUNT(user), COUNT(DISTINCT user), COUNT(amount), SUM(amount), "
			    "AVG(amount)\"}";
		}

		capabilities +=
		    ",\"daily_user_set\":{\"enabled\":" + string(b.profile == OBSERVATION_PROFILE ? "true" : "false") +
		    ",\"sql_function\":\"ossie_daily_users_json(VARCHAR,VARCHAR)\",\"mcp_example_registered\":false,"
		    "\"request_schema\":\"ossie-daily-users-request-v1\","
		    "\"result_schema\":\"ossie-daily-user-set-result-v1\",\"compile_supported\":true,\"compile_function\":"
		    "\"ossie_daily_users_compile(VARCHAR,VARCHAR)\",\"plan_schema\":\"ossie-daily-user-set-plan-v1\","
		    "\"supported_concepts\":" +
		    JSONStrings(b.profile == OBSERVATION_PROFILE ? vector<string> {b.derived} : vector<string> {}) +
		    ",\"semantics\":\"every_calendar_day_exists_qualifying_observation\","
		    "\"date_scope\":\"inclusive consecutive Gregorian DATE years0001..9999\","
		    "\"product_scope\":\"explicit all or nonempty distinct subset; raw UTF-8 binary; may vary by day\","
		    "\"rule_scope\":\"same current formal rule and frozen bundle\","
		    "\"missing_observation\":\"no witness; no real business status inference\","
		    "\"ordering\":\"raw UTF-8 byte ascending\",\"paging\":false,\"truncated\":false,"
		    "\"limits\":{\"request_json_utf8_bytes\":" +
		    to_string(DAILY_REQUEST_BYTES) + ",\"inclusive_days\":" + to_string(DAILY_MAX_DAYS) +
		    ",\"subset_products\":" + to_string(DAILY_MAX_PRODUCTS) +
		    ",\"product_decoded_utf8_bytes\":" + to_string(DAILY_PRODUCT_BYTES) +
		    ",\"unique_user_date_pairs\":" + to_string(MAX_ROWS) + ",\"members\":" + to_string(MAX_ROWS) +
		    ",\"result_json_utf8_bytes_including_metadata\":" + to_string(DAILY_RESULT_BYTES) +
		    ",\"plan_json_utf8_bytes\":" + to_string(DAILY_RESULT_BYTES) +
		    "},\"resource_error_code\":\"RESOURCE_LIMIT_EXCEEDED\",\"hard_execution_deadline\":false,"
		    "\"peak_process_memory_bound\":false,\"outer_mcp_wire_byte_bound\":false,"
		    "\"cross_rpc_snapshot_atomicity\":false,\"business_data_completeness\":false}";
		capabilities += "}";
		append({Value(POPULATION_API_VERSION), Value("ossie-population-discovery-v1"),
		        Value(b.profile == OBSERVATION_PROFILE ? "ossie-population-plan-v2" : "ossie-population-plan-v1"),
		        Value(b.definition_model.spec_version), Value(capabilities)});
	}
}

unique_ptr<FunctionData> RequestBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                     vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = LegacyCurrent(context, input.table_function.name);
	if (input.inputs[0].IsNull()) {
		Fail("concept", "bind", "concept required");
	}
	auto concept = input.inputs[0].GetValue<string>();
	result->concept = concept;
	bool objects = input.inputs.size() == 2;
	auto metrics = objects ? vector<string>() : ToStringList(input.inputs[1], "ossie_concept_query");
	auto dimensions = objects ? vector<string>() : ToStringList(input.inputs[2], "ossie_concept_query");
	auto filters = ToStringList(input.inputs.back(), "ossie_population");
	if (metrics.size() > 64 || dimensions.size() > 256) {
		Fail(concept, "capability", "request exceeds 64 metrics or 256 dimensions");
	}
	result->compile = input.table_function.name == "ossie_concept_compile";
	result->encoded =
	    input.table_function.name == "ossie_objects_json" || input.table_function.name == "ossie_concept_query_json";
	// Empty metrics explicitly select the object path for the compile endpoint only.
	objects |= result->compile && metrics.empty();
	result->objects = objects;
	if (objects && !dimensions.empty()) {
		Fail(concept, "bind", "object compile does not accept dimensions");
	}
	lock_guard<mutex> guard(result->bundle->query_lock);
	result->statement = QueryAST(*result->bundle, concept, metrics, dimensions, filters, objects);
	auto trace = input.named_parameters.find("include_trace");
	if (trace != input.named_parameters.end() && !trace->second.IsNull() && trace->second.GetValue<bool>()) {
		auto &select = result->statement->node->Cast<SelectNode>();
		for (const auto &entry : {std::make_pair("_ossie_snapshot_id", result->bundle->snapshot_id),
		                          std::make_pair("_ossie_content_sha256", result->bundle->content_hash),
		                          std::make_pair("_ossie_ontology_sha256", result->bundle->ontology_hash)}) {
			auto expression = make_uniq<ConstantExpression>(Value(entry.second));
			expression->SetAlias(entry.first);
			select.select_list.push_back(std::move(expression));
		}
	}
	auto &query = result->statement->node->Cast<SelectNode>();
	for (auto &expression : query.select_list) {
		EngineFunctions(*expression);
	}
	for (auto &expression : query.groups.group_expressions) {
		EngineFunctions(*expression);
	}
	if (query.where_clause) {
		EngineFunctions(*query.where_clause);
	}
	result->sql = result->statement->ToString();
	if (result->compile) {
		result->plan_json = PlanJSON(*result->bundle, *result->statement, concept, metrics, filters, objects);
		names = {"sql",
		         "profile",
		         "snapshot_id",
		         "content_sha256",
		         "core_sha256",
		         "ontology_sha256",
		         "mapping_sha256",
		         "manifest_sha256",
		         "execution_target",
		         "plan_json",
		         "executed"};
		types = vector<LogicalType>(10, LogicalType::VARCHAR);
		types.push_back(LogicalType::BOOLEAN);
	} else {
		auto engine = Engine(*result->bundle);
		auto prepared = engine->Prepare(result->statement->Copy());
		if (prepared->HasError()) {
			NormalizeError(prepared->GetErrorObject(), concept, "bind", ExceptionType::INVALID_INPUT);
		}
		types = prepared->GetTypes();
		names = prepared->GetNames();
		if (result->encoded) {
			case_insensitive_set_t unique_names;
			for (auto &name : names) {
				if (!unique_names.insert(name).second) {
					Fail(name, "bind", "duplicate column name in typed JSON result");
				}
			}
			result->result_types = types;
			result->result_names = names;
			types = {LogicalType::VARCHAR};
			names = {"result_json"};
		}
	}
	return std::move(result);
}

void DailyScopeJSON(DailyJSONWriter &json, const PopulationBind &data) {
	auto &request = data.daily_request;
	auto &b = *data.bundle;
	json.Field("concept", data.concept);
	json.Field("user_concept", b.user_concept);
	json.Append(",\"request\":{\"start_date\":");
	json.Quoted(request.start_text);
	json.Field("end_date", request.end_text);
	json.Append(",\"products\":{\"mode\":");
	json.Quoted(request.all_products ? "all" : "subset");
	if (!request.all_products) {
		json.Append(",\"values\":[");
		bool first = true;
		for (auto &product : request.products) {
			if (!first) {
				json.Append(",");
			}
			first = false;
			json.Quoted(product);
		}
		json.Append("]");
	}
	json.Append("}}");
	json.Field("required_day_count", to_string(request.days));
	json.Field("eligibility_semantics", "every_calendar_day_exists_qualifying_observation");
	json.Append(",\"qualification_rule\":{\"concept\":");
	json.Quoted(b.derived);
	json.Field("declared_sql", b.original_rule);
	json.Append("}");
	for (auto &field :
	     {std::make_pair("profile", b.profile), std::make_pair("snapshot_id", b.snapshot_id),
	      std::make_pair("content_sha256", b.content_hash), std::make_pair("core_sha256", b.core_hash),
	      std::make_pair("ontology_sha256", b.ontology_hash), std::make_pair("mapping_sha256", b.mapping_hash),
	      std::make_pair("manifest_sha256", b.manifest_hash)}) {
		json.Field(field.first, field.second);
	}
}

string DailyResultJSON(ClientContext &context, const PopulationBind &data, QueryResult &query) {
	auto &request = data.daily_request;
	std::map<string, std::set<int32_t>, RawByteLess> seen;
	idx_t pairs = 0;
	while (auto chunk = query.Fetch()) {
		for (idx_t row = 0; row < chunk->size(); row++) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			auto date = chunk->GetValue(1, row).GetValue<date_t>();
			if (date < request.start || date > request.end) {
				continue;
			}
			auto product = chunk->GetValue(2, row).GetValue<string>();
			if (!request.all_products && request.products.find(product) == request.products.end()) {
				continue;
			}
			auto user = chunk->GetValue(0, row).GetValue<string>();
			if (seen[user].insert(date.days).second) {
				DailyBudget("unique_user_date_pairs", ++pairs, MAX_ROWS);
			}
		}
	}
	if (query.HasError()) {
		NormalizeError(query.GetErrorObject(), "daily_user_set", "execute", ExceptionType::INVALID_INPUT);
	}
	vector<string> members;
	for (auto &entry : seen) {
		if (entry.second.size() == request.days) {
			members.push_back(entry.first);
		}
	}
	DailyBudget("members", members.size(), MAX_ROWS);
	ObserveDaily(context, PopulationDailyCheckpoint::BEFORE_ENCODE);
	DailyJSONWriter json(context);
	json.Append(
	    "{\"schema_version\":\"ossie-daily-user-set-result-v1\",\"request_kind\":\"daily_users\",\"executed\":true");
	DailyScopeJSON(json, data);
	json.Field("member_count", to_string(members.size()));
	json.Append(",\"paging\":false,\"truncated\":false,\"complete\":true,\"ordering\":\"raw UTF-8 byte "
	            "ascending\",\"members\":[");
	for (idx_t i = 0; i < members.size(); i++) {
		if (i) {
			json.Append(",");
		}
		json.Quoted(members[i]);
	}
	json.Append("]}");
	return json.Finish();
}

string DailyPlanJSON(ClientContext &context, const PopulationBind &data) {
	DailyJSONWriter json(context, "plan_json_utf8_bytes");
	json.Append("{\"schema_version\":\"ossie-daily-user-set-plan-v1\",\"request_kind\":\"daily_users\","
	            "\"executed\":false,\"physical_plan\":false,\"final_output_binding\":false,"
	            "\"input_projection_binding\":true,\"sql_replayable\":false,\"members_computed\":false");
	DailyScopeJSON(json, data);
	json.Field("phase", "bound qualified observation projection and explicit calendar User reduction");
	json.Field("qualified_projection_sql", data.statement->ToString());
	json.Append(",\"bound_identity\":[");
	auto &b = *data.bundle;
	for (idx_t i = 0; i < b.observation_identity.size(); i++) {
		if (i) {
			json.Append(",");
		}
		json.Append("{\"field\":");
		json.Quoted(OriginalName(b, b.observation_identity[i]));
		json.Field("role", i == 0 ? "user" : i == 1 ? "date" : "product");
		json.Field("bound_type", data.daily_input_types[i].ToString());
		json.Append("}");
	}
	json.Append("],\"operators\":[{\"operator\":\"qualified_observation_projection\",\"executor\":\"DuckDB "
	            "AST\",\"qualification\":\"TRUE only\"},"
	            "{\"operator\":\"inclusive_date_filter\",\"executor\":\"C++\",\"identity_ordinal\":2},"
	            "{\"operator\":\"explicit_product_scope\",\"executor\":\"C++\",\"identity_ordinal\":3},"
	            "{\"operator\":\"distinct_user_date\",\"executor\":\"C++\",\"identity_ordinals\":[1,2]},"
	            "{\"operator\":\"date_count_equals_required_calendar_days\",\"executor\":\"C++\",\"required_count\":");
	json.Quoted(to_string(data.daily_request.days));
	json.Append("},{\"operator\":\"complete_members_order\",\"comparison\":\"raw UTF-8 byte ascending\"},"
	            "{\"operator\":\"bounded_complete_JSON\",\"byte_limit\":");
	json.Append(to_string(DAILY_RESULT_BYTES));
	json.Append(",\"truncated\":false}],"
	            "\"limitations\":[\"Projection SQL alone does not include the C++ scope and cross-day reduction\","
	            "\"Internal frozen AST dependency prevents ordinary SQL replay\","
	            "\"Compile does not calculate members, validate success result size or ensure execution completion\","
	            "\"Separate calls can bind different snapshots after reload\"],\"plan_json_utf8_byte_limit\":");
	json.Append(to_string(DAILY_RESULT_BYTES));
	json.Append("}");
	return json.Finish();
}

void DailyStateMetadata(DailyJSONWriter &json, const Bundle &b) {
	json.Field("state_api_version", "1.0.0");
	json.Field("profile", b.profile);
	json.Field("user_concept", b.user_concept);
	json.Field("qualification_concept", b.derived);
	json.Field("declared_rule", b.original_rule);
	json.Field("rule_scope", "current frozen rule evaluated on captured observations; no historical rule inference");
	json.Field("user_domain", "captured_users");
	json.Field("transitions", "within_request_days");
	json.Field("state_semantics", "any TRUE -> TRUE; nonempty all FALSE -> FALSE; otherwise UNKNOWN");
	json.Append(",\"carry_forward\":false");
	for (auto &field :
	     {std::make_pair("snapshot_id", b.snapshot_id), std::make_pair("content_sha256", b.content_hash),
	      std::make_pair("core_sha256", b.core_hash), std::make_pair("ontology_sha256", b.ontology_hash),
	      std::make_pair("mapping_sha256", b.mapping_hash), std::make_pair("manifest_sha256", b.manifest_hash)}) {
		json.Field(field.first, field.second);
	}
}

void DailyStateScope(DailyJSONWriter &json, const PopulationBind &data) {
	DailyStateMetadata(json, *data.bundle);
	auto &r = data.daily_request;
	json.Append(",\"request\":{\"start_date\":");
	json.Quoted(r.start_text);
	json.Field("end_date", r.end_text);
	json.Field("user_domain", "captured_users");
	json.Field("transitions", "within_request_days");
	json.Append(",\"products\":{\"mode\":");
	json.Quoted(r.all_products ? "all" : "subset");
	if (!r.all_products) {
		json.Append(",\"values\":[");
		idx_t i = 0;
		for (auto &product : r.products) {
			if (i++) {
				json.Append(",");
			}
			json.Quoted(product);
		}
		json.Append("]");
	}
	json.Append("}}");
	json.Field("required_day_count", to_string(r.days));
}

string DailyStateProgram(ClientContext &context, const Bundle &b) {
	DailyJSONWriter json(context, "program_json_utf8_bytes", "daily_states");
	json.Append(
	    "{\"schema_version\":\"ossie-daily-states-program-v1\",\"query_executed\":false,\"mcp_registered\":false");
	DailyStateMetadata(json, b);
	json.Field("captured_user_count", to_string(b.user_count));
	json.Field("captured_observation_rows", to_string(b.rows));
	json.Append(
	    ",\"functions\":[\"ossie_daily_states_json\",\"ossie_daily_states_compile\",\"ossie_daily_states_program\"],"
	    "\"state_identity\":[\"User\",\"DATE\"],\"comparison_identity\":[\"User\",\"from_DATE\",\"to_DATE\"],"
	    "\"status_values\":[\"TRUE\",\"FALSE\",\"UNKNOWN\"],\"includes_unchanged_comparisons\":true,"
	    "\"limits\":{\"days\":366,\"combined_objects\":10000,\"request_utf8_bytes\":16384,"
	    "\"complete_json_utf8_bytes\":1048576,\"subset_products\":64,\"product_utf8_bytes\":256},"
	    "\"cardinality_gate\":\"captured_User_count <= 10000/(2*days-1) before allocation\","
	    "\"refused\":[\"other user domains\",\"request-external predecessor\",\"carry-forward\","
	    "\"all-products qualification\",\"historical validity inference\",\"paging or truncation\"],"
	    "\"registered_user_domain\":false}");
	return json.Finish();
}

string DailyStatePlan(ClientContext &context, const PopulationBind &data) {
	DailyJSONWriter json(context, "plan_json_utf8_bytes", "daily_states");
	json.Append("{\"schema_version\":\"ossie-daily-states-plan-v1\",\"executed\":false,\"states_computed\":false,"
	            "\"comparisons_computed\":false,\"physical_plan\":false,\"sql_replayable\":false,"
	            "\"input_projection_binding\":true");
	DailyStateScope(json, data);
	json.Field("captured_user_count", to_string(data.bundle->user_count));
	json.Field("projection_sql", data.statement->ToString());
	vector<string> types;
	for (auto &type : data.daily_input_types) {
		types.push_back(type.ToString());
	}
	json.Append(
	    ",\"input_types\":" + JSONStrings(types) +
	    ",\"operators\":[\"frozen full-root User/date/product/rule BOOLEAN projection without qualification WHERE\","
	    "\"collect whole-capture raw User domain before scope\",\"explicit inclusive date and product filter\","
	    "\"preserve rule TRUE/FALSE/NULL; aggregate per User/natural day\","
	    "\"UNKNOWN for missing days; no carry\",\"all within-request adjacent comparisons including unchanged\","
	    "\"raw UTF-8 User and ascending natural-day order\",\"complete bounded JSON\"],"
	    "\"limits\":{\"combined_objects\":10000,\"complete_json_utf8_bytes\":1048576},"
	    "\"cardinality_gate\":\"captured_User_count <= 10000/(2*days-1) before allocation\","
	    "\"limitations\":[\"Compile does not scan or compute states or comparisons\","
	    "\"Load User count is capture metadata, not a query count\","
	    "\"Projection SQL does not include C++ reduction and is not independently replayable\","
	    "\"Compile does not guarantee encoded execution result size\"]}");
	return json.Finish();
}

const char *DailyStateLabel(uint8_t mask) {
	return mask & 1 ? "TRUE" : mask == 2 ? "FALSE" : "UNKNOWN";
}

string DailyStateResult(ClientContext &context, const PopulationBind &data, QueryResult &query) {
	auto &b = *data.bundle;
	auto &r = data.daily_request;
	if (query.types != data.daily_input_types) {
		Fail("daily_states", "execute", "full observation projection types changed");
	}
	if (b.user_count > MAX_ROWS / (2 * r.days - 1)) {
		Fail("daily_states", "resource", "combined states/comparisons exceed 10000");
	}
	std::map<string, vector<uint8_t>, RawByteLess> users;
	idx_t scanned = 0;
	while (auto chunk = query.Fetch()) {
		for (idx_t row = 0; row < chunk->size(); row++) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			if (++scanned > b.rows) {
				Fail("daily_states", "execute", "projection exceeds captured observation rows");
			}
			auto user_value = chunk->GetValue(0, row);
			if (user_value.IsNull()) {
				Fail("daily_states", "execute", "NULL User identity prohibited");
			}
			auto user = user_value.GetValue<string>();
			auto found = users.find(user);
			if (found == users.end()) {
				if (users.size() >= b.user_count) {
					Fail("daily_states", "execute", "projection domain exceeds captured User count");
				}
				found = users.emplace(std::move(user), vector<uint8_t>(r.days, 0)).first;
			}
			auto date = chunk->GetValue(1, row).GetValue<date_t>();
			auto product = chunk->GetValue(2, row).GetValue<string>();
			if (date < r.start || date > r.end || (!r.all_products && r.products.find(product) == r.products.end())) {
				continue;
			}
			auto value = chunk->GetValue(3, row);
			found->second[date.days - r.start.days] |= value.IsNull() ? 4 : value.GetValue<bool>() ? 1 : 2;
		}
	}
	if (query.HasError()) {
		NormalizeError(query.GetErrorObject(), "daily_states", "execute", ExceptionType::INVALID_INPUT);
	}
	if (scanned != b.rows || users.size() != b.user_count) {
		Fail("daily_states", "execute", "full frozen observation/User domain mismatch");
	}
	ObserveDailyState(context, PopulationDailyStateCheckpoint::AFTER_DOMAIN);
	DailyJSONWriter json(context, "result_json_utf8_bytes", "daily_states");
	json.Append("{\"schema_version\":\"ossie-daily-states-result-v1\",\"executed\":true,\"complete\":true,"
	            "\"paging\":false,\"truncated\":false");
	DailyStateScope(json, data);
	json.Field("user_count", to_string(users.size()));
	json.Field("state_count", to_string(users.size() * r.days));
	json.Field("comparison_count", to_string(users.size() * (r.days - 1)));
	json.Field("combined_object_count", to_string(users.size() * (2 * r.days - 1)));
	json.Field("ordering", "raw UTF-8 User ascending, natural day ascending");
	json.Append(",\"state_columns\":[\"user_id\",\"date\",\"state\"],\"states\":[");
	ObserveDailyState(context, PopulationDailyStateCheckpoint::BEFORE_EXPAND);
	idx_t count = 0;
	idx_t state_totals[3] = {0, 0, 0};
	for (auto &entry : users) {
		for (idx_t day = 0; day < r.days; day++) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			if (count++) {
				json.Append(",");
			}
			json.Append("[");
			json.Quoted(entry.first);
			json.Append(",");
			json.Quoted(Date::ToString(date_t(r.start.days + static_cast<int32_t>(day))));
			json.Append(",");
			json.Quoted(DailyStateLabel(entry.second[day]));
			state_totals[entry.second[day] & 1 ? 0 : entry.second[day] == 2 ? 1 : 2]++;
			json.Append("]");
		}
	}
	json.Append("],\"comparison_columns\":[\"user_id\",\"from_date\",\"to_date\",\"from_state\",\"to_state\","
	            "\"changed\"],\"comparisons\":[");
	count = 0;
	idx_t changed_count = 0;
	for (auto &entry : users) {
		for (idx_t day = 1; day < r.days; day++) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			if (count++) {
				json.Append(",");
			}
			json.Append("[");
			json.Quoted(entry.first);
			for (auto date : {r.start.days + static_cast<int32_t>(day - 1), r.start.days + static_cast<int32_t>(day)}) {
				json.Append(",");
				json.Quoted(Date::ToString(date_t(date)));
			}
			json.Append(",");
			json.Quoted(DailyStateLabel(entry.second[day - 1]));
			json.Append(",");
			json.Quoted(DailyStateLabel(entry.second[day]));
			bool changed = string(DailyStateLabel(entry.second[day - 1])) != DailyStateLabel(entry.second[day]);
			changed_count += changed ? 1 : 0;
			json.Append(changed ? ",true]" : ",false]");
		}
	}
	json.Append("],\"state_totals\":{\"unit\":\"User/day\"");
	json.Field("TRUE", to_string(state_totals[0]));
	json.Field("FALSE", to_string(state_totals[1]));
	json.Field("UNKNOWN", to_string(state_totals[2]));
	json.Append("}");
	json.Field("changed_comparison_count", to_string(changed_count));
	json.Append("}");
	ObserveDailyState(context, PopulationDailyStateCheckpoint::BEFORE_FINISH);
	return json.Finish();
}

string ResultEncoding(const LogicalType &type) {
	switch (type.id()) {
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
	case LogicalTypeId::HUGEINT:
		return "integer-text";
	case LogicalTypeId::DECIMAL:
		return "decimal-text";
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE:
		return "ieee754-text";
	case LogicalTypeId::BOOLEAN:
		return "boolean";
	case LogicalTypeId::DATE:
		return "date-text";
	case LogicalTypeId::VARCHAR:
		return "string";
	default:
		Fail(type.ToString(), "capability", "typed JSON result does not support this bound type");
	}
}

string ResultJSON(ClientContext &context, const PopulationBind &data, QueryResult &query) {
	if (query.types != data.result_types || query.names != data.result_names) {
		Fail("query", "execute", "typed JSON result schema differs from the bound schema");
	}
	auto &b = *data.bundle;
	string json =
	    "{\"schema_version\":\"ossie-population-result-v1\",\"request_kind\":" +
	    JSONString(data.objects ? "objects" : "metrics") + ",\"concept\":" + JSONString(data.concept) +
	    ",\"executed\":true,\"profile\":" + JSONString(b.profile) + ",\"snapshot_id\":" + JSONString(b.snapshot_id) +
	    ",\"content_sha256\":" + JSONString(b.content_hash) + ",\"core_sha256\":" + JSONString(b.core_hash) +
	    ",\"ontology_sha256\":" + JSONString(b.ontology_hash) + ",\"mapping_sha256\":" + JSONString(b.mapping_hash) +
	    ",\"manifest_sha256\":" + JSONString(b.manifest_hash) +
	    ",\"ordering\":\"unspecified\",\"paging\":false,\"truncated\":false,\"columns\":[";
	for (idx_t col = 0; col < query.types.size(); col++) {
		if (col) {
			json += ",";
		}
		json += "{\"ordinal\":" + to_string(col + 1) + ",\"name\":" + JSONString(query.names[col]) +
		        ",\"bound_type\":" + JSONString(query.types[col].ToString()) +
		        ",\"encoding\":" + JSONString(ResultEncoding(query.types[col])) + "}";
	}
	json += "],\"rows\":[";
	idx_t count = 0;
	while (auto chunk = query.Fetch()) {
		if (chunk->ColumnCount() != query.types.size()) {
			Fail("query", "execute", "typed JSON chunk width differs from the bound schema");
		}
		for (idx_t row = 0; row < chunk->size(); row++) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			if (count++) {
				json += ",";
			}
			json += "[";
			for (idx_t col = 0; col < query.types.size(); col++) {
				if (col) {
					json += ",";
				}
				auto value = chunk->GetValue(col, row);
				if (value.IsNull()) {
					json += "null";
				} else if (value.type().id() == LogicalTypeId::BOOLEAN) {
					json += value.GetValue<bool>() ? "true" : "false";
				} else if (value.type().id() == LogicalTypeId::FLOAT || value.type().id() == LogicalTypeId::DOUBLE) {
					auto number = value.GetValue<double>();
					auto text = std::isnan(number)                    ? "NaN"
					            : std::isinf(number)                  ? (number < 0 ? "-Infinity" : "+Infinity")
					            : number == 0 && std::signbit(number) ? "-0"
					                                                  : value.ToString();
					json += JSONString(text);
				} else {
					json += JSONString(value.type().id() == LogicalTypeId::VARCHAR ? value.GetValue<string>()
					                                                               : value.ToString());
				}
			}
			json += "]";
		}
	}
	if (query.HasError()) {
		NormalizeError(query.GetErrorObject(), "query", "execute", ExceptionType::INVALID_INPUT);
	}
	if (context.IsInterrupted()) {
		throw InterruptException();
	}
	return json + "],\"row_count\":" + to_string(count) + "}";
}

void ObserveFact(ClientContext &context, PopulationFactCheckpoint checkpoint) {
	auto observer = context.registered_state->Get<PopulationFactObserver>(PopulationFactObserver::KEY);
	if (observer) {
		observer->Observe(checkpoint);
	}
	if (context.IsInterrupted()) {
		throw InterruptException();
	}
}
void FactIdentityJSON(DailyJSONWriter &writer, const Bundle &b) {
	writer.Field("profile", b.profile);
	writer.Field("snapshot_id", b.snapshot_id);
	writer.Field("content_sha256", b.content_hash);
	writer.Field("core_sha256", b.core_hash);
	writer.Field("ontology_sha256", b.ontology_hash);
	writer.Field("mapping_sha256", b.mapping_hash);
	writer.Field("manifest_sha256", b.manifest_hash);
	writer.Field("user_concept", b.fact_program->user_concept);
	writer.Field("user_identity_relationship", b.fact_program->user_identity_relationship);
}
string FactProgramJSON(ClientContext &context, const PopulationBind &data) {
	auto &b = *data.bundle;
	auto &p = *b.fact_program;
	DailyJSONWriter w(context, "program_json_utf8_bytes", "fact_program");
	w.Append("{\"schema_version\":\"ossie-fact-program-v1\"");
	FactIdentityJSON(w, b);
	w.Field("project_api_version", POPULATION_API_VERSION);
	w.Append(",\"query_executed\":false,\"counts_computed\":false,\"mcp_registered\":false,\"functions\":[\"ossie_fact_"
	         "users_json\",\"ossie_fact_users_compile\",\"ossie_fact_windows_json\",\"ossie_fact_windows_compile\","
	         "\"ossie_fact_program\"],\"sources\":[");
	for (idx_t i = 0; i < p.sources.size(); i++) {
		auto &s = p.sources[i];
		auto &child = *b.fact_children[i];
		if (i) {
			w.Append(",");
		}
		w.Append("{\"dataset\":");
		w.Quoted(s.dataset);
		w.Field("concept", s.concept);
		w.Field("user_concept", s.user_concept);
		w.Field("target_identity_relationship", s.target_identity_relationship);
		w.Field("source", b.model.FindDataset(s.dataset)->source_bound);
		w.Field("currency", s.currency);
		w.Field("content_sha256", child.content_hash);
		w.Field("captured_rows", to_string(child.rows));
		w.Append(",\"identity_fields\":" + JSONStrings(s.identity_fields) + ",\"user_field\":");
		w.Quoted(s.user_field);
		w.Field("date_field", s.date_field);
		w.Field("product_field", s.product_field);
		w.Field("amount_field", s.amount_field);
		w.Append(",\"fields\":[");
		for (idx_t f = 0; f < s.fields.size(); f++) {
			auto &field = s.fields[f];
			if (f) {
				w.Append(",");
			}
			w.Append("{\"name\":");
			w.Quoted(field.name);
			w.Field("physical_column", field.physical_column);
			w.Field("bound_type", field.type.ToString());
			w.Append("}");
		}
		w.Append("]}");
	}
	w.Append("],\"metrics\":[");
	for (idx_t i = 0; i < p.metrics.size(); i++) {
		auto &m = p.metrics[i];
		if (i) {
			w.Append(",");
		}
		w.Append("{\"name\":");
		w.Quoted(m.name);
		w.Field("original_definition", b.fact_input.metrics[i].sql);
		w.Field("unit", m.unit);
		w.Field("bound_type", b.metric_types.at(m.name));
		w.Append("}");
	}
	w.Append("],\"windows\":[");
	for (idx_t i = 0; i < p.windows.size(); i++) {
		auto &window = p.windows[i];
		if (i) {
			w.Append(",");
		}
		w.Append("{\"name\":");
		w.Quoted(window.name);
		w.Field("original_definition", b.fact_input.windows[i].sql);
		w.Field("unit", window.unit);
		w.Field("bound_type", b.metric_types.at(window.name));
		w.Field("dataset", p.sources[window.source].dataset);
		w.Field("bound_expression", window.expression->ToString());
		w.Field("bound_catalog", window.expression->Cast<WindowExpression>().catalog);
		w.Field("bound_schema", window.expression->Cast<WindowExpression>().schema);
		w.Append("}");
	}
	w.Append("],\"domain\":\"common User by row presence in both separately reduced "
	         "sources\",\"cohort_semantics\":\"TRUE atomic member sets; binary intersection, union and ordered "
	         "difference within common\",\"count_semantics\":\"COUNT(User) source rows; COUNT(amount) nonNULL values; "
	         "common_user_count before cohort, selected_user_count after "
	         "cohort\",\"input_scope\":\"entire_capture\",\"window_semantics\":\"single User/date fact; explicit User "
	         "partition and DATE ASC NULLS LAST; whole capture computation before display DATE filtering; LAG1 "
	         "includes NULL, ROWS0..366 observes rows not calendar days\",\"supported_aggregates\":[\"SUM(mapped "
	         "amount)\",\"COUNT(mapped User or amount)\",\"same-currency SUM components +/-\"],\"refused\":[\"raw "
	         "fact-to-fact fan-out\",\"AVG/COUNT(*) in declarations\",\"cross-fact aggregate leaves\",\"ratios or "
	         "mixed measure units\",\"input/date/product filtering\",\"recursive cohorts or dependencies\",\"mixed "
	         "window fact grains\",\"calendar filling or history/state inference\",\"ordinary rendered SQL "
	         "replay\"],\"limits\":{\"sources\":2,\"rows_per_source\":10000,\"physical_columns_per_source\":256,"
	         "\"combined_canonical_bytes\":16777216,\"regular_metrics\":32,\"window_metrics\":16,\"name_ascii_bytes\":"
	         "128,\"rule_bytes\":16384,\"total_rule_bytes\":131072,\"expression_depth\":32,\"expression_nodes\":1024,"
	         "\"expanded_program_nodes\":32768,\"metric_dependency_depth\":16,\"cohort_nodes\":64,\"cohort_depth\":16,"
	         "\"request_json_bytes\":16384,\"complete_json_bytes\":1048576,\"result_rows\":10000},\"paging\":false,"
	         "\"truncated\":false,\"peak_memory_bound\":false,\"hash_set_normalized\":false}");
	return w.Finish();
}
string FactQueryJSON(ClientContext &context, const PopulationBind &data) {
	auto &b = *data.bundle;
	auto &r = data.fact_request;
	auto &p = *b.fact_program;
	DailyJSONWriter w(context, data.fact_compile ? "plan_json_utf8_bytes" : "result_json_utf8_bytes", "fact_program");
	w.Append("{\"schema_version\":");
	w.Quoted(r.window ? (data.fact_compile ? "ossie-fact-windows-plan-v1" : "ossie-fact-windows-result-v1")
	                  : (data.fact_compile ? "ossie-fact-users-plan-v1" : "ossie-fact-users-result-v1"));
	FactIdentityJSON(w, b);
	w.Append(",\"request\":" + r.bytes + ",\"selected\":" + JSONStrings(r.selected) +
	         ",\"input_scope\":\"entire_capture\",\"columns\":[");
	for (idx_t c = 0; c < data.result_types.size(); c++) {
		if (c) {
			w.Append(",");
		}
		w.Append("{\"ordinal\":" + to_string(c + 1) + ",\"name\":");
		w.Quoted(data.result_names[c]);
		w.Field("bound_type", data.result_types[c].ToString());
		w.Field("encoding", ResultEncoding(data.result_types[c]));
		if (c >= (r.window ? 2 : 1)) {
			auto &name = r.selected[c - (r.window ? 2 : 1)];
			string unit;
			if (r.window) {
				for (auto &m : p.windows) {
					if (m.name == name) {
						unit = m.unit;
					}
				}
			} else {
				for (auto &m : p.metrics) {
					if (m.name == name) {
						unit = m.unit;
					}
				}
			}
			w.Field("unit", unit);
		}
		w.Append("}");
	}
	w.Append("],\"paging\":false,\"truncated\":false,\"json_byte_limit\":1048576,\"ordering\":");
	w.Quoted(r.window ? "raw User UTF8 bytes ASC then DATE chronological ASC" : "raw User UTF8 bytes ASC");
	if (r.window) {
		auto &s = p.sources[r.window_source];
		w.Field("dataset", s.dataset);
		w.Field("source_concept", s.concept);
		w.Field("source_captured_rows", to_string(b.fact_children[r.window_source]->rows));
		w.Append(",\"source_identity_fields\":" + JSONStrings(s.identity_fields) +
		         ",\"output_identity_fields\":[\"user_id\",\"ds\"],\"display_stage\":\"after whole capture window "
		         "computation\"");
	} else {
		w.Append(",\"domain\":\"common\",\"domain_semantics\":\"row presence in both facts before "
		         "cohort\",\"cohort_semantics\":\"TRUE member sets; difference excludes RHS TRUE only\"");
	}
	if (data.fact_compile) {
		w.Append(",\"executed\":false,\"members_computed\":false,\"counts_computed\":false,\"physical_plan\":false,"
		         "\"sql_replayable\":false");
		w.Field("projection_sql", data.statement->ToString());
		if (!r.window) {
			w.Field("common_count_sql", data.fact_domain_statement->ToString());
		}
		w.Append(",\"operators\":");
		w.Append(r.window
		             ? "[\"frozen single-source User/date projection\",\"whole capture window computation\",\"display "
		               "DATE filtering\",\"raw identity sorting\",\"complete bounded encoding\"]}"
		             : "[\"separate source User aggregation\",\"row-presence common domain\",\"same-unit metric "
		               "combination\",\"TRUE cohort set filtering\",\"independent common domain count\",\"raw User "
		               "sorting\",\"complete bounded encoding\"]}");
		return w.Finish();
	}
	vector<vector<Value>> rows;
	idx_t common = 0;
	ObserveFact(context, PopulationFactCheckpoint::BEFORE_EXECUTE);
	{
		lock_guard<mutex> guard(b.query_lock);
		auto engine = Engine(b);
		if (!r.window) {
			auto count = Run(context, *engine, data.fact_domain_statement->Copy());
			auto &material = count->Cast<MaterializedQueryResult>();
			if (count->types != vector<LogicalType> {LogicalType::BIGINT} || material.RowCount() != 1 ||
			    material.GetValue(0, 0).IsNull()) {
				Fail("common_user_count", "execute", "one nonNULL BIGINT count required");
			}
			auto value = material.GetValue(0, 0).GetValue<int64_t>();
			if (value < 0 || value > int64_t(MAX_ROWS)) {
				Fail("common_user_count", "execute", "common domain count outside bounded source");
			}
			common = idx_t(value);
			ObserveFact(context, PopulationFactCheckpoint::AFTER_DOMAIN);
		}
		auto query = Run(context, *engine, data.statement->Copy());
		if (query->types != data.result_types || query->names != data.result_names) {
			Fail("fact_query", "execute", "actual typed schema changed");
		}
		while (auto chunk = query->Fetch()) {
			for (idx_t row = 0; row < chunk->size(); row++) {
				if (context.IsInterrupted()) {
					throw InterruptException();
				}
				if (rows.size() >= MAX_ROWS) {
					Fail("fact_query", "resource", "complete rows exceed10000; no truncation");
				}
				vector<Value> values;
				for (idx_t c = 0; c < data.result_types.size(); c++) {
					values.push_back(chunk->GetValue(c, row));
				}
				if (values[0].IsNull() || StringValue::Get(values[0]).empty() ||
				    (r.window && (values[1].IsNull() || !Date::IsFinite(values[1].GetValue<date_t>())))) {
					Fail("fact_query", "execute", "nonempty User and finite DATE identities required");
				}
				rows.push_back(std::move(values));
			}
		}
		if (query->HasError()) {
			NormalizeError(query->GetErrorObject(), "fact_query", "execute", ExceptionType::INVALID_INPUT);
		}
	}
	ObserveFact(context, PopulationFactCheckpoint::BEFORE_SORT);
	RawByteLess less;
	std::sort(rows.begin(), rows.end(), [&](const vector<Value> &a, const vector<Value> &z) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		auto &left = StringValue::Get(a[0]);
		auto &right = StringValue::Get(z[0]);
		if (left != right) {
			return less(left, right);
		}
		return r.window && a[1].GetValue<date_t>().days < z[1].GetValue<date_t>().days;
	});
	for (idx_t i = 1; i < rows.size(); i++) {
		if (StringValue::Get(rows[i - 1][0]) == StringValue::Get(rows[i][0]) &&
		    (!r.window || rows[i - 1][1].GetValue<date_t>() == rows[i][1].GetValue<date_t>())) {
			Fail("fact_query", "execute", "duplicate output identity");
		}
	}
	if (!r.window && rows.size() > common) {
		Fail("fact_query", "execute", "selected Users exceed common domain");
	}
	ObserveFact(context, PopulationFactCheckpoint::BEFORE_ENCODE);
	w.Append(",\"executed\":true,\"members_computed\":true,\"counts_computed\":true");
	w.Field("row_count", to_string(rows.size()));
	if (!r.window) {
		w.Field("common_user_count", to_string(common));
		w.Field("selected_user_count", to_string(rows.size()));
	}
	w.Append(",\"rows\":[");
	for (idx_t i = 0; i < rows.size(); i++) {
		if (i) {
			w.Append(",");
		}
		w.Append("[");
		for (idx_t c = 0; c < rows[i].size(); c++) {
			if (c) {
				w.Append(",");
			}
			auto &value = rows[i][c];
			if (value.IsNull()) {
				w.Append("null");
			} else {
				w.Quoted(value.type() == LogicalType::VARCHAR ? StringValue::Get(value) : value.ToString());
			}
		}
		w.Append("]");
	}
	w.Append("]}");
	return w.Finish();
}

void ObserveHistory(ClientContext &context, PopulationHistoryCheckpoint point) {
	auto observer = context.registered_state->Get<PopulationHistoryObserver>(PopulationHistoryObserver::KEY);
	if (observer) {
		observer->Observe(point);
	}
	if (context.IsInterrupted()) {
		throw InterruptException();
	}
}
unique_ptr<FunctionData> HistoryBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                     vector<string> &names) {
	auto data = make_uniq<PopulationBind>();
	data->bundle = Current(context);
	auto &b = *data->bundle;
	if (b.profile != HISTORY_PROFILE) {
		Fail("history", "capability", "historical-observation profile required");
	}
	if (input.inputs[0].IsNull() || input.inputs[1].IsNull()) {
		Fail("history", "arguments", "relationship and request_json required");
	}
	if (input.inputs[0].GetValue<string>() != b.history_relationship) {
		Fail("history", "arguments", "exact declared historical relationship required");
	}
	data->history_query = true;
	data->history_compile = input.table_function.name == "ossie_history_links_compile";
	data->history_request = ParseHistoryRequest(input.inputs[1].GetValue<string>());
	auto &child = *b.history_children[0];
	auto &src = b.history_sources[0];
	auto select = make_uniq<SelectNode>();
	for (auto &field : src.fields) {
		select->select_list.push_back(make_uniq<ColumnRefExpression>(
		    FactPhysicalField(*b.model.FindDataset(src.dataset), *b.model.FindDataset(src.dataset)->FindField(field)),
		    src.dataset));
	}
	select->from_table = SnapshotTable(child);
	select->from_table->external_dependency->AddDependency("ossie_program_parent",
	                                                       make_shared_ptr<SnapshotDependency>(data->bundle));
	data->statement = make_uniq<SelectStatement>();
	data->statement->node = std::move(select);
	lock_guard<mutex> guard(b.query_lock);
	auto engine = Engine(b);
	auto prepared = engine->Prepare(data->statement->Copy());
	if (prepared->HasError()) {
		NormalizeError(prepared->GetErrorObject(), "history", "bind", ExceptionType::INVALID_INPUT);
	}
	data->result_types = prepared->GetTypes();
	if (data->result_types != vector<LogicalType> {LogicalType::VARCHAR, LogicalType::DATE, LogicalType::VARCHAR}) {
		Fail("history", "bind", "actual projection must be User/DATE/product VARCHAR/DATE/VARCHAR");
	}
	types = {LogicalType::VARCHAR};
	names = {data->history_compile ? "plan_json" : "result_json"};
	return std::move(data);
}
void HistoryMetadata(DailyJSONWriter &json, const Bundle &b) {
	json.Field("api_version", "1.0.0");
	json.Field("profile", b.profile);
	json.Field("operator", "history_at_observation");
	json.Field("root_concept", b.root);
	json.Field("relationship", b.history_relationship);
	json.Field("user_concept", b.user_concept);
	json.Field("target_concept", b.history_target);
	json.Field("target_identity_relationship", "id");
	json.Field("target_identity_bound_type", "VARCHAR");
	for (auto &f :
	     {std::make_pair("snapshot_id", b.snapshot_id), std::make_pair("content_sha256", b.content_hash),
	      std::make_pair("core_sha256", b.core_hash), std::make_pair("ontology_sha256", b.ontology_hash),
	      std::make_pair("mapping_sha256", b.mapping_hash), std::make_pair("manifest_sha256", b.manifest_hash)}) {
		json.Field(f.first, f.second);
	}
	json.Field("observation_rows", to_string(b.inputs[0].rows));
	json.Field("history_record_count", to_string(b.history_index->source_rows));
	json.Field("index_segment_count", to_string(b.history_index->segment_count));
	json.Append(",\"root_identity_relationships\":[\"user\",\"date\",\"product\"],\"root_identity_fields\":" +
	            JSONStrings(b.history_sources[0].fields));
	json.Append(",\"link_role_concepts\":" + JSONStrings({b.user_concept, "Date", "String", b.history_target}));
	json.Field("validity", "finite DATE start<=observation_date<end; gaps produce no link");
	json.Field("scope_semantics", "request filters captured observations only; full history validated at load; all "
	                              "observations without qualification rule");
	json.Field("source_identity", "record_id identifies source witnesses only, not output links");
}
void HistoryScope(DailyJSONWriter &json, const PopulationBind &data) {
	HistoryMetadata(json, *data.bundle);
	auto &r = data.history_request;
	json.Append(",\"request\":{\"dates\":{\"mode\":");
	json.Quoted(r.all_dates ? "all" : "range");
	if (!r.all_dates) {
		json.Field("start_date", r.start_text);
		json.Field("end_date", r.end_text);
	}
	json.Append("},\"products\":{\"mode\":");
	json.Quoted(r.all_products ? "all" : "subset");
	if (!r.all_products) {
		json.Append(",\"values\":[");
		idx_t n = 0;
		for (auto &p : r.products) {
			if (n++) {
				json.Append(",");
			}
			json.Quoted(p);
		}
		json.Append("]");
	}
	json.Append("}}");
}
string HistoryQueryJSON(ClientContext &context, const PopulationBind &data) {
	auto &b = *data.bundle;
	auto &r = data.history_request;
	DailyJSONWriter json(context, data.history_compile ? "plan_json_utf8_bytes" : "result_json_utf8_bytes", "history");
	json.Append(data.history_compile ? "{\"schema_version\":\"ossie-history-links-plan-v1\",\"executed\":false"
	                                 : "{\"schema_version\":\"ossie-history-links-result-v1\",\"executed\":true");
	HistoryScope(json, data);
	if (data.history_compile) {
		json.Append(",\"links_computed\":false,\"physical_plan\":false,\"sql_replayable\":false,\"input_projection_"
		            "binding\":true,\"input_types\":[\"VARCHAR\",\"DATE\",\"VARCHAR\"],\"projection_sql\":");
		json.Quoted(data.statement->ToString());
		json.Append(
		    ",\"operators\":[\"frozen full observation projection\",\"explicit observation date/product "
		    "scope\",\"immutable per-User interval binary lookup\",\"start inclusive and end exclusive; gap no "
		    "link\",\"complete Observation to typed target set\",\"raw User/product/target bytes and DATE value "
		    "ordering\",\"complete bounded "
		    "JSON\"],\"limits\":{\"links\":10000,\"request_utf8_bytes\":16384,\"complete_json_utf8_bytes\":1048576},"
		    "\"limitations\":[\"Compile binds projection but never scans or matches links\",\"Input and index counts "
		    "are load metadata\",\"Rendered projection excludes C++ scope/index matching and cannot replay "
		    "independently\",\"Compile does not guarantee execution completion or encoded result size\"]}");
		return json.Finish();
	}
	struct Link {
		string user, product;
		const string *target;
		date_t date;
	};
	vector<Link> links;
	idx_t scanned = 0;
	ObserveHistory(context, PopulationHistoryCheckpoint::BEFORE_EXECUTE);
	{
		lock_guard<mutex> guard(b.query_lock);
		auto engine = Engine(b);
		auto query = Run(context, *engine, data.statement->Copy());
		if (query->types != data.result_types) {
			Fail("history", "execute", "projection types changed");
		}
		while (auto chunk = query->Fetch()) {
			for (idx_t row = 0; row < chunk->size(); row++) {
				if (context.IsInterrupted()) {
					throw InterruptException();
				}
				if (++scanned > b.inputs[0].rows) {
					Fail("history", "execute", "projection exceeds captured observation count");
				}
				auto user = chunk->GetValue(0, row), date = chunk->GetValue(1, row), product = chunk->GetValue(2, row);
				if (user.IsNull() || date.IsNull() || product.IsNull()) {
					Fail("history", "execute", "frozen identity changed to NULL");
				}
				auto day = date.GetValue<date_t>();
				auto item = product.GetValue<string>();
				if ((!r.all_dates && (day < r.start || day > r.end)) || (!r.all_products && !r.products.count(item))) {
					continue;
				}
				auto id = user.GetValue<string>();
				auto target = b.history_index->Find(id, day, [&] {
					if (context.IsInterrupted()) {
						throw InterruptException();
					}
				});
				if (target) {
					if (links.size() >= MAX_ROWS) {
						Fail("history.links", "resource", "complete link count exceeds10000");
					}
					links.push_back({id, item, target, day});
				}
			}
		}
		if (query->HasError()) {
			NormalizeError(query->GetErrorObject(), "history", "execute", ExceptionType::INVALID_INPUT);
		}
	}
	if (scanned != b.inputs[0].rows) {
		Fail("history", "execute", "full observation scan differs from frozen count");
	}
	ObserveHistory(context, PopulationHistoryCheckpoint::BEFORE_SORT);
	HistoryByteLess less;
	std::sort(links.begin(), links.end(), [&](const Link &a, const Link &c) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		if (a.user != c.user) {
			return less(a.user, c.user);
		}
		if (a.date != c.date) {
			return a.date < c.date;
		}
		if (a.product != c.product) {
			return less(a.product, c.product);
		}
		return less(*a.target, *c.target);
	});
	ObserveHistory(context, PopulationHistoryCheckpoint::BEFORE_ENCODE);
	json.Field("link_count", to_string(links.size()));
	json.Append(",\"complete\":true,\"paging\":false,\"truncated\":false,\"link_columns\":[\"user_id\",\"date\","
	            "\"product\",\"target_id\"],\"link_types\":[\"VARCHAR\",\"DATE\",\"VARCHAR\",\"VARCHAR\"],\"link_"
	            "encodings\":[\"raw-UTF8\",\"finite-date-text\",\"raw-UTF8\",\"raw-UTF8\"],\"ordering\":\"raw User "
	            "bytes, DATE value, raw product bytes, raw target bytes ascending\",\"links\":[");
	for (idx_t i = 0; i < links.size(); i++) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		if (i) {
			json.Append(",");
		}
		auto &l = links[i];
		json.Append("[");
		json.Quoted(l.user);
		json.Append(",");
		json.Quoted(Date::ToString(l.date));
		json.Append(",");
		json.Quoted(l.product);
		json.Append(",");
		json.Quoted(*l.target);
		json.Append("]");
	}
	json.Append("]}");
	ObserveHistory(context, PopulationHistoryCheckpoint::BEFORE_FINISH);
	return json.Finish();
}

string HistoryProgramJSON(ClientContext &context, const Bundle &b) {
	DailyJSONWriter json(context, "program_json_utf8_bytes", "history");
	json.Append("{\"schema_version\":\"ossie-history-program-v1\",\"api_version\":\"1.0.0\",\"query_executed\":false,"
	            "\"query_supported\":true,\"compile_supported\":true,\"mcp_registered\":false");
	json.Field("profile", b.profile);
	json.Field("operator", "history_at_observation");
	json.Field("root_concept", b.root);
	json.Field("relationship", b.history_relationship);
	json.Field("user_concept", b.user_concept);
	json.Field("target_concept", b.history_target);
	json.Field("target_identity_relationship", "id");
	for (auto &f :
	     {std::make_pair("snapshot_id", b.snapshot_id), std::make_pair("content_sha256", b.content_hash),
	      std::make_pair("core_sha256", b.core_hash), std::make_pair("ontology_sha256", b.ontology_hash),
	      std::make_pair("mapping_sha256", b.mapping_hash), std::make_pair("manifest_sha256", b.manifest_hash)}) {
		json.Field(f.first, f.second);
	}
	json.Field("total_source_rows", to_string(b.rows));
	json.Field("history_record_count", to_string(b.history_index->source_rows));
	json.Field("index_segment_count", to_string(b.history_index->segment_count));
	json.Field("history_User_count", to_string(b.history_index->users.size()));
	json.Append(",\"functions\":[\"ossie_history_program\",\"ossie_history_links_json\",\"ossie_history_links_"
	            "compile\"],\"query_limits\":{\"links\":10000,\"request_utf8_bytes\":16384,\"subset_products\":64,"
	            "\"product_utf8_bytes\":256,\"complete_json_utf8_bytes\":1048576},\"request_dates\":\"explicit all or "
	            "inclusive finite canonical DuckDB DATE range; no calendar expansion\",\"request_products\":\"explicit "
	            "all or nonempty raw subset\",\"scope_semantics\":\"filters observations only, all without "
	            "qualification; full history checked at load\"");
	json.Append(",\"sources\":[");
	for (idx_t s = 0; s < 2; s++) {
		if (s) {
			json.Append(",");
		}
		auto &src = b.history_sources[s];
		auto &child = *b.history_children[s];
		json.Append("{");
		json.Quoted("dataset");
		json.Append(":");
		json.Quoted(src.dataset);
		json.Field("concept", src.concept);
		json.Field("source", b.inputs[s].source);
		json.Field("content_sha256", b.inputs[s].sha256);
		json.Field("rows", to_string(b.inputs[s].rows));
		json.Append(",\"roles\":[");
		for (idx_t i = 0; i < src.roles.size(); i++) {
			if (i) {
				json.Append(",");
			}
			json.Append("{\"role\":");
			json.Quoted(src.roles[i]);
			json.Field("field", src.fields[i]);
			auto physical = FactPhysicalField(*b.model.FindDataset(src.dataset),
			                                  *b.model.FindDataset(src.dataset)->FindField(src.fields[i]));
			json.Field("physical_column", physical);
			idx_t column = child.physical_names.size();
			for (idx_t n = 0; n < child.physical_names.size(); n++) {
				if (StringUtil::CIEquals(child.physical_names[n], physical)) {
					column = n;
				}
			}
			if (column == child.physical_names.size()) {
				Fail(physical, "bind", "missing physical role");
			}
			json.Field("bound_type", child.collection->Types()[column].ToString());
			json.Append("}");
		}
		json.Append("]}");
	}
	json.Append("]");
	json.Append(
	    ",\"limits\":{\"rows_per_source\":10000,\"combined_source_rows\":20000,\"physical_columns_per_source\":256,"
	    "\"combined_canonical_bytes\":16777216,\"index_segments\":10000,\"complete_json_utf8_bytes\":1048576},"
	    "\"validity\":\"finite DuckDB DATE [start,end), start<end\",\"conflict\":\"whole-history same User "
	    "different-target overlap rejects entire load\",\"source_identity\":\"global raw record_id, duplicates "
	    "rejected\",\"same_target_overlap\":\"distinct sources retained; derived coverage merged without "
	    "gaps\",\"link_identity\":[\"User\",\"DATE\",\"product\",\"target_id\"],\"load_metadata_not_query_counts\":"
	    "true}");
	return json.Finish();
}

unique_ptr<GlobalTableFunctionState> Init(ClientContext &context, TableFunctionInitInput &input) {
	auto result = make_uniq<PopulationScan>();
	auto &data = input.bind_data->Cast<PopulationBind>();
	if (data.load) {
		auto bundle = ErrorBoundary(
		    data.path, "load", [&]() { return LoadBundle(context, data.path); }, ExceptionType::INVALID_INPUT);
		result->rows.push_back({Value(bundle->profile), Value(bundle->snapshot_id), Value(bundle->content_hash),
		                        Value::BIGINT(static_cast<int64_t>(bundle->rows))});
		ObserveLoad(context, PopulationLoadCheckpoint::BEFORE_PUBLISH);
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		PopulationState::Get(context).SetBundle(std::move(bundle));
	} else if (data.history_query) {
		result->rows.push_back({Value(HistoryQueryJSON(context, data))});
	} else if (data.history_discovery) {
		result->rows.push_back({Value(HistoryProgramJSON(context, *data.bundle))});
	} else if (data.fact_discovery) {
		result->rows.push_back({Value(FactProgramJSON(context, data))});
	} else if (data.fact_query) {
		result->rows.push_back({Value(FactQueryJSON(context, data))});
	} else if (data.relation_objects) {
		result->rows.push_back({Value(RelationJSON(context, data))});
	} else if (data.relation_discovery) {
		auto &b = *data.bundle;
		auto &program = *b.relation_program;
		vector<string> identity_types;
		for (auto &field : b.observation_identity) {
			identity_types.push_back(b.field_types.at(field));
		}
		string json =
		    "{\"schema_version\":\"ossie-relation-program-v1\",\"profile\":" + JSONString(b.profile) +
		    ",\"snapshot_id\":" + JSONString(b.snapshot_id) + ",\"content_sha256\":" + JSONString(b.content_hash) +
		    ",\"core_sha256\":" + JSONString(b.core_hash) + ",\"ontology_sha256\":" + JSONString(b.ontology_hash) +
		    ",\"mapping_sha256\":" + JSONString(b.mapping_hash) +
		    ",\"manifest_sha256\":" + JSONString(b.manifest_hash) + ",\"root\":" + JSONString(program.root) +
		    ",\"target_concept\":" + JSONString(program.target_concept) +
		    ",\"mapped_user_relation\":" + JSONString(program.mapped_user_relation) +
		    ",\"root_identity_fields\":" + JSONStrings(b.observation_identity) +
		    ",\"root_identity_bound_types\":" + JSONStrings(identity_types) +
		    ",\"captured_rows\":" + JSONString(to_string(b.rows)) +
		    ",\"source_types_bound\":true,\"load_constraints_validated\":true,\"query_executed\":false,"
		    "\"root_constraints\":" +
		    JSONStrings(b.relation_input.requires) + ",\"relations\":[";
		for (idx_t i = 0; i < program.relations.size(); i++) {
			auto &relation = program.relations[i];
			if (i) {
				json += ",";
			}
			vector<string> constraints;
			for (auto &constraint : relation.constraints) {
				constraints.push_back(constraint->ToString());
			}
			json += "{\"name\":" + JSONString(relation.name) +
			        ",\"target_concept\":" + JSONString(relation.target_concept) +
			        ",\"dependencies\":" + JSONStrings(relation.dependencies) +
			        ",\"dependency_depth\":" + to_string(relation.dependency_depth) +
			        ",\"derived_by\":" + JSONStrings(relation.rules) +
			        ",\"bound_predicate\":" + JSONString(relation.predicate->ToString()) +
			        ",\"link_constraints\":" + JSONStrings(constraints) + "}";
		}
		json += "],\"objects_queryable\":true,\"mcp_registered\":false,\"constraint_scope\":\"entire root or actual "
		        "complete relation links before atomic publish\","
		        "\"predicate_sql_replayable\":false,\"json_byte_limit\":1048576}";
		if (json.size() > DAILY_RESULT_BYTES) {
			Fail("relation_program", "resource", "complete program JSON exceeds 1MiB");
		}
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		result->rows.push_back({Value(json)});
	} else if (data.daily_state_discovery) {
		result->rows.push_back({Value(DailyStateProgram(context, *data.bundle))});
	} else if (!data.vocabulary.empty()) {
		VocabularyRows(data, result->rows);
	} else if (data.discover) {
		auto &b = *data.bundle;
		vector<string> concepts = data.discover_v2 && b.profile == OBSERVATION_PROFILE
		                              ? vector<string> {b.user_concept, b.root, b.derived}
		                              : vector<string> {b.root, b.derived};
		for (const auto &concept : concepts) {
			bool derived = concept == b.derived;
			if (data.discover_v2) {
				bool user = concept == b.user_concept;
				result->rows.push_back(
				    {Value(concept), derived ? Value(b.root) : Value(LogicalType::VARCHAR),
				     derived ? Value(b.original_rule) : Value(LogicalType::VARCHAR), Value(IdentityComponents(b, user)),
				     b.profile == OBSERVATION_PROFILE && !user ? Value(ReferencedUserIdentity(b))
				                                               : Value(LogicalType::VARCHAR),
				     Value::BOOLEAN(!user), Value::BOOLEAN(!user), Value::BIGINT(static_cast<int64_t>(b.rows)),
				     Value::BIGINT(static_cast<int64_t>(b.profile == OBSERVATION_PROFILE ? b.user_count : b.rows)),
				     Value(b.profile), Value(b.original_source), Value(b.snapshot_id), Value(b.content_hash),
				     Value(b.core_hash), Value(b.ontology_hash), Value(b.mapping_hash), Value(b.manifest_hash)});
				continue;
			}
			result->rows.push_back({Value(concept), derived ? Value(b.root) : Value(LogicalType::VARCHAR),
			                        Value(OriginalName(b, b.identity)),
			                        derived ? Value(b.original_rule) : Value(LogicalType::VARCHAR), Value(b.profile),
			                        Value(b.snapshot_id), Value(b.content_hash), Value(b.core_hash),
			                        Value(b.ontology_hash), Value(b.mapping_hash), Value(b.manifest_hash),
			                        Value(b.original_source), Value::BIGINT(static_cast<int64_t>(b.rows))});
		}
	} else if (data.daily_compile) {
		result->rows.push_back(
		    {Value(data.daily_states ? DailyStatePlan(context, data) : DailyPlanJSON(context, data))});
	} else if (data.compile) {
		auto &b = *data.bundle;
		result->rows.push_back({Value(data.sql), Value(b.profile), Value(b.snapshot_id), Value(b.content_hash),
		                        Value(b.core_hash), Value(b.ontology_hash), Value(b.mapping_hash),
		                        Value(b.manifest_hash), Value(b.execution_target), Value(data.plan_json),
		                        Value::BOOLEAN(false)});
	} else {
		lock_guard<mutex> guard(data.bundle->query_lock);
		auto engine = Engine(*data.bundle);
		if (data.daily_states) {
			ObserveDailyState(context, PopulationDailyStateCheckpoint::BEFORE_EXECUTE);
		} else if (data.daily) {
			ObserveDaily(context, PopulationDailyCheckpoint::BEFORE_EXECUTE);
		}
		result->result = Run(context, *engine, data.statement->Copy());
		if (data.daily || data.encoded) {
			result->rows.push_back({Value(data.daily_states ? DailyStateResult(context, data, *result->result)
			                              : data.daily      ? DailyResultJSON(context, data, *result->result)
			                                                : ResultJSON(context, data, *result->result))});
			result->result.reset();
		}
	}
	return std::move(result);
}

void Scan(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<PopulationScan>();
	if (state.result) {
		state.chunk = state.result->Fetch();
		if (state.chunk && state.chunk->size()) {
			output.Reference(*state.chunk);
		}
		return;
	}
	idx_t count = 0;
	while (state.offset < state.rows.size() && count < STANDARD_VECTOR_SIZE) {
		auto &row = state.rows[state.offset++];
		for (idx_t col = 0; col < row.size(); col++) {
			output.SetValue(col, count, row[col]);
		}
		count++;
	}
	output.SetCardinality(count);
}

unique_ptr<FunctionData> BindBoundary(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                      vector<string> &names) {
	auto name = input.table_function.name;
	string object = name;
	if (!input.inputs.empty() && !input.inputs[0].IsNull() && input.inputs[0].type() == LogicalType::VARCHAR) {
		object = input.inputs[0].GetValue<string>();
	}
	return ErrorBoundary(object, "bind", [&]() {
		if (name == "ossie_history_links_json" || name == "ossie_history_links_compile") {
			return HistoryBind(context, input, types, names);
		}
		if (name == "ossie_history_program") {
			auto result = make_uniq<PopulationBind>();
			result->bundle = Current(context);
			if (result->bundle->profile != HISTORY_PROFILE) {
				Fail(name, "capability", "historical-observation profile required");
			}
			result->history_discovery = true;
			types = {LogicalType::VARCHAR};
			names = {"program_json"};
			return unique_ptr<FunctionData>(std::move(result));
		}
		if (name == "ossie_fact_program" || name == "ossie_fact_users_json" || name == "ossie_fact_users_compile" ||
		    name == "ossie_fact_windows_json" || name == "ossie_fact_windows_compile") {
			return FactBind(context, input, types, names);
		}
		if (name == "ossie_relation_objects_json" || name == "ossie_relation_compile") {
			return RelationBind(context, input, types, names);
		}
		if (name == "ossie_relation_program") {
			auto result = make_uniq<PopulationBind>();
			result->bundle = Current(context);
			if (!result->bundle->relation_program) {
				Fail(name, "capability", "relation program profile required");
			}
			result->relation_discovery = true;
			types = {LogicalType::VARCHAR};
			names = {"program_json"};
			return unique_ptr<FunctionData>(std::move(result));
		}
		if (name == "ossie_daily_states_program") {
			auto result = make_uniq<PopulationBind>();
			result->bundle = Current(context);
			if (result->bundle->profile != OBSERVATION_PROFILE) {
				Fail(name, "capability", "daily-observation profile required");
			}
			result->daily_state_discovery = true;
			types = {LogicalType::VARCHAR};
			names = {"program_json"};
			return unique_ptr<FunctionData>(std::move(result));
		}
		if (name == "ossie_daily_users_json" || name == "ossie_daily_users_compile" ||
		    name == "ossie_daily_states_json" || name == "ossie_daily_states_compile") {
			return DailyBind(context, input, types, names);
		}
		if (name == "ossie_bundle_load") {
			return LoadBind(context, input, types, names);
		}
		if (name == "ossie_concepts" || name == "ossie_concepts_v2") {
			return DiscoverBind(context, input, types, names);
		}
		if (name == "ossie_population_metrics" || name == "ossie_population_fields" ||
		    name == "ossie_population_capabilities") {
			return VocabularyBind(context, input, types, names);
		}
		return RequestBind(context, input, types, names);
	});
}

unique_ptr<GlobalTableFunctionState> InitBoundary(ClientContext &context, TableFunctionInitInput &input) {
	auto &data = input.bind_data->Cast<PopulationBind>();
	return ErrorBoundary(data.load ? data.path : "query", data.load ? "load" : "execute",
	                     [&]() { return Init(context, input); });
}

void ScanBoundary(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	ErrorBoundary("query", "execute", [&]() { Scan(context, input, output); });
}

void JSONListFunction(DataChunk &args, ExpressionState &, Vector &result) {
	for (idx_t row = 0; row < args.size(); row++) {
		auto value = args.data[0].GetValue(row);
		if (value.IsNull()) {
			Fail("list", "arguments", "population MCP: list argument required");
		}
		auto bytes = value.GetValue<string>();
		auto doc = yyjson_read(bytes.data(), bytes.size(), 0);
		if (!doc) {
			Fail("list", "arguments", "population MCP: malformed JSON; JSON array of strings required");
		}
		std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> owner(doc, yyjson_doc_free);
		auto root = yyjson_doc_get_root(doc);
		if (!yyjson_is_arr(root)) {
			Fail("list", "arguments", "population MCP: JSON array of strings required");
		}
		vector<Value> values;
		size_t index, max;
		yyjson_val *item;
		yyjson_arr_foreach(root, index, max, item) {
			if (!yyjson_is_str(item)) {
				Fail("list[" + to_string(index) + "]", "arguments", "population MCP: JSON array of strings required");
			}
			values.push_back(Value(string(yyjson_get_str(item), yyjson_get_len(item))));
		}
		result.SetValue(row, Value::LIST(LogicalType::VARCHAR, std::move(values)));
	}
}

} // namespace

unique_ptr<SQLStatement> PopulationFactStatementForTesting(ClientContext &context, bool window) {
	auto bundle = Current(context);
	if (!bundle->fact_program) {
		Fail("native fact test", "capability", "fact program required");
	}
	vector<string> names;
	if (window) {
		for (auto &value : bundle->fact_program->windows) {
			names.push_back(value.name);
		}
		return CompileFactWindows(*bundle->fact_program, names, "entire_capture", Value(), Value(),
		                          FactSnapshotSources(*bundle));
	}
	for (auto &value : bundle->fact_program->metrics) {
		names.push_back(value.name);
	}
	return CompileFactUsers(*bundle->fact_program, names, "common", {"all", "", {}}, FactSnapshotSources(*bundle));
}
vector<std::function<bool()>> PopulationFactLifetimeForTesting(ClientContext &context) {
	auto bundle = Current(context);
	vector<std::function<bool()>> alive;
	auto append = [&](const weak_ptr<Bundle> &weak) {
		alive.push_back([weak] { return !weak.expired(); });
	};
	append(bundle->owner);
	for (auto &child : bundle->fact_children) {
		append(child->owner);
	}
	return alive;
}

void RegisterPopulationFunctions(ExtensionLoader &loader) {
	for (auto name : {"ossie_fact_users_json", "ossie_fact_users_compile", "ossie_fact_windows_json",
	                  "ossie_fact_windows_compile"}) {
		RegisterDescribe(
		    loader, TableFunction(name, {LogicalType::VARCHAR}, ScanBoundary, BindBoundary, InitBoundary),
		    "Dedicated frozen multi-fact-user-program-v1 request. Users use common row-presence domain, "
		    "separately reduced metrics and TRUE cohort sets; windows compute whole single-source "
		    "observations before display DATE filtering. Strict16KiB JSON, actual bound types, raw "
		    "User/DATE identity ordering, complete1MiB JSON without truncation. Compile functions bind only "
		    "and never execute/count; rendered SQL requires frozen AST dependencies.",
		    "SELECT * FROM " + string(name) +
		        (string(name).find("windows") != string::npos
		             ? "('{\"input_scope\":\"entire_capture\",\"windows\":[\"previous\",\"rolling\"],\"display\":{"
		               "\"mode\":\"all\"}}')"
		             : "('{\"domain\":\"common\",\"input_scope\":\"entire_capture\",\"metrics\":[\"balance\"],"
		               "\"cohort\":{\"operation\":\"all\"}}')"));
	}
	RegisterDescribe(
	    loader, TableFunction("ossie_fact_program", {}, ScanBoundary, BindBoundary, InitBoundary),
	    "Discover frozen two-fact identities, mapped actual types, original metric/window definitions, units, load "
	    "capture metadata and bounded support/refusal contracts. No query execution or common/selected count "
	    "computation; max1MiB complete JSON. Native declaration does not infer external MCP registration.",
	    "SELECT * FROM ossie_fact_program()");

	RegisterDescribe(
	    loader,
	    TableFunction("ossie_relation_objects_json", {LogicalType::VARCHAR}, ScanBoundary, BindBoundary, InitBoundary),
	    "Return complete typed source User/date/product plus target User links for one declared derived relationship "
	    "in observation-relation-program-v1. Entire captured scope, TRUE membership, deterministic raw identity and "
	    "DATE order; max10000 links/1MiB complete JSON, no truncation.",
	    "SELECT * FROM ossie_relation_objects_json('audited_user')");
	RegisterDescribe(
	    loader,
	    TableFunction("ossie_relation_compile", {LogicalType::VARCHAR}, ScanBoundary, BindBoundary, InitBoundary),
	    "Explain and bind the full relationship link projection without execution or member calculation. Same frozen "
	    "profile and predicate as relation objects, with actual output types, target identity, dependencies and "
	    "snapshot hashes; max1MiB complete JSON, no SQL replay or physical plan guarantee.",
	    "SELECT * FROM ossie_relation_compile('audited_user')");
	for (auto name : {"ossie_history_links_json", "ossie_history_links_compile"}) {
		RegisterDescribe(
		    loader,
		    TableFunction(name, {LogicalType::VARCHAR, LogicalType::VARCHAR}, ScanBoundary, BindBoundary, InitBoundary),
		    "Complete historical Observation-to-target links or nonexecuting logical plan. Explicit request_json dates "
		    "all/range finite canonical DATE and products all/subset; scope filters observations only, full history "
		    "validated at load. Half-open intervals and gaps, raw identity, no paging, 16KiB request/1MiB complete "
		    "JSON. Compile never scans/matches; compare request and six hashes. No arbitrary SQL or qualification "
		    "inference.",
		    string("SELECT * FROM ") + name +
		        "('team_at_observation','{\"dates\":{\"mode\":\"all\"},\"products\":{\"mode\":\"all\"}}')");
	}
	RegisterDescribe(
	    loader, TableFunction("ossie_history_program", {}, ScanBoundary, BindBoundary, InitBoundary),
	    "Discover frozen typed history capture/index metadata and accepted finite validity policy; "
	    "query_executed=false. Dedicated link/compile SQL are available; native declaration does not probe MCP; load "
	    "metadata is not query counts.",
	    "SELECT * FROM ossie_history_program()");
	RegisterDescribe(loader, TableFunction("ossie_relation_program", {}, ScanBoundary, BindBoundary, InitBoundary),
	                 "Discover bound nonrecursive relationship declarations, typed root/User, dependencies and "
	                 "full-population load constraint validation in observation-relation-program-v1. Frozen snapshot; "
	                 "no object query execution. Complete JSON at most1MiB.",
	                 "SELECT * FROM ossie_relation_program()");
	loader.RegisterFunction(
	    TableFunction("_ossie_snapshot_rows", {}, FrozenScanFunction, FrozenBindFunction, FrozenInit));
	RegisterDescribe(
	    loader,
	    TableFunction("ossie_daily_users_json", {LogicalType::VARCHAR, LogicalType::VARCHAR}, ScanBoundary,
	                  BindBoundary, InitBoundary),
	    "Return the complete raw User set with at least one qualifying observation on each explicitly "
	    "requested inclusive calendar DATE. Products may differ by day. One frozen bundle/current rule; "
	    "missing observations provide no witness. Strict JSON all/subset request, max366 days/64 products/"
	    "256 bytes per product/16KiB request/1MiB complete inner result; resource refusal never truncates.",
	    "SELECT * FROM ossie_daily_users_json('QualifiedObservation', "
	    "'{\"start_date\":\"2026-01-31\",\"end_date\":\"2026-02-01\",\"products\":{\"mode\":\"all\"}}')");
	RegisterDescribe(loader,
	                 TableFunction("ossie_daily_users_compile", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                               ScanBoundary, BindBoundary, InitBoundary),
	                 "Explain the same bound daily User set request without execution. Plan names qualified AST "
	                 "projection plus C++ date/product scope, distinct User/date and required calendar-day count, "
	                 "raw UTF-8 member sorting and complete bounded encoding. No computed members, physical plan "
	                 "or ordinary SQL replay guarantee; max1MiB complete inner plan JSON.",
	                 "SELECT * FROM ossie_daily_users_compile('QualifiedObservation', "
	                 "'{\"start_date\":\"2026-01-31\",\"end_date\":\"2026-02-01\",\"products\":{\"mode\":\"all\"}}')");
	for (auto name : {"ossie_daily_states_json", "ossie_daily_states_compile"}) {
		RegisterDescribe(
		    loader,
		    TableFunction(name, {LogicalType::VARCHAR, LogicalType::VARCHAR}, ScanBoundary, BindBoundary, InitBoundary),
		    "Evaluate the declared qualification rule as TRUE/FALSE/NULL over the full frozen observation "
		    "capture. Request captured_users and within_request_days explicitly; daily any TRUE, only "
		    "nonempty all FALSE, otherwise UNKNOWN. No carry; all adjacent comparisons including unchanged. "
		    "Compile binds without scan. Max366 days,10000 combined objects,16KiB request,1MiB complete JSON.",
		    "SELECT * FROM " + string(name) +
		        "('QualifiedObservation', '{\"start_date\":\"2026-01-31\","
		        "\"end_date\":\"2026-02-01\",\"products\":{\"mode\":\"all\"},\"user_domain\":\"captured_users\","
		        "\"transitions\":\"within_request_days\"}')");
	}
	RegisterDescribe(loader, TableFunction("ossie_daily_states_program", {}, ScanBoundary, BindBoundary, InitBoundary),
	                 "Discover dedicated daily states and within-request comparisons: frozen rule, full captured "
	                 "User domain, three-valued truth, explicit identities and resource/refusal boundaries. No query "
	                 "execution, no external MCP registration inference; max1MiB complete JSON.",
	                 "SELECT * FROM ossie_daily_states_program()");
	auto list = LogicalType::LIST(LogicalType::ANY);
	auto traced = [](TableFunction function) {
		function.named_parameters["include_trace"] = LogicalType::BOOLEAN;
		return function;
	};
	RegisterDescribe(
	    loader, TableFunction("ossie_bundle_load", {LogicalType::VARCHAR}, ScanBoundary, BindBoundary, InitBoundary),
	    "Load a hash-bound local core/ontology/mapping bundle for gaap-scalar-population-v1 or "
	    "fact-dimension-population-v1 or daily-observation-population-v1 or observation-relation-program-v1 or "
	    "multi-fact-user-program-v1. Captures "
	    "a "
	    "committed native sources into frozen typed batches (AST dependencies required) and "
	    "preflights entire-batch "
	    "identity before publishing. Requires autocommit, one dataset or an explicit INTEGER-key "
	    "INNER/reject pair, or two explicit independent same-unit facts. At most 10000 rows/256 columns per source and "
	    "16 MiB canonical values (combined for independent facts, including metadata). Explicit reload is required to "
	    "observe source changes.",
	    "CALL ossie_bundle_load('bundle.json')");
	RegisterDescribe(loader, TableFunction("ossie_concepts", {}, ScanBoundary, BindBoundary, InitBoundary),
	                 "Discover the two executable concepts in the current frozen batch, with parent, identity, rule, "
	                 "definition hashes, source and snapshot identity. Database-wide state is captured at binding.",
	                 "SELECT * FROM ossie_concepts()");
	RegisterDescribe(loader, TableFunction("ossie_concepts_v2", {}, ScanBoundary, BindBoundary, InitBoundary),
	                 "Discover structured typed identities, referenced entity identities, queryability and full-batch "
	                 "observation/User counts. Counts describe capture, not qualifying request scope. Supports all "
	                 "population profiles.",
	                 "SELECT * FROM ossie_concepts_v2()");
	for (auto name : {"ossie_population_metrics", "ossie_population_fields", "ossie_population_capabilities"}) {
		RegisterDescribe(
		    loader, TableFunction(name, {}, ScanBoundary, BindBoundary, InitBoundary),
		    "Discover the bound immutable population bundle vocabulary or project API capabilities. "
		    "Original names/definitions and actual load-preflight types retain snapshot hashes; "
		    "individual preflight does not guarantee arbitrary combined requests. Independent of core model state.",
		    "SELECT * FROM " + string(name) + "()");
	}
	RegisterDescribe(
	    loader,
	    traced(TableFunction("ossie_objects", {LogicalType::VARCHAR, list}, ScanBoundary, BindBoundary, InitBoundary)),
	    "Return every logical field for an executable concept in the bound immutable batch. Scalar/join profiles add "
	    "entity_id and record_id; observation profile preserves typed tuple columns. "
	    "Strict scalar filters can only narrow population. No implicit sorting, paging or truncation.",
	    "SELECT * FROM ossie_objects('ValidUser', [])");
	RegisterDescribe(loader,
	                 traced(TableFunction("ossie_concept_query", {LogicalType::VARCHAR, list, list, list}, ScanBoundary,
	                                      BindBoundary, InitBoundary)),
	                 "Execute bounded SUM/COUNT/AVG metrics for a concept, using the same frozen batch and population "
	                 "predicate as ossie_objects. NULL and sparse groups follow DuckDB SQL behavior.",
	                 "SELECT * FROM ossie_concept_query('ValidUser', ['mean_gaap'], ['dws_gaap_di.ds'], [])");
	for (bool objects : {true, false}) {
		auto arguments = objects ? vector<LogicalType> {LogicalType::VARCHAR, list}
		                         : vector<LogicalType> {LogicalType::VARCHAR, list, list, list};
		auto name = objects ? "ossie_objects_json" : "ossie_concept_query_json";
		RegisterDescribe(
		    loader, traced(TableFunction(name, arguments, ScanBoundary, BindBoundary, InitBoundary)),
		    "Execute the same frozen population query with a versioned typed JSON result. One result_json row carries "
		    "actual bound columns, snapshot/definition hashes and row arrays even when empty. Integers, Decimal and "
		    "approximate IEEE floats use strings, NULL stays null; no sorting, paging or truncation. Native APIs "
		    "retain "
		    "their types. This adapter materializes the complete response; input limits are not output/memory bounds.",
		    objects ? "SELECT * FROM ossie_objects_json('ValidUser', [])"
		            : "SELECT * FROM ossie_concept_query_json('ValidUser', ['mean_gaap'], [], [])");
	}
	RegisterDescribe(
	    loader,
	    traced(TableFunction("ossie_concept_compile", {LogicalType::VARCHAR, list, list, list}, ScanBoundary,
	                         BindBoundary, InitBoundary)),
	    "Compile a concept request without executing it, with diagnostic SQL targeting a frozen batch and pinned "
	    "definition/snapshot hashes and project-versioned plan_json from the generated AST; executed is false. "
	    "Empty metrics select the object query path. include_trace adds the execution trace projections. "
	    "A separate query after reload may bind a different snapshot.",
	    "SELECT * FROM ossie_concept_compile('ValidUser', ['mean_gaap'], [], [])");
	ScalarFunction strings("ossie_population_strings", {LogicalType::VARCHAR}, LogicalType::LIST(LogicalType::VARCHAR),
	                       JSONListFunction);
	strings.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
	CreateScalarFunctionInfo info(strings);
	info.descriptions.push_back(
	    Describe({"value"},
	             "Decode a JSON string array for the population MCP example. Rejects SQL/JSON NULL, malformed JSON and "
	             "non-string elements with the population error contract; no empty-list fallback.",
	             {"SELECT ossie_population_strings('[\"amount_sum\"]')"}));
	loader.RegisterFunction(std::move(info));
}

} // namespace ossie
} // namespace duckdb
