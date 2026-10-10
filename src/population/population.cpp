#include "ossie/compile.hpp"
#include "ossie/functions.hpp"
#include "ossie/describe_functions.hpp"
#include "ossie/parser.hpp"
#include "ossie/population_testing.hpp"
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
constexpr idx_t MAX_ROWS = 10000;
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

struct Bundle {
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
	if (bundle.profile == OBSERVATION_PROFILE) {
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
	shared_ptr<Bundle> bundle;
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
                           optional_ptr<Catalog> &catalog, optional_ptr<CatalogEntry> &previous) {
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
	Frame(capture.canonical, dataset.name);
	Frame(capture.canonical, dataset.source_bound);
	Frame(capture.canonical, to_string(count));
	for (idx_t c = 0; c < capture.types.size(); c++) {
		if (!Portable(capture.types[c]) ||
		    (capture.types[c].id() == LogicalTypeId::VARCHAR && !StringType::GetCollation(capture.types[c]).empty())) {
			Fail(capture.names[c], "snapshot", "unsupported physical column type or collation");
		}
		Frame(capture.canonical, capture.names[c]);
		Frame(capture.canonical, capture.types[c].ToString());
	}
	while (auto chunk = result->Fetch()) {
		for (idx_t r = 0; r < chunk->size(); r++) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			vector<Value> row;
			for (idx_t c = 0; c < chunk->ColumnCount(); c++) {
				auto value = chunk->GetValue(c, r);
				Frame(capture.canonical, value.ToSQLString());
				if (capture.canonical.size() > MAX_BYTES) {
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
		Empty(c, "requires", "concept");
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
		auto name = Text(r, "name"), target = relationship_target(r, "ManyToOne");
		if (!relation_names.insert(name).second || !targets.insert(target).second ||
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
	     bundle->profile == JOIN_PROFILE
	         ? std::initializer_list<const char *> {"profile", "core", "ontology", "mapping", "join"}
	         : std::initializer_list<const char *> {"profile", "core", "ontology", "mapping"},
	     "bundle manifest");
	if (bundle->profile != PROFILE && bundle->profile != JOIN_PROFILE && bundle->profile != OBSERVATION_PROFILE) {
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
	bundle->model = ParseModel(LooksLikeYaml(core_bytes) ? YamlToJson(core_bytes, "core") : core_bytes, RebindMap());
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
	if (bundle->profile == OBSERVATION_PROFILE) {
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
	if (bundle->profile == PROFILE || bundle->profile == OBSERVATION_PROFILE) {
		bundle->original_source = dataset.source_bound;
	}
	if (bundle->profile == PROFILE || bundle->profile == OBSERVATION_PROFILE) {
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
	if (bundle->profile == OBSERVATION_PROFILE) {
		ObservationIdentityPreflight(context, *bundle, *engine);
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
	result->bundle = Current(context);
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
constexpr const char *POPULATION_API_VERSION = "2.1.0";

unique_ptr<FunctionData> VocabularyBind(ClientContext &context, TableFunctionBindInput &input,
                                        vector<LogicalType> &types, vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = Current(context);
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
		capabilities += "}";
		append({Value(POPULATION_API_VERSION), Value("ossie-population-discovery-v1"),
		        Value(b.profile == OBSERVATION_PROFILE ? "ossie-population-plan-v2" : "ossie-population-plan-v1"),
		        Value(b.definition_model.spec_version), Value(capabilities)});
	}
}

unique_ptr<FunctionData> RequestBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                     vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = Current(context);
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
	} else if (data.compile) {
		auto &b = *data.bundle;
		result->rows.push_back({Value(data.sql), Value(b.profile), Value(b.snapshot_id), Value(b.content_hash),
		                        Value(b.core_hash), Value(b.ontology_hash), Value(b.mapping_hash),
		                        Value(b.manifest_hash), Value(b.execution_target), Value(data.plan_json),
		                        Value::BOOLEAN(false)});
	} else {
		lock_guard<mutex> guard(data.bundle->query_lock);
		auto engine = Engine(*data.bundle);
		result->result = Run(context, *engine, data.statement->Copy());
		if (data.encoded) {
			result->rows.push_back({Value(ResultJSON(context, data, *result->result))});
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

void RegisterPopulationFunctions(ExtensionLoader &loader) {
	loader.RegisterFunction(
	    TableFunction("_ossie_snapshot_rows", {}, FrozenScanFunction, FrozenBindFunction, FrozenInit));
	auto list = LogicalType::LIST(LogicalType::ANY);
	auto traced = [](TableFunction function) {
		function.named_parameters["include_trace"] = LogicalType::BOOLEAN;
		return function;
	};
	RegisterDescribe(
	    loader, TableFunction("ossie_bundle_load", {LogicalType::VARCHAR}, ScanBoundary, BindBoundary, InitBoundary),
	    "Load a hash-bound local core/ontology/mapping bundle for gaap-scalar-population-v1 or "
	    "fact-dimension-population-v1 or daily-observation-population-v1. Captures a "
	    "committed native table into a frozen typed observation batch (AST dependency required) and "
	    "preflights entire-batch "
	    "identity before publishing. Requires autocommit, one dataset or an explicit INTEGER-key "
	    "INNER/reject pair, at most 10000 rows/256 columns per batch and "
	    "16 MiB canonical values. Explicit reload is required to observe source changes.",
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
