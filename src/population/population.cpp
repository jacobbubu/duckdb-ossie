#include "ossie/compile.hpp"
#include "ossie/functions.hpp"
#include "ossie/describe_functions.hpp"
#include "ossie/parser.hpp"
#include "ossie/population_testing.hpp"
#include "ossie/relation_program.hpp"
#include "ossie/fact_program.hpp"
#include "ossie/dialect.hpp"
#include "ossie/yaml.hpp"
#include "ossie/ontology.hpp"
#include "population_internal.hpp"
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
#include "ossie/names.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/main/query_result.hpp"
#include "duckdb/main/prepared_statement.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/expression/case_expression.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/conjunction_expression.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
#include "duckdb/parser/expression/window_expression.hpp"
#include "duckdb/parser/expression/type_expression.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/qualified_name.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
#include "duckdb/parser/tableref/joinref.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/storage/object_cache.hpp"
#include "yyjson.hpp"
#include <atomic>
#include <cstdlib>
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
using duckdb_yyjson::yyjson_doc_mut_copy;
using duckdb_yyjson::yyjson_get_bool;
using duckdb_yyjson::yyjson_get_len;
using duckdb_yyjson::yyjson_get_str;
using duckdb_yyjson::yyjson_is_arr;
using duckdb_yyjson::yyjson_is_bool;
using duckdb_yyjson::yyjson_is_obj;
using duckdb_yyjson::yyjson_is_str;
using duckdb_yyjson::yyjson_mut_doc;
using duckdb_yyjson::yyjson_mut_doc_free;
using duckdb_yyjson::yyjson_mut_doc_get_root;
using duckdb_yyjson::yyjson_mut_obj_remove_key;
using duckdb_yyjson::yyjson_mut_write;
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
constexpr const char *CONCEPT_REQUIRES = "concept-scalar-requires-v1";
constexpr const char *STRING_JOIN = "native-string-join-v1";
constexpr const char *DIRECT_DERIVED = "direct-derived-concepts-v1";
constexpr const char *SINGLE_INHERITANCE = "bounded-single-inheritance-v1";
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
	auto result = connection.Submit(std::move(sql));
	if (result->HasError()) {
		NormalizeError(result->GetErrorObject(), "query", "bind", ExceptionType::INVALID_INPUT);
	}
	result->Materialize();
	while (true) {
		if (caller.IsInterrupted()) {
			connection.Interrupt();
			// Submit schedules tasks before the first ExecuteTask. The handle's destructor
			// does not drain them, and ClientContext skips Destroy during unwinding.
			// Finish cancellation while the nested context and task notifiers are alive.
			result->Close();
			connection.context->CancelTransaction();
			throw InterruptException();
		}
		auto state = result->ExecuteTask();
		if (state == QueryResultState::EXECUTION_ERROR || result->HasError()) {
			NormalizeError(result->GetErrorObject(), "query", "execute", ExceptionType::INVALID_INPUT);
		}
		if (state == QueryResultState::FINISHED) {
			break;
		}
		if (state == QueryResultState::BLOCKED || state == QueryResultState::READY) {
			result->WaitForTask();
		} else {
			std::this_thread::yield();
		}
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

struct ConceptConstraint {
	string owner, original, attribute, field, bound_type, null_policy, threshold_sql;
	idx_t index = 0;
	bool derived = false;
	unique_ptr<ParsedExpression> predicate;
};

struct DirectConcept {
	string name, parent, original_rule;
	idx_t depth = 1;
	unique_ptr<ParsedExpression> rule;
};

struct Bundle {
	bool direct_derived = false;
	bool single_inheritance = false;
	vector<DirectConcept> direct_concepts;
	bool string_join = false;
	string join_key_type;
	bool concept_requires = false;
	vector<ConceptConstraint> concept_constraints;
	vector<HistorySource> history_sources;
	vector<shared_ptr<Bundle>> history_children;
	shared_ptr<Bundle> history_segments;
	idx_t history_segment_count = 0, history_user_count = 0;
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

const DirectConcept *DirectChild(const Bundle &b, const string &name) {
	for (auto &child : b.direct_concepts) {
		if (child.name == name) {
			return &child;
		}
	}
	return nullptr;
}
vector<string> ConceptLineage(const Bundle &b, const string &name) {
	vector<string> result {name};
	while (result.back() != b.root) {
		auto child = DirectChild(b, result.back());
		if (!child) {
			Fail(result.back(), "bind", "unknown parent concept");
		}
		if (std::find(result.begin(), result.end(), child->parent) != result.end()) {
			Fail(name, "bind", "inheritance cycle prohibited");
		}
		result.push_back(child->parent);
		if (b.single_inheritance && result.size() > 4) {
			Fail(name, "resource", "at most three inheritance edges");
		}
	}
	std::reverse(result.begin(), result.end());
	return result;
}
bool AncestorAlias(const Bundle &b, const string &owner, const string &name) {
	if (!b.single_inheritance || owner.empty()) {
		return false;
	}
	for (auto &ancestor : ConceptLineage(b, owner)) {
		if (StringUtil::CIEquals(ancestor, name)) {
			return true;
		}
	}
	return false;
}
void LineageBudget(const ParsedExpression &e, idx_t depth, idx_t &nodes) {
	if (++nodes > 4096 || depth > 40) {
		Fail("lineage", "resource", "lineage predicate exceeds 4096 nodes or depth 40");
	}
	ParsedExpressionIterator::EnumerateChildren(
	    e, [&](const ParsedExpression &child) { LineageBudget(child, depth + 1, nodes); });
}
unique_ptr<ParsedExpression> EffectiveMembership(const Bundle &b, const string &name) {
	unique_ptr<ParsedExpression> result;
	for (auto &ancestor : ConceptLineage(b, name)) {
		if (ancestor == b.root) {
			continue;
		}
		auto rule = DirectChild(b, ancestor)->rule->Copy();
		if (result) {
			result =
			    make_uniq<ConjunctionExpression>(ExpressionType::CONJUNCTION_AND, std::move(result), std::move(rule));
		} else {
			result = std::move(rule);
		}
	}
	if (result) {
		idx_t nodes = 0;
		LineageBudget(*result, 0, nodes);
	}
	return result;
}
bool ConceptNamespace(const Bundle &b, const string &name) {
	if (StringUtil::CIEquals(name, b.root) || (!b.derived.empty() && StringUtil::CIEquals(name, b.derived))) {
		return true;
	}
	for (auto &child : b.direct_concepts) {
		if (StringUtil::CIEquals(name, child.name)) {
			return true;
		}
	}
	return false;
}
vector<string> ExecutableConcepts(const Bundle &b) {
	if (!b.direct_derived) {
		return b.profile == OBSERVATION_PROFILE ? vector<string> {b.user_concept, b.root, b.derived}
		                                        : vector<string> {b.root, b.derived};
	}
	vector<string> names {b.root};
	for (auto &child : b.direct_concepts) {
		names.push_back(child.name);
	}
	return names;
}
const char *PopulationPlanSchema(const Bundle &b) {
	return b.single_inheritance ? "ossie-population-plan-v4"
	       : b.direct_derived
	           ? "ossie-population-plan-v3"
	           : (b.profile == OBSERVATION_PROFILE ? "ossie-population-plan-v2" : "ossie-population-plan-v1");
}

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
	auto &qualified = function.GetQualifiedName();
	if (function.IsOperator() || !qualified.Catalog().empty() || !qualified.Schema().empty() ||
	    !StringUtil::CIEquals(function.FunctionName().GetIdentifierName(), "nullif") ||
	    function.GetArguments().size() != 2) {
		return false;
	}
	auto &zero = function.GetArguments()[1].GetExpression();
	if (zero.GetExpressionClass() != ExpressionClass::CONSTANT) {
		return false;
	}
	auto &literal = zero.Cast<ConstantExpression>().GetLiteral();
	return !literal.IsNull() && literal.IsNumeric() && literal.ToValue() == Value::INTEGER(0);
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
		auto &type = cast.TargetType();
		if (!type.GetCatalog().empty() || !type.GetSchema().empty()) {
			Fail(expression.ToString(), "rule", "qualified casts not supported");
		}
		auto id = UnboundType::TryDefaultBind(type).id();
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
		auto &qualified = function.GetQualifiedName();
		auto &arguments = function.GetArguments();
		auto name = StringUtil::Lower(function.FunctionName().GetIdentifierName());
		bool aggregate = name == "sum" || name == "count" || name == "avg";
		bool division = function.IsOperator() && name == "/";
		bool arithmetic = function.IsOperator() && (name == "+" || name == "-" || name == "*" || division);
		bool nullif = !function.IsOperator() && name == "nullif" && arguments.size() == 2;
		bool named = false;
		for (auto &argument : arguments) {
			named = named || argument.HasName();
		}
		if (!qualified.Catalog().empty() || !qualified.Schema().empty() || function.Filter() || named ||
		    (function.OrderBy() && !function.OrderBy()->orders.empty()) || function.ExportState() ||
		    (!arithmetic && !aggregate && !nullif) || (aggregate && (!aggregates || inside_aggregate)) ||
		    (!aggregate && function.Distinct()) || (aggregate && arguments.size() != 1)) {
			Fail(expression.ToString(), "rule", "unsupported function or aggregate in scalar population");
		}
		if (division && (arguments.size() != 2 || !ZeroProtectedDenominator(arguments[1].GetExpression()))) {
			Fail(expression.ToString(), "rule",
			     "division requires a direct NULLIF(expr, numeric zero constant) denominator");
		}
		for (auto &argument : arguments) {
			Strict(argument.GetExpression(), aggregates, inside_aggregate || aggregate, depth + 1, visited);
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
		auto name = StringUtil::Lower(expression.Cast<FunctionExpression>().FunctionName().GetIdentifierName());
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
		expressions = Parser::GetBuiltinParser().ParseExpressionList(sql);
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
		auto names = ColumnNames(expression->Cast<ColumnRefExpression>());
		if (names.size() > 2 || (names.size() == 2 && !StringUtil::CIEquals(names[0], dataset))) {
			Fail(expression->ToString(), "bind", "physical reference crosses dataset");
		}
		expression = make_uniq<ColumnRefExpression>(Identifier(names.back()), Identifier(dataset));
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
	auto names = ColumnNames(expression->Cast<ColumnRefExpression>());
	return LogicalName(bundle, names);
}

void Resolve(const Bundle &bundle, unique_ptr<ParsedExpression> &expression, bool ontology_only = false,
             const string &owner = "") {
	if (expression->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto names = ColumnNames(expression->Cast<ColumnRefExpression>());
		if (names.size() != 2) {
			Fail(expression->ToString(), "rule", "scalar references must be qualified");
		}
		string field;
		if (StringUtil::CIEquals(names[0], bundle.root) ||
		    (bundle.direct_derived
		         ? (!owner.empty() && (StringUtil::CIEquals(names[0], owner) || AncestorAlias(bundle, owner, names[0])))
		         : StringUtil::CIEquals(names[0], bundle.derived))) {
			for (auto &attribute : bundle.attributes) {
				if (StringUtil::CIEquals(names[1], attribute.name)) {
					field = attribute.field;
				}
			}
		} else if (bundle.direct_derived && ConceptNamespace(bundle, names[0])) {
			Fail(names[0], "rule", "other derived concept references prohibited; root or selected owner only");
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
	    *expression, [&](unique_ptr<ParsedExpression> &child) { Resolve(bundle, child, ontology_only, owner); });
}

void Empty(const vector<string> &values, const char *key, const string &object) {
	if (!values.empty()) {
		Fail(object, "capability", string("unsupported nonempty ") + key);
	}
}

void References(const Bundle &bundle, const Ontology &ontology, const Mapping &mapping, const string &reason) {
	if (mapping.ontology_name != ontology.name || mapping.model_name != bundle.model.name) {
		Fail("mapping", "bind", reason);
	}
}

//! The mapped value of an object or nested referent that must carry exactly one scalar identity referent.
ScalarReferent SingleReferent(const ObjectMapping &object, const string &owner, const string &reason) {
	if (!object.entity || object.referents.size() != 1 || object.referents[0].nested) {
		Fail(owner, "mapping", reason);
	}
	return {object.referents[0].relationship, object.referents[0].expression};
}

ScalarReferent SingleReferent(const ReferentMapping &referent, const string &owner, const string &reason) {
	if (!referent.nested || referent.referents.size() != 1) {
		Fail(owner, "mapping", reason);
	}
	return referent.referents[0];
}

string ObjectIdentity(const Bundle &bundle, const ObjectMapping &object, const string &identity_relationship) {
	if (!object.concept.empty() && object.concept != bundle.root) {
		Fail(bundle.root, "mapping", "root concept override");
	}
	auto referent = SingleReferent(object, bundle.root, "identity must have one referent");
	if (referent.relationship != identity_relationship) {
		Fail(bundle.root, "mapping", "identity relationship mismatch");
	}
	return LogicalField(bundle, referent.expression);
}

void Definitions(Bundle &bundle, const Ontology &ontology, const Mapping &mapping) {
	Empty(ontology.requires_list, "requires", "ontology");
	const Concept *parent = nullptr, *child = nullptr;
	vector<const Concept *> children;
	for (auto &component : ontology.concepts) {
		if (!bundle.concept_requires) {
			Empty(component.requires_list, "requires", "concept");
		} else {
			if (component.requires_list.size() > 8) {
				Fail(component.name, "resource", "at most eight concept requires");
			}
			for (idx_t i = 0; i < component.requires_list.size(); i++) {
				ConceptConstraint constraint;
				constraint.owner = component.name;
				constraint.original = component.requires_list[i];
				constraint.index = i;
				idx_t total_bytes = constraint.original.size();
				for (auto &prior : bundle.concept_constraints) {
					total_bytes += prior.original.size();
				}
				if (constraint.original.size() > 16384 || total_bytes > 131072) {
					Fail(constraint.owner, "resource", "concept requires exceed 16KiB each or 128KiB total");
				}
				if (bundle.direct_derived && bundle.concept_constraints.size() >= 16) {
					Fail(constraint.owner, "resource", "at most sixteen total concept requires");
				}
				bundle.concept_constraints.push_back(std::move(constraint));
			}
		}
		if (component.type != "EntityType") {
			Fail(component.name, "capability", "explicit value types are not supported in this profile");
		}
		if (component.extends.empty()) {
			if (parent) {
				Fail("ontology", "capability", "exactly one root entity required");
			}
			parent = &component;
		} else {
			if (bundle.direct_derived) {
				if (children.size() >= 8) {
					Fail("ontology", "resource", "at most eight direct derived concepts");
				}
				children.push_back(&component);
			} else {
				if (child) {
					Fail("ontology", "capability", "exactly one derived entity required");
				}
				child = &component;
			}
		}
	}
	if (bundle.direct_derived) {
		if (!parent || children.empty() || ontology.concepts.size() != children.size() + 1) {
			Fail("ontology", "capability", "one root and one to eight direct children required");
		}
		bundle.root = parent->name;
		case_insensitive_set_t names;
		names.insert(bundle.root);
		for (auto entry : children) {
			DirectConcept concept;
			concept.name = entry->name;
			if (!names.insert(concept.name).second) {
				Fail(concept.name, "bind", "duplicate concept name");
			}
			if (entry->extends.size() != 1 || (!bundle.single_inheritance && entry->extends[0] != bundle.root)) {
				Fail(concept.name, "capability", "single direct inheritance from root required");
			}
			concept.parent = entry->extends[0];
			Empty(entry->identify_by, "identify_by", concept.name);
			if (!entry->relationships.empty()) {
				Fail(concept.name, "capability", "unsupported nonempty relationships");
			}
			bundle.direct_concepts.push_back(std::move(concept));
		}
	} else {
		if (!parent || !child || ontology.concepts.size() != 2) {
			Fail("ontology", "capability", "profile requires one root and one derived entity");
		}
		bundle.root = parent->name;
		bundle.derived = child->name;
		if (StringUtil::CIEquals(bundle.root, bundle.derived)) {
			Fail(bundle.root, "bind", "duplicate concept name");
		}
		if (child->extends.size() != 1 || child->extends[0] != bundle.root) {
			Fail(bundle.derived, "capability", "single inheritance from root required");
		}
		Empty(child->identify_by, "identify_by", bundle.derived);
		if (!child->relationships.empty()) {
			Fail(bundle.derived, "capability", "unsupported nonempty relationships");
		}
	}
	if (bundle.single_inheritance) {
		for (auto &concept : bundle.direct_concepts) {
			concept.depth = ConceptLineage(bundle, concept.name).size() - 1;
		}
	}
	Empty(parent->derived_by, "derived_by", bundle.root);
	if (parent->identify_by.size() != 1) {
		Fail(bundle.root, "capability", "single identity relationship required");
	}
	auto id_relation = parent->identify_by[0];
	bundle.identity_relation = id_relation;
	case_insensitive_set_t relation_names;
	for (auto &relationship : parent->relationships) {
		Empty(relationship.requires_list, "requires", "relationship");
		Empty(relationship.derived_by, "derived_by", "relationship");
		auto &name = relationship.name;
		if (!relation_names.insert(name).second) {
			Fail(name, "bind", "duplicate relationship");
		}
		if (relationship.roles.size() != 1) {
			Fail(name, "capability", "only binary scalar relationships supported");
		}
		auto &target = relationship.roles[0];
		if (target != "String" && target != "Date" && target != "Decimal" && target != "Integer" && target != "Float" &&
		    target != "Boolean") {
			Fail(name, "capability", "unsupported target value type");
		}
		auto &multiplicity = relationship.multiplicity;
		if (multiplicity != "ManyToOne" && multiplicity != "OneToOne") {
			Fail(name, "capability", "scalar multiplicity required");
		}
		if (name == id_relation && (multiplicity != "OneToOne" || target != "String")) {
			Fail(name, "capability", "identity requires OneToOne String");
		}
		bundle.attributes.push_back({name, target, "", multiplicity == "OneToOne"});
	}
	References(bundle, ontology, mapping, "document reference name mismatch");
	if (mapping.concept_mappings.size() != 1) {
		Fail("mapping", "capability", "only root concept may have a mapping");
	}
	auto &root_mapping = mapping.concept_mappings[0];
	if (root_mapping.concept != bundle.root) {
		Fail("mapping", "capability", "derived concept cannot have independent population mapping");
	}
	if (root_mapping.object_mappings.size() != 1) {
		Fail(bundle.root, "mapping", "single object mapping required");
	}
	bundle.identity = ObjectIdentity(bundle, root_mapping.object_mappings[0], id_relation);
	if (root_mapping.link_mappings.size() != 1) {
		Fail(bundle.root, "mapping", "single link mapping tree required");
	}
	auto &tree = root_mapping.link_mappings[0];
	if (ObjectIdentity(bundle, tree.object, id_relation) != bundle.identity) {
		Fail(bundle.root, "mapping", "link identity override");
	}
	case_insensitive_set_t bound;
	for (auto &link : tree.children) {
		auto &name = link.relationship;
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
		auto &object = link.object;
		if (object.entity) {
			Fail(name, "mapping", "attribute object mapping must be a scalar expression");
		}
		if (object.concept != attribute->target) {
			Fail(name, "mapping", "target concept mismatch");
		}
		attribute->field = LogicalField(bundle, object.expression);
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
	    bundle.model.datasets[0].primary_key != vector<string> {ColumnNames(id->Cast<ColumnRefExpression>()).back()}) {
		Fail(bundle.root, "identity", "identity must be a plain declared single-column primary key");
	}
	if (bundle.direct_derived) {
		idx_t bytes = 0;
		for (idx_t i = 0; i < children.size(); i++) {
			auto &concept = bundle.direct_concepts[i];
			if (children[i]->derived_by.size() != 1) {
				Fail(concept.name, "rule", "exactly one scalar derivation required");
			}
			concept.original_rule = children[i]->derived_by[0];
			bytes += concept.original_rule.size();
			if (bytes > 131072) {
				Fail(concept.name, "resource", "direct derived rules exceed 128KiB total");
			}
			concept.rule = Parse(concept.original_rule);
			Strict(*concept.rule);
			Resolve(bundle, concept.rule, true, concept.name);
			Strict(*concept.rule);
		}
		if (bundle.single_inheritance) {
			std::stable_sort(bundle.direct_concepts.begin(), bundle.direct_concepts.end(),
			                 [](const DirectConcept &a, const DirectConcept &b) { return a.depth < b.depth; });
			for (auto &concept : bundle.direct_concepts) {
				EffectiveMembership(bundle, concept.name);
			}
		}
		return;
	}
	if (child->derived_by.size() != 1) {
		Fail(bundle.derived, "rule", "exactly one scalar derivation required");
	}
	bundle.original_rule = child->derived_by[0];
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
		auto names = ColumnNames(expression->Cast<ColumnRefExpression>());
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
                                            vector<Identifier> &names) {
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
	names = Identifiers(result->bundle->physical_names);
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
	table->function = make_uniq<FunctionExpression>(QualifiedName("system", "main", "_ossie_snapshot_rows"),
	                                                vector<unique_ptr<ParsedExpression>>());
	table->alias = Identifier(bundle.model.datasets[0].name);
	table->external_dependency = make_shared_ptr<ExternalDependency>();
	table->external_dependency->AddDependency("ossie_batch", make_shared_ptr<SnapshotDependency>(bundle.owner.lock()));
	return std::move(table);
}
void EngineFunctions(ParsedExpression &expression) {
	if (expression.GetExpressionClass() == ExpressionClass::FUNCTION) {
		auto &function = expression.Cast<FunctionExpression>();
		function.SetQualifiedName("system", "main", function.FunctionName());
	}
	if (expression.GetExpressionClass() == ExpressionClass::WINDOW) {
		auto &window = expression.Cast<WindowExpression>();
		window.SetQualifiedName("system", "main", window.FunctionName());
	}
	ParsedExpressionIterator::EnumerateChildren(expression, [&](ParsedExpression &child) { EngineFunctions(child); });
}
unique_ptr<SQLStatement> FrozenSQL(Bundle &bundle, const string &sql) {
	auto parser = Parser::GetBuiltinParser();
	parser.ParseQuery(sql);
	auto statement = std::move(parser.statements[0]);
	auto &select = statement->Cast<SelectStatement>().node->Cast<SelectNode>();
	select.from_table = SnapshotTable(bundle);
	for (auto &expression : select.select_list) {
		EngineFunctions(*expression);
	}
	return statement;
}

unique_ptr<TableRef> ProgramSources(unique_ptr<TableRef> ref, Bundle &parent,
                                    const vector<shared_ptr<Bundle>> &children) {
	switch (ref->type) {
	case TableReferenceType::BASE_TABLE: {
		auto &base = ref->Cast<BaseTableRef>();
		for (auto &child : children) {
			if (child->model.datasets[0].name == base.Table().GetIdentifierName()) {
				auto table = SnapshotTable(*child);
				table->external_dependency->AddDependency("ossie_program_parent",
				                                          make_shared_ptr<SnapshotDependency>(parent.owner.lock()));
				if (!base.alias.empty()) {
					table->alias = base.alias;
				}
				return std::move(table);
			}
		}
		Fail(base.Table().GetIdentifierName(), "bind", "unknown frozen program source");
	}
	case TableReferenceType::JOIN: {
		auto &join = ref->Cast<JoinRef>();
		join.left = ProgramSources(std::move(join.left), parent, children);
		join.right = ProgramSources(std::move(join.right), parent, children);
		return ref;
	}
	case TableReferenceType::SUBQUERY: {
		auto &node = *ref->Cast<SubqueryRef>().subquery->node;
		if (node.type == QueryNodeType::SELECT_NODE && node.Cast<SelectNode>().from_table) {
			auto &select = node.Cast<SelectNode>();
			select.from_table = ProgramSources(std::move(select.from_table), parent, children);
		}
		return ref;
	}
	default:
		return ref;
	}
}

// Program SQL names frozen children by dataset; every function pins to system.main.
unique_ptr<SelectStatement> ProgramSQL(Bundle &parent, const vector<shared_ptr<Bundle>> &children, const string &sql) {
	auto parser = Parser::GetBuiltinParser();
	parser.ParseQuery(sql);
	auto statement = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	auto &select = statement->node->Cast<SelectNode>();
	select.from_table = ProgramSources(std::move(select.from_table), parent, children);
	ParsedExpressionIterator::EnumerateQueryNodeChildren(
	    *statement->node, [&](unique_ptr<ParsedExpression> &expression) { EngineFunctions(*expression); });
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
		for (auto &check : expression.Cast<CaseExpression>().CaseChecks()) {
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

// Independent of business names and capture shape: Mapping has already resolved
// both supported frontends into one typed frozen population with provenance.
void BindConceptConstraint(Bundle &bundle, ConceptConstraint &constraint) {
	auto tree = Parse(constraint.original);
	Strict(*tree);
	if (tree->GetExpressionClass() != ExpressionClass::CONJUNCTION ||
	    (tree->GetExpressionType() != ExpressionType::CONJUNCTION_OR &&
	     tree->GetExpressionType() != ExpressionType::CONJUNCTION_AND)) {
		Fail(constraint.owner, "rule", "requires needs explicit same-attribute NULL guard");
	}
	auto &conjunction = tree->Cast<ConjunctionExpression>().GetChildren();
	if (conjunction.size() != 2) {
		Fail(constraint.owner, "rule", "requires accepts one guard and one comparison");
	}
	const ParsedExpression *guard = conjunction[0].get(), *comparison = conjunction[1].get();
	if (guard->GetExpressionClass() == ExpressionClass::COMPARISON) {
		std::swap(guard, comparison);
	}
	bool allow_null = tree->GetExpressionType() == ExpressionType::CONJUNCTION_OR;
	auto guard_type = allow_null ? ExpressionType::OPERATOR_IS_NULL : ExpressionType::OPERATOR_IS_NOT_NULL;
	if (guard->GetExpressionClass() != ExpressionClass::OPERATOR || guard->GetExpressionType() != guard_type ||
	    comparison->GetExpressionClass() != ExpressionClass::COMPARISON ||
	    comparison->GetExpressionType() != ExpressionType::COMPARE_GREATERTHANOREQUALTO) {
		Fail(constraint.owner, "rule", "requires accepts IS NULL OR >=, or IS NOT NULL AND >=");
	}
	auto &operation = guard->Cast<OperatorExpression>().GetChildren();
	auto &test = comparison->Cast<ComparisonExpression>();
	if (operation.size() != 1 || operation[0]->GetExpressionClass() != ExpressionClass::COLUMN_REF ||
	    test.Left().GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		Fail(constraint.owner, "rule", "requires needs direct mapped scalar references");
	}
	auto guard_names = ColumnNames(operation[0]->Cast<ColumnRefExpression>());
	auto names = ColumnNames(test.Left().Cast<ColumnRefExpression>());
	if (names.size() != 2 || guard_names.size() != 2 || !StringUtil::CIEquals(names[0], constraint.owner) ||
	    !StringUtil::CIEquals(guard_names[0], constraint.owner) || !StringUtil::CIEquals(guard_names[1], names[1])) {
		Fail(constraint.owner, "rule", "requires guard and comparison must name the same owner attribute");
	}
	const Attribute *attribute = nullptr;
	for (auto &candidate : bundle.attributes) {
		if (StringUtil::CIEquals(candidate.name, names[1])) {
			attribute = &candidate;
		}
	}
	if (!attribute) {
		Fail(constraint.owner, "bind", "unknown requires attribute");
	}
	if (attribute->target != "Integer" && attribute->target != "Date") {
		Fail(constraint.owner, "capability", "concept requires supports Integer/DATE only");
	}
	auto physical = FieldTree(bundle, attribute->field);
	if (physical->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		Fail(constraint.owner, "capability", "requires attribute must map to a direct physical column");
	}
	auto engine = Engine(bundle);
	auto prepared =
	    engine->Prepare(FrozenSQL(bundle, "SELECT " + physical->ToString() + " FROM main.ossie_snapshot AS " +
	                                          Quote(bundle.model.datasets[0].name)));
	if (prepared->HasError()) {
		NormalizeError(prepared->GetErrorObject(), constraint.owner, "bind", ExceptionType::INVALID_INPUT);
	}
	auto type = prepared->GetTypes()[0];
	Value threshold;
	if (type.IsIntegral() && attribute->target == "Integer" &&
	    test.Right().GetExpressionClass() == ExpressionClass::CONSTANT) {
		auto value = test.Right().Cast<ConstantExpression>().GetLiteral().ToValue();
		string error;
		auto fitted = value.IsNull() || !value.type().IsIntegral() ? optional<Value>()
		                                                           : value.DefaultTryCastAs(type, &error, true);
		if (!fitted) {
			Fail(constraint.owner, "bind", "requires threshold must fit the actual integer type without coercion");
		}
		threshold = *fitted;
	} else if (type == LogicalType::DATE && attribute->target == "Date") {
		// A typed DATE literal is represented by the parser as CAST(string AS DATE).
		// No column casts, TRY_CAST, untyped strings or other temporal types are accepted.
		if (test.Right().GetExpressionClass() == ExpressionClass::CAST) {
			auto &cast = test.Right().Cast<CastExpression>();
			auto target = UnboundType::TryDefaultBind(cast.TargetType());
			if (cast.IsTryCast() || target != LogicalType::DATE ||
			    cast.Child().GetExpressionClass() != ExpressionClass::CONSTANT) {
				Fail(constraint.owner, "rule", "requires needs a typed DATE constant");
			}
			auto value = cast.Child().Cast<ConstantExpression>().GetLiteral().ToValue();
			string error;
			auto date = value.IsNull() || value.type() != LogicalType::VARCHAR
			                ? optional<Value>()
			                : value.DefaultTryCastAs(LogicalType::DATE, &error, true);
			if (!date) {
				Fail(constraint.owner, "bind", "invalid requires DATE constant");
			}
			threshold = *date;
		} else {
			Fail(constraint.owner, "rule", "requires needs a typed DATE constant");
		}
		if (threshold.IsNull()) {
			Fail(constraint.owner, "rule", "requires constant cannot be NULL");
		}
	} else {
		Fail(constraint.owner, "bind", "requires threshold and actual scalar types must agree");
	}
	constraint.derived =
	    bundle.direct_derived ? DirectChild(bundle, constraint.owner) != nullptr : constraint.owner == bundle.derived;
	constraint.attribute = attribute->name;
	constraint.field = attribute->field;
	constraint.bound_type = type.ToString();
	constraint.null_policy = allow_null ? "allow" : "reject";
	constraint.threshold_sql = threshold.ToSQLString();
	constraint.predicate = make_uniq<ConjunctionExpression>(
	    tree->GetExpressionType(), make_uniq<OperatorExpression>(guard_type, physical->Copy()),
	    make_uniq<ComparisonExpression>(ExpressionType::COMPARE_GREATERTHANOREQUALTO, physical->Copy(),
	                                    ConstantExpression::FromValue(threshold)));
	CheckBoolean(bundle, *constraint.predicate);
}

void ValidateConceptConstraints(ClientContext &context, Bundle &bundle, Connection &engine) {
	// Bind every declaration even for empty populations before evaluating any one.
	for (auto &constraint : bundle.concept_constraints) {
		BindConceptConstraint(bundle, constraint);
	}
	for (auto &constraint : bundle.concept_constraints) {
		auto rule = bundle.direct_derived && constraint.derived ? DirectChild(bundle, constraint.owner)->rule.get()
		                                                        : bundle.rule.get();
		auto effective = bundle.single_inheritance ? EffectiveMembership(bundle, constraint.owner) : nullptr;
		if (bundle.single_inheritance) {
			rule = effective.get();
		}
		auto population = constraint.derived ? "(" + rule->ToString() + ") IS TRUE AND " : string();
		auto sql = "SELECT count(*) FROM main.ossie_snapshot AS " + Quote(bundle.model.datasets[0].name) + " WHERE " +
		           population + "(" + constraint.predicate->ToString() + ") IS DISTINCT FROM TRUE";
		auto result = Run(context, engine, FrozenSQL(bundle, sql))->Fetch();
		if (result->GetValue(0, 0).GetValue<int64_t>() != 0) {
			Fail(constraint.owner, "constraint",
			     "requires[" + to_string(constraint.index) +
			         "] violates entire declared concept population; snapshot not published");
		}
	}
}

string ConceptConstraintsJSON(const Bundle &bundle) {
	string result =
	    string("{\"schema_version\":") +
	    JSONString(bundle.single_inheritance ? "ossie-concept-constraints-v2" : "concept-scalar-requires-v1") +
	    (bundle.single_inheritance ? ",\"grammar_version\":\"concept-scalar-requires-v1\"" : "") +
	    ",\"enabled\":true,"
	    "\"load_constraints_validated\":true,\"query_executed\":false,\"operator\":\">=\","
	    "\"types\":[\"Integer\",\"Date\"],\"max_per_concept\":8,\"max_total_bytes\":131072,"
	    "\"max_rule_bytes\":16384,\"max_ast_nodes\":1024,\"max_ast_depth\":32,\"constraints\":[";
	for (idx_t i = 0; i < bundle.concept_constraints.size(); i++) {
		auto &constraint = bundle.concept_constraints[i];
		if (i) {
			result += ",";
		}
		result += "{\"owner\":" + JSONString(constraint.owner) + ",\"index\":" + to_string(constraint.index) +
		          ",\"original\":" + JSONString(constraint.original) +
		          ",\"attribute\":" + JSONString(constraint.attribute) +
		          ",\"source_field\":" + JSONString(OriginalName(bundle, constraint.field)) +
		          ",\"bound_type\":" + JSONString(constraint.bound_type) +
		          ",\"null_policy\":" + JSONString(constraint.null_policy) +
		          ",\"threshold_sql\":" + JSONString(constraint.threshold_sql) + ",\"population\":" +
		          JSONString(bundle.single_inheritance
		                         ? "owner effective lineage TRUE population"
		                         : (constraint.derived ? "derived rule TRUE objects" : "entire root objects")) +
		          (bundle.single_inheritance ? ",\"lineage\":" + JSONStrings(ConceptLineage(bundle, constraint.owner))
		                                     : string()) +
		          "}";
	}
	return result + "]}";
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

idx_t PhysicalColumn(const CapturedTable &table, const string &name) {
	for (idx_t i = 0; i < table.names.size(); i++) {
		if (StringUtil::CIEquals(table.names[i], name)) {
			return i;
		}
	}
	Fail(name, "snapshot", "undeclared physical column in captured source");
}

struct JoinColumns {
	idx_t fact_key;
	idx_t fact_identity;
	idx_t dimension_key;
	const string &fact_name;
	const string &dimension_name;
};

// std::string hashing/equality preserves full length, including NUL and UTF-8 bytes.
template <class KEY>
vector<idx_t> MatchKeys(ClientContext &context, const CapturedTable &facts, const CapturedTable &dims,
                        const JoinColumns &columns, unordered_set<string> &identities) {
	unordered_map<KEY, idx_t> lookup;
	for (idx_t r = 0; r < dims.rows.size(); r++) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		auto &key = dims.rows[r][columns.dimension_key];
		if (key.IsNull() || !lookup.emplace(key.GetValue<KEY>(), r).second) {
			Fail(columns.dimension_name, "identity", "entire dimension batch requires nonnull unique key");
		}
	}

	vector<idx_t> matches;
	for (auto &row : facts.rows) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		auto &identity = row[columns.fact_identity];
		if (identity.IsNull() || !identities.insert(identity.GetValue<string>()).second) {
			Fail(columns.fact_name, "identity", "entire fact batch requires nonnull unique identity");
		}
		auto &key = row[columns.fact_key];
		if (key.IsNull()) {
			Fail(columns.fact_name, "identity", "nonnull fact foreign key required before filtering");
		}
		auto found = lookup.find(key.GetValue<KEY>());
		if (found == lookup.end()) {
			Fail(columns.fact_name, "identity", "every fact foreign key must match a dimension before filtering");
		}
		matches.push_back(found->second);
	}
	return matches;
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
	auto result =
	    Run(context, source,
	        "SELECT * FROM " + Quote(parts[0].GetIdentifierName()) + "." + Quote(parts[1].GetIdentifierName()) + "." +
	            Quote(parts[2].GetIdentifierName()) + " LIMIT " + to_string(MAX_ROWS + 1));
	CapturedTable capture;
	capture.names = Names(result->GetNames());
	capture.types = result->GetTypes();
	auto count = result->RowCount();
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

unique_ptr<ColumnDataCollection> Collect(ClientContext &context, const CapturedTable &capture) {
	auto collection = make_uniq<ColumnDataCollection>(context, capture.types);
	DataChunk chunk;
	chunk.Initialize(Allocator::Get(context), capture.types);
	for (idx_t r = 0; r < capture.rows.size(); r++) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		idx_t slot = r % STANDARD_VECTOR_SIZE;
		for (idx_t c = 0; c < capture.types.size(); c++) {
			chunk.data[c].Append(capture.rows[r][c]);
		}
		chunk.CheckCardinality(slot + 1);
		if (slot + 1 == STANDARD_VECTOR_SIZE || r + 1 == capture.rows.size()) {
			collection->Append(chunk);
			chunk.Reset();
		}
	}
	return collection;
}

void ExpandOriginal(const Model &model, unique_ptr<ParsedExpression> &expression) {
	if (expression->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto names = ColumnNames(expression->Cast<ColumnRefExpression>());
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
		auto names = ColumnNames(expression->Cast<ColumnRefExpression>());
		if (names.empty() || names.size() > 2 || (names.size() == 2 && !StringUtil::CIEquals(names[0], dataset.name))) {
			Fail(expression->ToString(), "bind", "physical reference crosses dataset");
		}
		auto index = PhysicalColumn(capture, names.back()) + offset;
		expression = make_uniq<ColumnRefExpression>(Identifier("_ossie_column_" + to_string(index)), Identifier(root));
		return;
	}
	ParsedExpressionIterator::EnumerateChildren(*expression, [&](unique_ptr<ParsedExpression> &child) {
		FlattenPhysical(child, dataset, capture, offset, root);
	});
}

void FlattenLogical(const Bundle &bundle, unique_ptr<ParsedExpression> &expression) {
	if (expression->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto field = LogicalName(bundle, ColumnNames(expression->Cast<ColumnRefExpression>()));
		expression = make_uniq<ColumnRefExpression>(Identifier(field), Identifier(bundle.model.datasets[0].name));
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
	if (bundle.string_join && relation.cardinality != Cardinality::MANY_TO_ONE) {
		Fail("join", "capability", "native-string-join-v1 requires ManyToOne");
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
	const bool integer_keys = facts.types[fk] == LogicalType::INTEGER && dims.types[dk] == LogicalType::INTEGER;
	const bool string_keys =
	    bundle.string_join && facts.types[fk] == LogicalType::VARCHAR && dims.types[dk] == LogicalType::VARCHAR;
	if (!integer_keys && !string_keys) {
		Fail("join", "identity",
		     bundle.string_join
		         ? "both join keys must be matching native INTEGER or uncollated VARCHAR; coercion refused"
		         : "both join keys must be native INTEGER; coercion refused");
	}
	bundle.join_key_type = facts.types[fk].ToString();
	if (facts.types[id] != LogicalType::VARCHAR) {
		Fail("join", "identity", "fact identity must be native uncollated String");
	}
	JoinColumns columns {fk, id, dk, fact->name, dimension->name};
	unordered_set<string> identities;
	auto matches = integer_keys ? MatchKeys<int32_t>(context, facts, dims, columns, identities)
	                            : MatchKeys<string>(context, facts, dims, columns, identities);
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
			chunk.data[c].Append(value);
			Frame(joined, value.ToSQLString());
			if (joined.size() > MAX_BYTES) {
				Fail("join", "snapshot", "joined batch exceeds 16 MiB canonical value limit");
			}
		}
		chunk.CheckCardinality(slot + 1);
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
void ObservationDefinitions(Bundle &b, const Ontology &ontology, const Mapping &mapping) {
	bool program = b.profile == RELATION_PROFILE;
	Empty(ontology.requires_list, "requires", "ontology");
	const Concept *user = nullptr, *observation = nullptr, *derived = nullptr;
	case_insensitive_set_t names;
	for (auto &c : ontology.concepts) {
		if (!program || c.identify_by.size() != 3) {
			Empty(c.requires_list, "requires", "concept");
		}
		if (c.type != "EntityType" || !names.insert(c.name).second) {
			Fail("ontology", "capability", "three distinct entity concepts required");
		}
		if (!c.extends.empty()) {
			if (derived) {
				Fail("ontology", "capability", "single derived observation required");
			}
			derived = &c;
		} else if (c.identify_by.size() == 1) {
			if (user) {
				Fail("ontology", "capability", "single referenced User required");
			}
			user = &c;
		} else if (c.identify_by.size() == 3) {
			if (observation) {
				Fail("ontology", "capability", "single observation root required");
			}
			observation = &c;
		} else {
			Fail("ontology", "capability", "User and three-component observation identities required");
		}
	}
	if (ontology.concepts.size() != 3 || !user || !observation || !derived) {
		Fail("ontology", "capability", "profile requires User, observation and derived observation");
	}
	b.user_concept = user->name;
	b.root = observation->name;
	b.derived = derived->name;
	if (program) {
		b.relation_input.requires_list = observation->requires_list;
	}
	b.user_id_relation = user->identify_by[0];
	if (derived->extends.size() != 1 || derived->extends[0] != b.root) {
		Fail(b.derived, "capability", "single inheritance from observation required");
	}
	Empty(derived->identify_by, "identify_by", b.derived);
	if (!derived->relationships.empty()) {
		Fail(b.derived, "capability", "unsupported nonempty relationships");
	}
	Empty(user->derived_by, "derived_by", b.user_concept);
	Empty(observation->derived_by, "derived_by", b.root);
	auto relationship_target = [&](const OntologyRelationship &r, const string &multiplicity) {
		Empty(r.requires_list, "requires", "relationship");
		Empty(r.derived_by, "derived_by", "relationship");
		if (r.roles.size() != 1 || r.multiplicity != multiplicity) {
			Fail(r.name, "capability", "unsupported relationship role or multiplicity");
		}
		return r.roles[0];
	};
	auto &ur = user->relationships;
	if (ur.size() != 1 || ur[0].name != b.user_id_relation || relationship_target(ur[0], "OneToOne") != "String") {
		Fail(b.user_concept, "capability", "referenced User has only a OneToOne String identity");
	}
	auto &identity_relations = observation->identify_by;
	b.observation_relationships = identity_relations;
	case_insensitive_set_t relation_names, targets;
	for (auto &r : observation->relationships) {
		if (program && !relation_names.insert(r.name).second) {
			Fail(r.name, "capability", "duplicate mapped or derived relationship name");
		}
		if (program && !r.derived_by.empty()) {
			if (r.roles.size() != 1) {
				Fail(r.name, "capability", "single User role required");
			}
			RelationDeclaration declaration;
			declaration.name = r.name;
			declaration.role_concept = r.roles[0];
			declaration.multiplicity = r.multiplicity;
			declaration.derived_by = r.derived_by;
			declaration.requires_list = r.requires_list;
			b.relation_input.declarations.push_back(std::move(declaration));
			continue;
		}
		auto &name = r.name;
		auto target = relationship_target(r, "ManyToOne");
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
	References(b, ontology, mapping, "document reference name mismatch");
	// A nested User referent has exactly its id. It is not another link-tree layer.
	auto user_id = [&](const ScalarReferent &referent) {
		if (referent.relationship != b.user_id_relation) {
			Fail(b.user_concept, "mapping", "User identity relationship mismatch");
		}
		return LogicalField(b, referent.expression);
	};
	auto user_object = [&](const ObjectMapping &object) {
		return user_id(SingleReferent(object, b.user_concept, "single User identity referent required"));
	};
	auto observation_referent = [&](const ObjectMapping &object) {
		if (!object.concept.empty() && object.concept != b.root) {
			Fail(b.root, "mapping", "observation concept override");
		}
		if (!object.entity || object.referents.size() != 3) {
			Fail(b.root, "mapping", "three observation referents required");
		}
		vector<string> fields;
		for (idx_t i = 0; i < object.referents.size(); i++) {
			auto &referent = object.referents[i];
			if (referent.relationship != identity_relations[i]) {
				Fail(b.root, "mapping", "observation referent order mismatch");
			}
			if (i == 0) {
				fields.push_back(
				    user_id(SingleReferent(referent, b.user_concept, "single User identity referent required")));
			} else {
				if (referent.nested) {
					Fail(b.root, "mapping", "scalar identity referent required");
				}
				fields.push_back(LogicalField(b, referent.expression));
			}
		}
		return fields;
	};
	if (mapping.concept_mappings.size() != 2) {
		Fail("mapping", "capability", "only User and root observation mappings required");
	}
	case_insensitive_set_t mapped;
	string user_field;
	for (auto &cm : mapping.concept_mappings) {
		auto &concept = cm.concept;
		if (!mapped.insert(concept).second || (concept != b.root && concept != b.user_concept)) {
			Fail(concept, "mapping", "unexpected concept mapping");
		}
		if (cm.object_mappings.size() != 1 || cm.link_mappings.size() != 1) {
			Fail(concept, "mapping", "single object and link tree required");
		}
		auto &tree = cm.link_mappings[0];
		if (concept == b.user_concept) {
			for (auto object : {&cm.object_mappings[0], &tree.object}) {
				if (!object->concept.empty() && object->concept != b.user_concept) {
					Fail(concept, "mapping", "User concept override");
				}
				auto field = user_object(*object);
				if (!user_field.empty() && user_field != field) {
					Fail(concept, "mapping", "User identity override");
				}
				user_field = field;
			}
			if (tree.children.size() != 1) {
				Fail(concept, "mapping", "User has only its identity link");
			}
			auto &object = tree.children[0].object;
			if (tree.children[0].relationship != b.user_id_relation || object.entity || object.concept != "String" ||
			    LogicalField(b, object.expression) != user_field) {
				Fail(concept, "mapping", "User id link mismatch");
			}
		} else {
			b.observation_identity = observation_referent(cm.object_mappings[0]);
			if (observation_referent(tree.object) != b.observation_identity) {
				Fail(concept, "mapping", "observation identity override");
			}
			case_insensitive_set_t bound;
			for (auto &link : tree.children) {
				auto &name = link.relationship;
				Attribute *a = nullptr;
				for (auto &candidate : b.attributes) {
					if (candidate.name == name) {
						a = &candidate;
					}
				}
				if (!a || !bound.insert(name).second) {
					Fail(name, "mapping", "unknown or duplicate observation link");
				}
				auto &object = link.object;
				if (a->target == b.user_concept) {
					a->field = user_object(object);
				} else {
					if (object.entity) {
						Fail(name, "mapping", "scalar link object required");
					}
					a->field = LogicalField(b, object.expression);
				}
				if (object.concept != a->target || b.model.datasets[0].FindField(a->field)->datatype !=
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
		physical.push_back(ColumnNames(tree->Cast<ColumnRefExpression>()).back());
	}
	if (b.model.datasets[0].primary_key != physical) {
		Fail(b.root, "identity", "declared primary key must match typed observation tuple");
	}
	case_insensitive_set_t bound_fields, physical_fields;
	for (auto &a : b.attributes) {
		auto tree = FieldTree(b, a.field);
		if (tree->GetExpressionClass() != ExpressionClass::COLUMN_REF || !bound_fields.insert(a.field).second ||
		    !physical_fields.insert(ColumnNames(tree->Cast<ColumnRefExpression>()).back()).second) {
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
		auto name = StringUtil::Lower(f.FunctionName().GetIdentifierName());
		if (f.GetArguments().size() != 1 || f.GetArguments()[0].HasName()) {
			Fail(m.name, "capability", "single aggregate argument required");
		}
		string field;
		for (const auto &candidate : {b.identity, b.amount_field}) {
			if (f.GetArguments()[0].GetExpression().Equals(*FieldTree(b, candidate))) {
				field = candidate;
			}
		}
		bool valid = (name == "count" && field == b.identity) ||
		             (!f.Distinct() && field == b.amount_field && (name == "count" || name == "sum" || name == "avg"));
		auto shape = name + ":" + (f.Distinct() ? "distinct:" : "all:") + field;
		if (!valid || !metric_shapes.insert(shape).second) {
			Fail(m.name, "capability", "unsupported or repeated observation metric shape");
		}
	}
	if (metric_shapes.size() != 5) {
		Fail("metrics", "capability", "all five observation aggregate forms required");
	}
	if (derived->derived_by.size() != 1) {
		Fail(b.derived, "rule", "single observation qualification rule required");
	}
	b.original_rule = derived->derived_by[0];
	b.rule = Parse(b.original_rule);
	Strict(*b.rule);
	Resolve(b, b.rule, true);
	if (b.rule->GetExpressionType() != ExpressionType::COMPARE_GREATERTHANOREQUALTO) {
		Fail(b.derived, "capability", "qualification must be class integer >= integer constant");
	}
	auto &comparison = b.rule->Cast<ComparisonExpression>();
	if (!comparison.Left().Equals(*FieldTree(b, b.class_field)) ||
	    comparison.Right().GetExpressionClass() != ExpressionClass::CONSTANT ||
	    !comparison.Right().Cast<ConstantExpression>().GetLiteral().ToValue().type().IsIntegral() ||
	    comparison.Right().Cast<ConstantExpression>().GetLiteral().IsNull()) {
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
	if (duplicates->RowCount() != 0) {
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
				auto parsed = Parser::GetBuiltinParser().ParseExpressionList(sql);
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
	auto names = ColumnNames(expr->Cast<ColumnRefExpression>());
	if (names.size() != 2 || !StringUtil::CIEquals(names[0], dataset.name) || !dataset.FindField(names[1])) {
		Fail(sql, "mapping", "declared field in this fact required");
	}
	return dataset.FindField(names[1])->name;
}
string FactPhysicalField(const Dataset &dataset, const Field &field) {
	if (field.expression.tree->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		Fail(field.name, "capability", "fact field must directly map a physical column");
	}
	auto names = ColumnNames(field.expression.tree->Cast<ColumnRefExpression>());
	if (names.empty() || names.size() > 2 || (names.size() == 2 && !StringUtil::CIEquals(names[0], dataset.name))) {
		Fail(field.name, "bind", "own physical column required");
	}
	return names.back();
}
void FactDefinitions(Bundle &b, const Ontology &ontology, const Mapping &mapping, yyjson_val *config) {
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
	Empty(ontology.requires_list, "requires", "ontology");
	if (ontology.concepts.size() != 3) {
		Fail("ontology", "capability", "referent User and two root entities required");
	}
	const Concept *user = nullptr;
	case_insensitive_map_t<const Concept *> root_objects;
	case_insensitive_set_t allconcepts;
	for (auto &concept : ontology.concepts) {
		auto &name = concept.name;
		if (!allconcepts.insert(name).second || concept.type != "EntityType") {
			Fail(name, "capability", "distinct EntityType concepts required");
		}
		Empty(concept.extends, "extends", name);
		Empty(concept.derived_by, "derived_by", name);
		Empty(concept.requires_list, "requires", name);
		if (roots.count(name)) {
			root_objects[name] = &concept;
		} else {
			if (user) {
				Fail(name, "capability", "one User referent required");
			}
			user = &concept;
			b.user_concept = name;
		}
	}
	if (!user || root_objects.size() != 2) {
		Fail("ontology", "bind", "configured fact roots and one User referent required");
	}
	if (user->identify_by.size() != 1 || user->relationships.size() != 1) {
		Fail(b.user_concept, "identity", "single User identity required");
	}
	b.user_id_relation = user->identify_by[0];
	auto target = [&](const OntologyRelationship &relation, const string &multiplicity) {
		Empty(relation.requires_list, "requires", "relationship");
		Empty(relation.derived_by, "derived_by", "relationship");
		if (relation.roles.size() != 1 || relation.multiplicity != multiplicity) {
			Fail(relation.name, "capability", "single role and declared multiplicity required");
		}
		return relation.roles[0];
	};
	if (user->relationships[0].name != b.user_id_relation || target(user->relationships[0], "OneToOne") != "String") {
		Fail(b.user_concept, "identity", "User identity must be OneToOne String");
	}
	b.fact_input.user_concept = b.user_concept;
	b.fact_input.user_identity_relationship = b.user_id_relation;
	References(b, ontology, mapping, "definition references must agree");
	if (mapping.concept_mappings.size() != 2) {
		Fail("mapping", "capability", "only two fact root mappings; User is a nested referent");
	}
	case_insensitive_map_t<const ConceptMapping *> mapped;
	for (auto &cm : mapping.concept_mappings) {
		if (!roots.count(cm.concept) || !mapped.emplace(cm.concept, &cm).second) {
			Fail(cm.concept, "mapping", "distinct configured root mappings required");
		}
	}
	for (auto &source : b.fact_input.sources) {
		auto &dataset = *b.model.FindDataset(source.dataset);
		auto &concept = *root_objects.at(source.concept);
		source.dataset = dataset.name;
		source.concept = concept.name;
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
		for (auto &relation : concept.relationships) {
			auto t = target(relation, "ManyToOne");
			if (!relations.emplace(relation.name, t).second || !targets.insert(t).second ||
			    (t != b.user_concept && t != "Date" && t != "String" && t != "Decimal")) {
				Fail(relation.name, "capability", "one User/Date[/product String]/Decimal role required");
			}
		}
		auto &identity_relations = concept.identify_by;
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
		auto user_id = [&](const ScalarReferent &referent) {
			if (referent.relationship != b.user_id_relation) {
				Fail(source.concept, "mapping", "same declared User id required");
			}
			return FactLogicalField(dataset, referent.expression);
		};
		auto identity = [&](const ObjectMapping &object) {
			if (!object.concept.empty() && !StringUtil::CIEquals(object.concept, source.concept)) {
				Fail(source.concept, "mapping", "root concept override refused");
			}
			if (!object.entity || object.referents.size() != identity_relations.size()) {
				Fail(source.concept, "identity", "full root tuple required");
			}
			vector<string> fields;
			for (idx_t i = 0; i < object.referents.size(); i++) {
				auto &referent = object.referents[i];
				if (!StringUtil::CIEquals(referent.relationship, identity_relations[i])) {
					Fail(source.concept, "identity", "referent order must agree with root identity");
				}
				if (i == 0) {
					fields.push_back(
					    user_id(SingleReferent(referent, source.concept, "single nested User id required")));
				} else {
					if (referent.nested) {
						Fail(source.concept, "identity", "scalar root referent required");
					}
					fields.push_back(FactLogicalField(dataset, referent.expression));
				}
			}
			return fields;
		};
		auto &cm = *mapped.at(source.concept);
		if (cm.object_mappings.size() != 1 || cm.link_mappings.size() != 1) {
			Fail(source.concept, "mapping", "one root object and full link tree required");
		}
		auto &tree = cm.link_mappings[0];
		source.identity_fields = identity(cm.object_mappings[0]);
		if (identity(tree.object) != source.identity_fields) {
			Fail(source.concept, "identity", "object and link identity differ");
		}
		case_insensitive_set_t bound;
		for (auto &link : tree.children) {
			auto &name = link.relationship;
			auto it = relations.find(name);
			if (it == relations.end() || !bound.insert(name).second) {
				Fail(name, "mapping", "unknown or duplicate mapped relationship");
			}
			auto &object = link.object;
			auto t = it->second;
			bool entity = t == b.user_concept;
			if (object.entity != entity) {
				Fail(name, "mapping", entity ? "nested User referent required" : "scalar value required");
			}
			if (object.concept != t) {
				Fail(name, "mapping", "mapped role target differs");
			}
			auto field = entity ? user_id(SingleReferent(object, source.concept, "single nested User id required"))
			                    : FactLogicalField(dataset, object.expression);
			if (dataset.FindField(field)->datatype != (entity ? "String" : t)) {
				Fail(field, "bind", "declared logical datatype differs from role");
			}
			if (entity) {
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
				    (j == 1 ? !value.GetValue<date_t>().IsFinite() : value.GetValue<string>().empty())) {
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
		child->collection = Collect(context, capture);
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
				auto parsed = Parser::GetBuiltinParser().ParseExpressionList(sql);
				if (parsed.size() != 1 || !parsed[0]->GetAlias().empty() ||
				    parsed[0]->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
					Fail("history", "capability", "one direct unaliased field before model lowering");
				}
			}
		}
	}
}
void HistoryDefinitions(Bundle &b, const Ontology &ontology, const Mapping &mapping, yyjson_val *program) {
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
	Empty(ontology.requires_list, "requires", "ontology");
	case_insensitive_map_t<const Concept *> concepts;
	case_insensitive_set_t builtins = {"Any", "Boolean", "Date", "DateTime", "Decimal", "Float", "Integer", "String"};
	for (auto &c : ontology.concepts) {
		if (builtins.count(c.name) || c.type != "EntityType" || !concepts.emplace(c.name, &c).second) {
			Fail("history", "bind", "distinct EntityTypes required");
		}
		Empty(c.extends, "extends", "history concept");
		Empty(c.derived_by, "derived_by", "history concept");
		Empty(c.requires_list, "requires", "history concept");
	}
	if (concepts.size() != 4) {
		Fail("history", "capability", "two roots, User and target referents required");
	}
	auto relation_map = [&](const string &concept) {
		if (!concepts.count(concept)) {
			Fail(concept, "bind", "declared concept required");
		}
		case_insensitive_map_t<string> roles;
		for (auto &r : concepts.at(concept)->relationships) {
			Empty(r.derived_by, "derived_by", "history relationship");
			Empty(r.requires_list, "requires", "history relationship");
			auto &name = r.name;
			if (r.roles.size() != 1) {
				Fail(name, "capability", "one target role required");
			}
			if (r.multiplicity != (name == "id" || name == "record_id" ? "OneToOne" : "ManyToOne") ||
			    !roles.emplace(name, r.roles[0]).second) {
				Fail(name, "capability", "declared multiplicity and distinct role names required");
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
		auto &ids = concepts.at(name)->identify_by;
		if (roles.size() != 1 || !roles.count("id") || roles.at("id") != "String" || ids.size() != 1 ||
		    ids[0] != "id") {
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
	References(b, ontology, mapping, "matching document references required");
	case_insensitive_map_t<const ConceptMapping *> mappings;
	for (auto &cm : mapping.concept_mappings) {
		if (!mappings.emplace(cm.concept, &cm).second) {
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
		auto &declared_ids = concepts.at(src.concept)->identify_by;
		if (declared_ids.size() != ids.size()) {
			Fail(src.concept, "identity", "full fixed source identity required");
		}
		for (idx_t i = 0; i < ids.size(); i++) {
			if (declared_ids[i] != ids[i]) {
				Fail(src.concept, "identity", "identity role order differs");
			}
		}
		auto id_referent = [&](const ScalarReferent &referent) {
			if (referent.relationship != "id") {
				Fail(src.concept, "mapping", "single declared id referent required");
			}
			return FactLogicalField(dataset, referent.expression);
		};
		auto identity = [&](const ObjectMapping &o) {
			if (o.concept != src.concept) {
				Fail(src.concept, "mapping", "root override refused");
			}
			if (!o.entity || o.referents.size() != ids.size()) {
				Fail(src.concept, "identity", "full root tuple required");
			}
			vector<string> fields;
			for (idx_t i = 0; i < ids.size(); i++) {
				auto &referent = o.referents[i];
				if (referent.relationship != ids[i]) {
					Fail(src.concept, "identity", "referent order differs");
				}
				if (s == 0 && i == 0) {
					fields.push_back(
					    id_referent(SingleReferent(referent, src.concept, "single declared id referent required")));
				} else {
					if (referent.nested) {
						Fail(src.concept, "identity", "scalar referent required");
					}
					fields.push_back(FactLogicalField(dataset, referent.expression));
				}
			}
			return fields;
		};
		auto &cm = *mappings.at(src.concept);
		if (cm.object_mappings.size() != 1 || cm.link_mappings.size() != 1) {
			Fail(src.concept, "mapping", "one full source object/link tree required");
		}
		auto &tree = cm.link_mappings[0];
		auto id_fields = identity(cm.object_mappings[0]);
		if (identity(tree.object) != id_fields) {
			Fail(src.concept, "identity", "object/link identities differ");
		}
		case_insensitive_map_t<string> fields;
		for (auto &link : tree.children) {
			auto &role = link.relationship;
			auto expected = s == 0 ? obs_roles : hist_roles;
			if (std::find(src.roles.begin(), src.roles.end(), role) == src.roles.end()) {
				Fail(role, "mapping", "unknown/direct generated link refused");
			}
			auto &o = link.object;
			bool entity = role == "user" || role == "team";
			if (o.entity != entity) {
				Fail(role, "mapping", entity ? "nested id referent required" : "scalar value required");
			}
			if (o.concept != expected.at(role)) {
				Fail(role, "mapping", "declared typed target differs");
			}
			auto field = entity ? id_referent(SingleReferent(o, src.concept, "single declared id referent required"))
			                    : FactLogicalField(dataset, o.expression);
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
const char *const HISTORY_SEGMENTS = "_ossie_history_segments";

string HistoryRoleColumn(const Bundle &b, idx_t source, const string &role) {
	auto &src = b.history_sources[source];
	auto &ds = *b.model.FindDataset(src.dataset);
	auto position = std::find(src.roles.begin(), src.roles.end(), role) - src.roles.begin();
	return Quote(FactPhysicalField(ds, *ds.FindField(src.fields[position])));
}

// Validates the complete history witnesses and merges them into per-User half-open
// target segments; the segments become a frozen child source for the ASOF link join.
void BuildHistorySegments(ClientContext &context, Bundle &b) {
	auto &hist = b.history_sources[1];
	auto records = "(SELECT " + HistoryRoleColumn(b, 1, "record_id") + " AS record_id, " +
	               HistoryRoleColumn(b, 1, "user") + " AS user_id, " + HistoryRoleColumn(b, 1, "team") +
	               " AS target, " + HistoryRoleColumn(b, 1, "starts") + " AS starts, " +
	               HistoryRoleColumn(b, 1, "ends") + " AS ends FROM " + Quote(hist.dataset) + ") AS records";
	auto engine = Engine(b);
	auto checks = Run(context, *engine,
	                  ProgramSQL(b, b.history_children,
	                             "SELECT count(*) - count(DISTINCT record_id), count(*) FILTER (WHERE starts >= ends) "
	                             "FROM " +
	                                 records));
	auto chunk = checks->Fetch();
	if (chunk->GetValue(0, 0).GetValue<int64_t>() != 0) {
		Fail("history", "identity", "duplicate history record_id across complete capture");
	}
	if (chunk->GetValue(1, 0).GetValue<int64_t>() != 0) {
		Fail("history", "constraint", "finite history DATE endpoints require start<end");
	}
	auto segments = Run(
	    context, *engine,
	    ProgramSQL(b, b.history_children,
	               "SELECT user_id, target, min(starts) AS starts, max(ends) AS ends FROM (SELECT user_id, target, "
	               "starts, ends, sum(CASE WHEN previous_end IS NULL OR starts > previous_end THEN 1 ELSE 0 END) OVER "
	               "(PARTITION BY user_id, target ORDER BY starts, ends ROWS UNBOUNDED PRECEDING) AS island FROM "
	               "(SELECT user_id, target, starts, ends, max(ends) OVER (PARTITION BY user_id, target ORDER BY "
	               "starts, ends ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING) AS previous_end FROM " +
	                   records +
	                   ") AS marked) AS islands GROUP BY user_id, target, island ORDER BY user_id, starts, target"));
	auto child = make_shared_ptr<Bundle>();
	child->owner = child;
	child->database = b.database;
	child->profile = HISTORY_PROFILE;
	Dataset dataset;
	dataset.name = HISTORY_SEGMENTS;
	dataset.source_bound = "memory.main.ossie_snapshot";
	child->model.datasets.push_back(dataset);
	child->physical_names = {"user_id", "target", "starts", "ends"};
	child->collection = segments->TakeCollection();
	child->rows = child->collection->Count();
	b.history_segments = std::move(child);
	auto summary = Run(context, *engine,
	                   ProgramSQL(b, {b.history_segments},
	                              "SELECT count(*) FILTER (WHERE starts < previous_end), count(*), count(DISTINCT "
	                              "user_id) FROM (SELECT user_id, starts, max(ends) OVER (PARTITION BY user_id ORDER "
	                              "BY starts, ends ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING) AS previous_end "
	                              "FROM " +
	                                  Quote(HISTORY_SEGMENTS) + ") AS ordered"));
	chunk = summary->Fetch();
	if (chunk->GetValue(0, 0).GetValue<int64_t>() != 0) {
		Fail("history", "constraint", "different history targets overlap for the same raw User");
	}
	b.history_segment_count = chunk->GetValue(1, 0).GetValue<int64_t>();
	b.history_user_count = chunk->GetValue(2, 0).GetValue<int64_t>();
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
				    (date ? !value.GetValue<date_t>().IsFinite() : value.GetValue<string>().empty())) {
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
		child->collection = Collect(context, capture);
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
	BuildHistorySegments(context, b);
	b.definition_model = b.model;
	b.content_hash = Hash(engine, evidence);
	b.snapshot_id = b.content_hash + ":" + to_string(NextBatchSequence());
}

string InferProfile(yyjson_val *root, const Ontology &ontology) {
	auto program = Member(root, "program");
	if (Member(root, "join") && program) {
		Fail("bundle manifest", "capability", "join and program sections are mutually exclusive");
	}
	if (Member(root, "join")) {
		return JOIN_PROFILE;
	}
	if (program) {
		return Member(program, "operator") ? HISTORY_PROFILE : FACT_PROFILE;
	}
	bool observation = false, relation = false;
	for (auto &c : ontology.concepts) {
		if (c.identify_by.size() == 3) {
			observation = true;
			relation |= !c.requires_list.empty();
		}
		for (auto &r : c.relationships) {
			relation |= !r.derived_by.empty();
		}
	}
	if (!observation) {
		return PROFILE;
	}
	return relation ? RELATION_PROFILE : OBSERVATION_PROFILE;
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
	Keys(root, {"profile", "core", "ontology", "mapping", "capabilities", "join", "program"}, "bundle manifest");
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
	Ontology ontology;
	Mapping mapping;
	{
		Document ontology_document(ontology_bytes), mapping_document(mapping_bytes);
		ontology = ParseOntology(ontology_document.Root());
		mapping = ParseMapping(mapping_document.Root());
	}
	bundle->ontology_name = ontology.name;
	bundle->ontology_version = ontology.version;
	bundle->mapping_name = mapping.name;
	bundle->mapping_version = mapping.version;
	bundle->profile = InferProfile(root, ontology);
	if (Member(root, "profile") && Text(root, "profile") != bundle->profile) {
		Fail("profile", "capability", "declared profile does not match the manifest sections and ontology shape");
	}
	if (Member(root, "capabilities")) {
		if (bundle->profile != PROFILE && bundle->profile != JOIN_PROFILE) {
			Fail("capabilities", "capability", "capabilities apply only to scalar and joined populations");
		}
		auto capabilities = Array(root, "capabilities", true);
		bool direct = false, hierarchy = false;
		for (auto value : capabilities) {
			if (yyjson_is_str(value) && ArrayText(value) == DIRECT_DERIVED) {
				direct = true;
			}
			if (yyjson_is_str(value) && ArrayText(value) == SINGLE_INHERITANCE) {
				hierarchy = true;
			}
		}
		if (direct && hierarchy) {
			Fail("capabilities", "capability", "one inheritance mode required");
		}
		if (direct || hierarchy) {
			case_insensitive_set_t seen;
			for (auto value : capabilities) {
				auto name = ArrayText(value);
				if ((name != CONCEPT_REQUIRES && name != STRING_JOIN && name != DIRECT_DERIVED &&
				     name != SINGLE_INHERITANCE) ||
				    !seen.insert(name).second) {
					Fail("capabilities", "capability", "distinct supported capabilities required");
				}
				bundle->concept_requires |= name == CONCEPT_REQUIRES;
				bundle->string_join |= name == STRING_JOIN;
			}
			bundle->direct_derived = true;
			bundle->single_inheritance = hierarchy;
		} else if (capabilities.size() == 1) {
			auto name = ArrayText(capabilities[0]);
			bundle->concept_requires = name == CONCEPT_REQUIRES;
			bundle->string_join = name == STRING_JOIN;
		} else if (capabilities.size() == 2 && yyjson_is_str(capabilities[0]) && yyjson_is_str(capabilities[1])) {
			auto first = ArrayText(capabilities[0]), second = ArrayText(capabilities[1]);
			if ((first == CONCEPT_REQUIRES && second == STRING_JOIN) ||
			    (second == CONCEPT_REQUIRES && first == STRING_JOIN)) {
				bundle->concept_requires = true;
				bundle->string_join = true;
			}
		}
		if (!bundle->concept_requires && !bundle->string_join && !bundle->direct_derived) {
			Fail("capabilities", "capability", "exactly concept-scalar-requires-v1 required");
		}
		if (bundle->direct_derived && bundle->profile != PROFILE && bundle->profile != JOIN_PROFILE) {
			Fail("capabilities", "capability", "direct-derived-concepts-v1 requires scalar or joined profile");
		}
		if (bundle->string_join && bundle->profile != JOIN_PROFILE) {
			Fail("capabilities", "capability", "native-string-join-v1 requires fact-dimension-population-v1");
		}
	}
	if (bundle->profile == HISTORY_PROFILE) {
		Document core(core_json);
		HistoryCoreBudget(core.Root());
		bundle->model = ParseModel(core_json, RebindMap());
		HistoryDefinitions(*bundle, ontology, mapping, Member(root, "program"));
		FreezeHistory(context, *bundle, *engine);
		return bundle;
	}
	if (bundle->profile == FACT_PROFILE) {
		Document core(core_json);
		FactCoreBudget(core.Root());
		bundle->model = ParseModel(core_json, RebindMap(), MetricReferencePolicy::DECLARED_METRICS);
		FactDefinitions(*bundle, ontology, mapping, Member(root, "program"));
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
	if (bundle->profile == OBSERVATION_PROFILE || bundle->profile == RELATION_PROFILE) {
		ObservationDefinitions(*bundle, ontology, mapping);
	} else {
		Definitions(*bundle, ontology, mapping);
	}
	for (auto &entry : bundle->provenance) {
		if (ConceptNamespace(*bundle, entry.first[0])) {
			Fail(entry.first[0], "bind", "dataset and concept namespaces must be distinct");
		}
	}
	if (ConceptNamespace(*bundle, dataset.name) ||
	    (!bundle->user_concept.empty() && StringUtil::CIEquals(dataset.name, bundle->user_concept))) {
		Fail(dataset.name, "bind", "dataset and concept namespaces must be distinct");
	}
	if (bundle->profile == PROFILE || bundle->profile == OBSERVATION_PROFILE || bundle->profile == RELATION_PROFILE) {
		bundle->original_source = dataset.source_bound;
		Connection source(DatabaseInstance::GetDatabase(context));
		Run(context, source, "BEGIN TRANSACTION");
		optional_ptr<Catalog> catalog;
		optional_ptr<CatalogEntry> previous;
		auto capture = CaptureTable(context, source, dataset, catalog, previous);
		Run(context, source, "COMMIT");
		bundle->rows = capture.rows.size();
		bundle->physical_names = capture.names;
		bundle->collection = Collect(context, capture);
		bundle->content_hash = Hash(*engine, capture.canonical);
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
		options.population_sources[bundle->model.datasets[0].name] = source_ref.get();
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
	if (bundle->direct_derived) {
		for (auto &child : bundle->direct_concepts) {
			CheckBoolean(*bundle, *child.rule);
		}
	} else {
		CheckBoolean(*bundle, *bundle->rule);
	}
	if (bundle->profile == OBSERVATION_PROFILE || bundle->profile == RELATION_PROFILE) {
		ObservationIdentityPreflight(context, *bundle, *engine);
		if (bundle->profile == RELATION_PROFILE) {
			auto &input = bundle->relation_input;
			input.root = bundle->root;
			input.user_concept = bundle->user_concept;
			input.dataset = dataset.name;
			input.user_column = ColumnNames(FieldTree(*bundle, bundle->identity)->Cast<ColumnRefExpression>()).back();
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
					     ColumnNames(FieldTree(*bundle, attribute.field)->Cast<ColumnRefExpression>()).back(),
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
	if (bundle->concept_requires) {
		ValidateConceptConstraints(context, *bundle, *engine);
	}
	return bundle;
}

unique_ptr<ParsedExpression> Population(Bundle &bundle, const string &concept, const vector<string> &filters) {
	if (bundle.profile == OBSERVATION_PROFILE && concept == bundle.user_concept) {
		Fail(concept, "capability", "User is referent-only; direct objects and metrics queries unsupported");
	}
	auto selected = bundle.direct_derived ? DirectChild(bundle, concept) : nullptr;
	if (concept != bundle.root && (bundle.direct_derived ? selected == nullptr : concept != bundle.derived)) {
		Fail(concept, "bind", "unknown executable concept");
	}
	unique_ptr<ParsedExpression> predicate;
	if (bundle.direct_derived) {
		if (selected) {
			predicate = bundle.single_inheritance ? EffectiveMembership(bundle, concept) : selected->rule->Copy();
		}
	} else if (concept == bundle.derived) {
		predicate = bundle.rule->Copy();
	}
	if (filters.size() > 64) {
		Fail(concept, "capability", "at most 64 request filters supported");
	}
	for (auto &filter : filters) {
		auto expression = Parse(filter);
		Strict(*expression);
		Resolve(bundle, expression, false, bundle.direct_derived ? concept : "");
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
		options.population_sources[bundle.model.datasets[0].name] = source.get();
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
				translated.push_back(make_uniq<ColumnRefExpression>(Identifier(LogicalField(bundle, dimension)),
				                                                    Identifier(bundle.model.datasets[0].name))
				                         ->ToString());
			}
		}
		auto statement = Compile(bundle.model, metrics, translated, {}, options);
		if (!bundle.provenance.empty()) {
			auto &select = statement->node->Cast<SelectNode>();
			for (idx_t i = 0; i < dimensions.size(); i++) {
				select.select_list[i]->SetAlias(Identifier(dimensions[i]));
			}
		}
		return statement;
	}
	auto select = make_uniq<SelectNode>();
	if (bundle.profile != OBSERVATION_PROFILE) {
		for (auto name : {"entity_id", "record_id"}) {
			auto id = FieldTree(bundle, bundle.identity);
			id->SetAlias(Identifier(name));
			select->select_list.push_back(std::move(id));
		}
	}
	for (auto &field : bundle.model.datasets[0].fields) {
		auto expression = FieldTree(bundle, field.name);
		expression->SetAlias(Identifier(OriginalName(bundle, field.name)));
		select->select_list.push_back(std::move(expression));
	}
	select->where_clause = std::move(predicate);
	select->from_table = SnapshotTable(bundle);
	auto statement = make_uniq<SelectStatement>();
	statement->node = std::move(select);
	return statement;
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
	auto selected = bundle.direct_derived ? DirectChild(bundle, concept) : nullptr;
	auto derived = bundle.direct_derived ? selected != nullptr : concept == bundle.derived;
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
	auto rule = bundle.direct_derived ? (selected ? selected->rule->Copy() : nullptr) : bundle.rule->Copy();
	if (rule) {
		EngineFunctions(*rule);
	}
	string formal_rule = "null";
	if (!bundle.direct_derived || selected) {
		formal_rule = "{\"owner\":" + JSONString(bundle.direct_derived ? selected->name : bundle.derived) +
		              ",\"applied\":" + (derived ? "true" : "false") +
		              ",\"input_language\":\"profile scalar SQL subset (no core dialect lowering)\",\"declared_sql\":" +
		              JSONString(bundle.direct_derived ? selected->original_rule : bundle.original_rule) +
		              ",\"resolved_definition_sql\":" + JSONString(rule->ToString()) + "}";
	}
	string result =
	    "{\"schema_version\":" + JSONString(PopulationPlanSchema(bundle)) +
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
	    ",\"root\":" + JSONString(bundle.root) +
	    (bundle.direct_derived ? ",\"parent\":" + (selected ? JSONString(selected->parent) : "null") : string()) +
	    ",\"formal_rule\":" + formal_rule + "},\"identity\":" +
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
		result += "{\"ordinal\":" + to_string(i + 1) +
		          ",\"alias\":" + JSONString(expression->GetAlias().GetIdentifierName()) +
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
	if (bundle.direct_derived) {
		result.pop_back();
		result += ",\"constraint_phase\":\"load validation, not query revalidation\",\"selected_constraint_owners\":" +
		          JSONStrings(bundle.single_inheritance ? ConceptLineage(bundle, concept)
		                                                : (selected ? vector<string> {bundle.root, selected->name}
		                                                            : vector<string> {bundle.root})) +
		          ",\"concept_constraints\":" + (bundle.concept_requires ? ConceptConstraintsJSON(bundle) : "null") +
		          "}";
	}
	if (bundle.single_inheritance) {
		result.pop_back();
		result +=
		    ",\"lineage\":" + JSONStrings(ConceptLineage(bundle, concept)) +
		    ",\"membership_semantics\":\"parent population intersection; every lineage rule TRUE\",\"applied_rules\":[";
		bool first = true;
		for (auto &name : ConceptLineage(bundle, concept)) {
			if (name == bundle.root) {
				continue;
			}
			auto child = DirectChild(bundle, name);
			auto resolved = child->rule->Copy();
			EngineFunctions(*resolved);
			if (!first) {
				result += ",";
			}
			first = false;
			result += "{\"owner\":" + JSONString(name) + ",\"parent\":" + JSONString(child->parent) +
			          ",\"declared_sql\":" + JSONString(child->original_rule) +
			          ",\"resolved_definition_sql\":" + JSONString(resolved->ToString()) + ",\"applied\":true}";
		}
		result += "]}";
	}
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
	    pos != text.size() || special || !value.IsFinite() || Date::ToString(value) != text) {
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
	bool program = false;
	bool discover = false;
	bool encoded = false;
	bool objects = false;
	bool daily = false;
	bool daily_compile = false;
	bool daily_states = false;
	bool fact_query = false;
	bool fact_compile = false;
	bool history_query = false, history_compile = false;
	HistoryRequest history_request;
	FactRequest fact_request;
	unique_ptr<SelectStatement> fact_domain_statement;
	bool relation_objects = false;
	bool relation_compile = false;
	idx_t relation_index = 0;
	DailyRequest daily_request;
	vector<LogicalType> daily_input_types;
	string concept;
	vector<LogicalType> result_types;
	vector<string> result_names;
};

enum class Kind {
	OBJECTS,
	METRICS,
	DAILY_USERS,
	DAILY_STATES,
	RELATION_LINKS,
	FACT_USERS,
	FACT_WINDOWS,
	HISTORY_LINKS
};

const char *KindName(Kind kind) {
	switch (kind) {
	case Kind::OBJECTS:
		return "objects";
	case Kind::METRICS:
		return "metrics";
	case Kind::DAILY_USERS:
		return "daily_users";
	case Kind::DAILY_STATES:
		return "daily_states";
	case Kind::RELATION_LINKS:
		return "relation_links";
	case Kind::FACT_USERS:
		return "fact_users";
	case Kind::FACT_WINDOWS:
		return "fact_windows";
	default:
		return "history_links";
	}
}

struct Request {
	Kind kind = Kind::OBJECTS;
	bool compile = false;
	bool encoded = true;
	string operation;
	string concept;
	vector<string> metrics, dimensions, filters;
	bool include_trace = false;
	string request_json;
	string echo_json;
};

vector<string> StringArray(yyjson_val *value, const string &name) {
	vector<string> result;
	if (!value) {
		return result;
	}
	if (!yyjson_is_arr(value)) {
		Fail(name, "arguments", "JSON array of strings required");
	}
	size_t index, maximum;
	yyjson_val *item;
	yyjson_arr_foreach(value, index, maximum, item) {
		result.push_back(DailyString(item, name + "[" + to_string(index) + "]"));
	}
	return result;
}

string WithoutKeys(yyjson_doc *doc, const vector<string> &keys) {
	auto copy = yyjson_doc_mut_copy(doc, nullptr);
	if (!copy) {
		Fail("request", "arguments", "request copy failed");
	}
	std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> owner(copy, yyjson_mut_doc_free);
	auto root = yyjson_mut_doc_get_root(copy);
	for (auto &key : keys) {
		yyjson_mut_obj_remove_key(root, key.c_str());
	}
	size_t length = 0;
	auto text = yyjson_mut_write(copy, 0, &length);
	if (!text) {
		Fail("request", "arguments", "request serialization failed");
	}
	string result(text, length);
	free(text);
	return result;
}

Request ParseUnifiedRequest(const string &bytes, bool compile, const string &operation) {
	if (bytes.size() > DAILY_REQUEST_BYTES) {
		Fail(operation + ".request_json_utf8_bytes", "resource", "complete request exceeds 16KiB");
	}
	auto doc = yyjson_read(bytes.data(), bytes.size(), 0);
	if (!doc) {
		Fail(operation, "arguments", "strict JSON object request required");
	}
	std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> owner(doc, yyjson_doc_free);
	auto root = yyjson_doc_get_root(doc);
	if (!yyjson_is_obj(root)) {
		Fail(operation, "arguments", "strict JSON object request required");
	}
	Request r;
	r.compile = compile;
	r.operation = operation;
	r.echo_json = bytes;
	auto kind = DailyString(yyjson_obj_get(root, "kind"), "kind");
	auto required = [&](const char *name) {
		auto value = DailyString(yyjson_obj_get(root, name), name);
		if (value.empty()) {
			Fail(name, "arguments", "nonempty string required");
		}
		return value;
	};
	auto trace = [&]() {
		auto value = yyjson_obj_get(root, "include_trace");
		if (value && !yyjson_is_bool(value)) {
			Fail("include_trace", "arguments", "boolean required");
		}
		return value && yyjson_get_bool(value);
	};
	if (kind == "objects") {
		r.kind = Kind::OBJECTS;
		DailyKeys(root, {"kind", "concept", "filters", "include_trace"});
		r.concept = required("concept");
		r.filters = StringArray(yyjson_obj_get(root, "filters"), "filters");
		r.include_trace = trace();
	} else if (kind == "metrics") {
		r.kind = Kind::METRICS;
		DailyKeys(root, {"kind", "concept", "metrics", "dimensions", "filters", "include_trace"});
		r.concept = required("concept");
		r.metrics = StringArray(yyjson_obj_get(root, "metrics"), "metrics");
		r.dimensions = StringArray(yyjson_obj_get(root, "dimensions"), "dimensions");
		r.filters = StringArray(yyjson_obj_get(root, "filters"), "filters");
		r.include_trace = trace();
	} else if (kind == "daily_users" || kind == "daily_states") {
		r.kind = kind == "daily_users" ? Kind::DAILY_USERS : Kind::DAILY_STATES;
		r.concept = required("concept");
		r.request_json = WithoutKeys(doc, {"kind", "concept"});
	} else if (kind == "relation_links") {
		r.kind = Kind::RELATION_LINKS;
		DailyKeys(root, {"kind", "relationship"});
		r.concept = required("relationship");
	} else if (kind == "fact_users" || kind == "fact_windows") {
		r.kind = kind == "fact_users" ? Kind::FACT_USERS : Kind::FACT_WINDOWS;
		r.request_json = WithoutKeys(doc, {"kind"});
	} else if (kind == "history_links") {
		r.kind = Kind::HISTORY_LINKS;
		r.concept = required("relationship");
		r.request_json = WithoutKeys(doc, {"kind", "relationship"});
	} else {
		Fail("kind", "arguments",
		     "objects, metrics, daily_users, daily_states, relation_links, fact_users, fact_windows or history_links "
		     "required");
	}
	return r;
}

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
		Fail(operation, "capability", "history profile accepts only history_links requests");
	}
	if (bundle->profile == FACT_PROFILE) {
		Fail(operation, "capability", "multi-fact program accepts only fact_users and fact_windows requests");
	}
	if (bundle->profile == RELATION_PROFILE) {
		Fail(operation, "capability", "relation program profile accepts only relation_links requests");
	}
	return bundle;
}

unique_ptr<FunctionData> FactBind(ClientContext &context, const Request &request, vector<LogicalType> &types,
                                  vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = Current(context);
	auto &b = *result->bundle;
	if (!b.fact_program) {
		Fail("fact_program", "capability", "multi-fact-user-program-v1 required");
	}
	bool window = request.kind == Kind::FACT_WINDOWS;
	result->fact_compile = request.compile;
	result->fact_query = true;
	result->fact_request = ParseFactRequest(request.request_json, *b.fact_program, window);
	result->fact_request.bytes = request.echo_json;
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
	result->result_names = Names(prepared->GetNames());
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

unique_ptr<FunctionData> RelationBind(ClientContext &context, const Request &request, vector<LogicalType> &types,
                                      vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = Current(context);
	auto &b = *result->bundle;
	if (!b.relation_program) {
		Fail("relation", "capability", "relation program profile required");
	}
	auto &name = request.concept;
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
	result->relation_compile = request.compile;
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
		if (query->GetTypes() != data.result_types) {
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

unique_ptr<FunctionData> DailyBind(ClientContext &context, const Request &request, vector<LogicalType> &types,
                                   vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = Current(context);
	result->daily_states = request.kind == Kind::DAILY_STATES;
	result->concept = request.concept;
	auto &bundle = *result->bundle;
	if (bundle.profile != OBSERVATION_PROFILE || result->concept != bundle.derived) {
		Fail("daily_user_set", "capability", "current registered qualified daily observation concept required");
	}
	result->daily_request = ParseDailyRequest(request.request_json, result->daily_states);
	result->daily = true;
	result->daily_compile = request.compile;
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

vector<string> ConceptColumns(bool v2) {
	if (v2) {
		return {"concept",
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
	}
	return {"concept",         "parent",         "identity_field", "derived_by",      "profile",
	        "snapshot_id",     "content_sha256", "core_sha256",    "ontology_sha256", "mapping_sha256",
	        "manifest_sha256", "source",         "records"};
}

void ConceptRows(const Bundle &b, bool v2, vector<vector<Value>> &rows) {
	vector<string> concepts =
	    b.direct_derived ? ExecutableConcepts(b)
	                     : (v2 && b.profile == OBSERVATION_PROFILE ? vector<string> {b.user_concept, b.root, b.derived}
	                                                               : vector<string> {b.root, b.derived});
	for (const auto &concept : concepts) {
		auto selected = b.direct_derived ? DirectChild(b, concept) : nullptr;
		bool derived = b.direct_derived ? selected != nullptr : concept == b.derived;
		auto &original_rule = selected ? selected->original_rule : b.original_rule;
		if (v2) {
			bool user = concept == b.user_concept;
			rows.push_back(
			    {Value(concept), derived ? Value(selected ? selected->parent : b.root) : Value(LogicalType::VARCHAR),
			     derived ? Value(original_rule) : Value(LogicalType::VARCHAR), Value(IdentityComponents(b, user)),
			     b.profile == OBSERVATION_PROFILE && !user ? Value(ReferencedUserIdentity(b))
			                                               : Value(LogicalType::VARCHAR),
			     Value::BOOLEAN(!user), Value::BOOLEAN(!user), Value::BIGINT(static_cast<int64_t>(b.rows)),
			     Value::BIGINT(static_cast<int64_t>(b.profile == OBSERVATION_PROFILE ? b.user_count : b.rows)),
			     Value(b.profile), Value(b.original_source), Value(b.snapshot_id), Value(b.content_hash),
			     Value(b.core_hash), Value(b.ontology_hash), Value(b.mapping_hash), Value(b.manifest_hash)});
			continue;
		}
		rows.push_back(
		    {Value(concept), derived ? Value(selected ? selected->parent : b.root) : Value(LogicalType::VARCHAR),
		     Value(OriginalName(b, b.identity)), derived ? Value(original_rule) : Value(LogicalType::VARCHAR),
		     Value(b.profile), Value(b.snapshot_id), Value(b.content_hash), Value(b.core_hash), Value(b.ontology_hash),
		     Value(b.mapping_hash), Value(b.manifest_hash), Value(b.original_source),
		     Value::BIGINT(static_cast<int64_t>(b.rows))});
	}
}

unique_ptr<FunctionData> DiscoverBind(ClientContext &context, vector<LogicalType> &types, vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = LegacyCurrent(context, "ossie_concepts");
	result->discover = true;
	if (result->bundle->profile == OBSERVATION_PROFILE) {
		Fail("ossie_concepts", "capability", "compound identity requires ossie_program");
	}
	names = ConceptColumns(false);
	types = vector<LogicalType>(12, LogicalType::VARCHAR);
	types.push_back(LogicalType::BIGINT);
	return std::move(result);
}

// Versioned separately from the Ossie document and diagnostic plan schema.
constexpr const char *POPULATION_API_VERSION = "2.2.0";
constexpr const char *CONCEPT_REQUIRES_API_VERSION = "2.3.0";
constexpr const char *STRING_JOIN_API_VERSION = "2.4.0";
constexpr const char *DIRECT_DERIVED_API_VERSION = "2.5.0";
constexpr const char *SINGLE_INHERITANCE_API_VERSION = "2.6.0";

vector<string> VocabularyColumns(const string &vocabulary) {
	vector<string> names;
	if (vocabulary == "metrics") {
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
	} else if (vocabulary == "fields") {
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
	} else {
		names = {"api_version", "discovery_schema", "plan_schema", "ossie_spec_version", "capabilities_json"};
	}
	for (auto name : {"profile", "snapshot_id", "content_sha256", "core_sha256", "ontology_sha256", "mapping_sha256",
	                  "manifest_sha256"}) {
		names.push_back(name);
	}
	return names;
}

void VocabularyRows(const Bundle &b, const string &vocabulary, vector<vector<Value>> &rows) {
	auto append = [&](vector<Value> row) {
		for (const auto &value : {b.profile, b.snapshot_id, b.content_hash, b.core_hash, b.ontology_hash,
		                          b.mapping_hash, b.manifest_hash}) {
			row.push_back(Value(value));
		}
		rows.push_back(std::move(row));
	};
	if (vocabulary == "metrics") {
		for (auto &m : b.definition_model.metrics) {
			append({Value(m.name), Value(m.datatype), Value(b.metric_types.at(m.name)), Value(m.description),
			        Value(m.expression.sql), Value(m.expression.dialect),
			        Value(JSONStrings(m.expression.available_dialects)), Value(JSONStrings(m.ai_context.synonyms)),
			        Value(JSONStrings(m.ai_context.examples)), Value(m.ai_context.instructions),
			        Value(m.ai_context.raw_json), Value(m.custom_extensions_json), Value::BOOLEAN(true),
			        Value::BOOLEAN(false)});
		}
	} else if (vocabulary == "fields") {
		for (auto &dataset : b.definition_model.datasets) {
			for (auto &f : dataset.fields) {
				auto qualified =
				    make_uniq<ColumnRefExpression>(Identifier(f.name), Identifier(dataset.name))->ToString();
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
		    "{\"scope\":\"bound frozen bundle\",\"concepts\":" + JSONStrings(ExecutableConcepts(b)) +
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
		    "\"mcp_example\":{\"request_encoding\":\"one JSON object with "
		    "kind\",\"exact_numeric_transport_verified\":false,"
		    "\"result_schema\":\"ossie-population-result-v1\",\"numeric_encoding\":\"typed strings\","
		    "\"transport_size_and_truncation_verified\":\"fixed fixtures only\"},"
		    "\"uniform_errors\":true,\"error_contract\":{\"schema\":\"ossie-population-error-v1\","
		    "\"scope\":\"recoverable population callback errors\","
		    "\"external_boundaries\":[\"SQL signature/conversion before callbacks\",\"outer table output binding\","
		    "\"MCP missing argument/unknown tool/protocol\",\"cancellation/fatal/internal/OOM/unknown native "
		    "failures\"]},"
		    "\"arbitrary_ontology_execution\":false,\"cross_call_reload_atomicity\":false}";
		capabilities.pop_back();
		if (b.concept_requires) {
			capabilities += ",\"concept_constraints\":" + ConceptConstraintsJSON(b);
		}
		if (b.string_join) {
			capabilities += ",\"typed_join\":{\"schema_version\":\"ossie-native-string-join-v1\",\"capability\":" +
			                JSONString(STRING_JOIN) + ",\"actual_key_type\":" + JSONString(b.join_key_type) +
			                ",\"native_key_pairs\":[\"INTEGER/INTEGER\",\"VARCHAR/VARCHAR\"],"
			                "\"string_equality\":\"raw UTF-8 bytes\",\"normalization\":\"none\","
			                "\"empty_string\":\"valid nonnull key\",\"embedded_NUL\":\"length-preserved bytes\","
			                "\"collations\":\"rejected\",\"cardinality\":\"ManyToOne\",\"composite_keys\":false,"
			                "\"validation_scope\":\"entire captured inputs before filters\","
			                "\"dimension_key\":\"nonnull unique including unused rows\","
			                "\"fact_foreign_key\":\"nonnull complete match\",\"live_query_join\":false}";
		}
		if (b.direct_derived && !b.single_inheritance) {
			capabilities +=
			    ",\"direct_derived\":{\"schema_version\":\"ossie-direct-derived-concepts-v1\",\"capability\":" +
			    JSONString(DIRECT_DERIVED) + ",\"root\":" + JSONString(b.root) +
			    ",\"max_children\":8,\"max_rules_total_bytes\":131072,\"max_requires_total\":16,\"shared_identity\":"
			    "true,\"shared_mapping\":true,\"overlap_allowed\":true,\"exhaustive\":false,\"sibling_references\":"
			    "false,\"multilevel\":false,\"multiple_inheritance\":false,\"children\":[";
			for (idx_t i = 0; i < b.direct_concepts.size(); i++) {
				auto &child = b.direct_concepts[i];
				if (i) {
					capabilities += ",";
				}
				capabilities += "{\"name\":" + JSONString(child.name) + ",\"parent\":" + JSONString(b.root) +
				                ",\"declared_rule\":" + JSONString(child.original_rule) + "}";
			}
			capabilities += "]}";
		}
		if (b.single_inheritance) {
			capabilities +=
			    ",\"single_inheritance\":{\"schema_version\":\"ossie-bounded-single-inheritance-v1\",\"capability\":" +
			    JSONString(SINGLE_INHERITANCE) + ",\"root\":" + JSONString(b.root) +
			    ",\"max_derived\":8,\"max_depth_edges\":3,\"max_rules_total_bytes\":131072,\"max_requires_total\":16,"
			    "\"lineage_predicate_nodes\":4096,\"lineage_predicate_depth\":40,\"shared_identity\":true,\"shared_"
			    "mapping\":true,\"overlap_allowed\":true,\"exhaustive\":false,\"ancestor_attribute_aliases\":true,"
			    "\"sibling_references\":false,\"descendant_references\":false,\"multiple_inheritance\":false,\"nodes\":"
			    "[{\"name\":" +
			    JSONString(b.root) + ",\"parent\":null,\"depth\":0,\"declared_rule\":null}";
			for (auto &child : b.direct_concepts) {
				capabilities += ",{\"name\":" + JSONString(child.name) + ",\"parent\":" + JSONString(child.parent) +
				                ",\"depth\":" + to_string(child.depth) +
				                ",\"declared_rule\":" + JSONString(child.original_rule) + "}";
			}
			capabilities += "]}";
		}
		capabilities += ",\"concept_discovery\":{\"function\":\"ossie_program\",\"schema\":\"ossie-program-v1\"}";
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
		    ",\"sql_function\":\"ossie_population_json(VARCHAR)\",\"mcp_example_registered\":false,"
		    "\"request_schema\":\"ossie-daily-users-request-v1\","
		    "\"result_schema\":\"ossie-daily-user-set-result-v1\",\"compile_supported\":true,\"compile_function\":"
		    "\"ossie_population_compile(VARCHAR)\",\"plan_schema\":\"ossie-daily-user-set-plan-v1\","
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
		auto api_version = b.concept_requires ? CONCEPT_REQUIRES_API_VERSION : POPULATION_API_VERSION;
		if (b.string_join) {
			api_version = STRING_JOIN_API_VERSION;
		}
		if (b.direct_derived) {
			api_version = b.single_inheritance ? SINGLE_INHERITANCE_API_VERSION : DIRECT_DERIVED_API_VERSION;
		}
		append({Value(api_version), Value("ossie-population-discovery-v1"), Value(PopulationPlanSchema(b)),
		        Value(b.definition_model.spec_version), Value(capabilities)});
	}
}

unique_ptr<FunctionData> RequestBind(ClientContext &context, const Request &request, vector<LogicalType> &types,
                                     vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = LegacyCurrent(context, request.operation);
	auto &concept = request.concept;
	result->concept = concept;
	bool objects = request.kind == Kind::OBJECTS;
	auto &metrics = request.metrics;
	auto &dimensions = request.dimensions;
	auto &filters = request.filters;
	if (metrics.size() > 64 || dimensions.size() > 256) {
		Fail(concept, "capability", "request exceeds 64 metrics or 256 dimensions");
	}
	result->compile = request.compile;
	result->encoded = request.encoded;
	// Empty metrics explicitly select the object path for the compile endpoint only.
	objects |= result->compile && metrics.empty();
	result->objects = objects;
	if (objects && !dimensions.empty()) {
		Fail(concept, "bind", "object compile does not accept dimensions");
	}
	lock_guard<mutex> guard(result->bundle->query_lock);
	result->statement = QueryAST(*result->bundle, concept, metrics, dimensions, filters, objects);
	if (request.include_trace) {
		auto &select = result->statement->node->Cast<SelectNode>();
		for (const auto &entry : {std::make_pair("_ossie_snapshot_id", result->bundle->snapshot_id),
		                          std::make_pair("_ossie_content_sha256", result->bundle->content_hash),
		                          std::make_pair("_ossie_ontology_sha256", result->bundle->ontology_hash)}) {
			auto expression = ConstantExpression::FromValue(Value(entry.second));
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
		result->plan_json.pop_back();
		result->plan_json += ",\"sql\":" + JSONString(result->sql) +
		                     ",\"execution_target\":" + JSONString(result->bundle->execution_target) + "}";
		types = {LogicalType::VARCHAR};
		names = {"plan_json"};
	} else {
		auto engine = Engine(*result->bundle);
		auto prepared = engine->Prepare(result->statement->Copy());
		if (prepared->HasError()) {
			NormalizeError(prepared->GetErrorObject(), concept, "bind", ExceptionType::INVALID_INPUT);
		}
		types = prepared->GetTypes();
		names = Names(prepared->GetNames());
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
	json.Append(",\"functions\":[\"ossie_population_json\",\"ossie_population_compile\",\"ossie_program\"],"
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
	if (query.GetTypes() != data.daily_input_types) {
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
	if (query.GetTypes() != data.result_types || Names(query.GetNames()) != data.result_names) {
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
	for (idx_t col = 0; col < query.GetTypes().size(); col++) {
		if (col) {
			json += ",";
		}
		json += "{\"ordinal\":" + to_string(col + 1) +
		        ",\"name\":" + JSONString(query.GetNames()[col].GetIdentifierName()) +
		        ",\"bound_type\":" + JSONString(query.GetTypes()[col].ToString()) +
		        ",\"encoding\":" + JSONString(ResultEncoding(query.GetTypes()[col])) + "}";
	}
	json += "],\"rows\":[";
	idx_t count = 0;
	while (auto chunk = query.Fetch()) {
		if (chunk->ColumnCount() != query.GetTypes().size()) {
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
			for (idx_t col = 0; col < query.GetTypes().size(); col++) {
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
string FactProgramJSON(ClientContext &context, const Bundle &b) {
	auto &p = *b.fact_program;
	DailyJSONWriter w(context, "program_json_utf8_bytes", "fact_program");
	w.Append("{\"schema_version\":\"ossie-fact-program-v1\"");
	FactIdentityJSON(w, b);
	w.Field("project_api_version", POPULATION_API_VERSION);
	w.Append(",\"query_executed\":false,\"counts_computed\":false,\"mcp_registered\":false,\"functions\":[\"ossie_"
	         "population_json\",\"ossie_population_compile\",\"ossie_program\"],"
	         "\"sources\":[");
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
		auto &qualified = window.expression->Cast<WindowExpression>().GetQualifiedName();
		w.Field("bound_catalog", qualified.Catalog().GetIdentifierName());
		w.Field("bound_schema", qualified.Schema().GetIdentifierName());
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
			if (count->GetTypes() != vector<LogicalType> {LogicalType::BIGINT} || count->RowCount() != 1 ||
			    count->Collection().GetValue(0, 0).IsNull()) {
				Fail("common_user_count", "execute", "one nonNULL BIGINT count required");
			}
			auto value = count->Collection().GetValue(0, 0).GetValue<int64_t>();
			if (value < 0 || value > int64_t(MAX_ROWS)) {
				Fail("common_user_count", "execute", "common domain count outside bounded source");
			}
			common = idx_t(value);
			ObserveFact(context, PopulationFactCheckpoint::AFTER_DOMAIN);
		}
		auto query = Run(context, *engine, data.statement->Copy());
		if (query->GetTypes() != data.result_types || Names(query->GetNames()) != data.result_names) {
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
				    (r.window && (values[1].IsNull() || !values[1].GetValue<date_t>().IsFinite()))) {
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
unique_ptr<FunctionData> HistoryBind(ClientContext &context, const Request &request, vector<LogicalType> &types,
                                     vector<string> &names) {
	auto data = make_uniq<PopulationBind>();
	data->bundle = Current(context);
	auto &b = *data->bundle;
	if (b.profile != HISTORY_PROFILE) {
		Fail("history", "capability", "historical-observation profile required");
	}
	if (request.concept != b.history_relationship) {
		Fail("history", "arguments", "exact declared historical relationship required");
	}
	data->history_query = true;
	data->history_compile = request.compile;
	data->history_request = ParseHistoryRequest(request.request_json);
	auto &r = data->history_request;
	auto &obs = b.history_sources[0];
	auto observation = Quote(obs.dataset);
	auto user = observation + "." + HistoryRoleColumn(b, 0, "user");
	auto date = observation + "." + HistoryRoleColumn(b, 0, "date");
	auto product = observation + "." + HistoryRoleColumn(b, 0, "product");
	data->statement =
	    ProgramSQL(b, {b.history_children[0], b.history_segments},
	               "SELECT " + user + ", " + date + ", " + product + ", seg.target FROM " + observation +
	                   " ASOF JOIN " + Quote(HISTORY_SEGMENTS) + " AS seg ON " + user + " = seg.user_id AND " + date +
	                   " >= seg.starts WHERE " + date + " < seg.ends ORDER BY 1, 2, 3, 4");
	auto &select = data->statement->node->Cast<SelectNode>();
	auto column = [&](const string &role) {
		auto &ds = *b.model.FindDataset(obs.dataset);
		auto position = std::find(obs.roles.begin(), obs.roles.end(), role) - obs.roles.begin();
		return make_uniq<ColumnRefExpression>(Identifier(FactPhysicalField(ds, *ds.FindField(obs.fields[position]))),
		                                      Identifier(obs.dataset));
	};
	auto conjoin = [&](unique_ptr<ParsedExpression> predicate) {
		select.where_clause = make_uniq<ConjunctionExpression>(ExpressionType::CONJUNCTION_AND,
		                                                       std::move(select.where_clause), std::move(predicate));
	};
	if (!r.all_dates) {
		conjoin(make_uniq<ComparisonExpression>(ExpressionType::COMPARE_GREATERTHANOREQUALTO, column("date"),
		                                        ConstantExpression::FromValue(Value::DATE(r.start))));
		conjoin(make_uniq<ComparisonExpression>(ExpressionType::COMPARE_LESSTHANOREQUALTO, column("date"),
		                                        ConstantExpression::FromValue(Value::DATE(r.end))));
	}
	if (!r.all_products) {
		auto in = make_uniq<OperatorExpression>(ExpressionType::COMPARE_IN);
		in->GetChildrenMutable().push_back(column("product"));
		for (auto &item : r.products) {
			in->GetChildrenMutable().push_back(ConstantExpression::String(item));
		}
		conjoin(std::move(in));
	}
	lock_guard<mutex> guard(b.query_lock);
	auto engine = Engine(b);
	auto prepared = engine->Prepare(data->statement->Copy());
	if (prepared->HasError()) {
		NormalizeError(prepared->GetErrorObject(), "history", "bind", ExceptionType::INVALID_INPUT);
	}
	data->result_types = prepared->GetTypes();
	if (data->result_types !=
	    vector<LogicalType> {LogicalType::VARCHAR, LogicalType::DATE, LogicalType::VARCHAR, LogicalType::VARCHAR}) {
		Fail("history", "bind", "actual link projection must be User/DATE/product/target VARCHAR/DATE/VARCHAR/VARCHAR");
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
	json.Field("history_record_count", to_string(b.inputs[1].rows));
	json.Field("history_segment_count", to_string(b.history_segment_count));
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
		            "binding\":true,\"input_types\":[\"VARCHAR\",\"DATE\",\"VARCHAR\"],\"link_sql\":");
		json.Quoted(data.statement->ToString());
		json.Append(
		    ",\"operators\":[\"frozen full observation source\",\"frozen merged per-User target segments\",\"explicit "
		    "observation date/product scope predicates\",\"ASOF JOIN on raw User and observation date >= segment "
		    "start\",\"start inclusive and end exclusive; gap no link\",\"complete Observation to typed target "
		    "set\",\"raw User/product/target bytes and DATE value ordering\",\"complete bounded "
		    "JSON\"],\"limits\":{\"links\":10000,\"request_utf8_bytes\":16384,\"complete_json_utf8_bytes\":1048576},"
		    "\"limitations\":[\"Compile binds the link statement but never executes it\",\"Input and segment counts "
		    "are load metadata\",\"Rendered SQL names frozen snapshots that only the bound statement can reach, so it "
		    "cannot replay independently\",\"Compile does not guarantee execution completion or encoded result "
		    "size\"]}");
		return json.Finish();
	}
	struct Link {
		string user, product, target;
		date_t date;
	};
	vector<Link> links;
	ObserveHistory(context, PopulationHistoryCheckpoint::BEFORE_EXECUTE);
	{
		lock_guard<mutex> guard(b.query_lock);
		auto engine = Engine(b);
		auto query = Run(context, *engine, data.statement->Copy());
		if (query->GetTypes() != data.result_types) {
			Fail("history", "execute", "link projection types changed");
		}
		while (auto chunk = query->Fetch()) {
			for (idx_t row = 0; row < chunk->size(); row++) {
				if (context.IsInterrupted()) {
					throw InterruptException();
				}
				if (links.size() >= MAX_ROWS) {
					Fail("history.links", "resource", "complete link count exceeds10000");
				}
				auto user = chunk->GetValue(0, row), date = chunk->GetValue(1, row), product = chunk->GetValue(2, row),
				     target = chunk->GetValue(3, row);
				if (user.IsNull() || date.IsNull() || product.IsNull() || target.IsNull()) {
					Fail("history", "execute", "frozen identity changed to NULL");
				}
				links.push_back({user.GetValue<string>(), product.GetValue<string>(), target.GetValue<string>(),
				                 date.GetValue<date_t>()});
			}
		}
		if (query->HasError()) {
			NormalizeError(query->GetErrorObject(), "history", "execute", ExceptionType::INVALID_INPUT);
		}
	}
	ObserveHistory(context, PopulationHistoryCheckpoint::BEFORE_SORT);
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
		json.Quoted(l.target);
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
	json.Field("history_record_count", to_string(b.inputs[1].rows));
	json.Field("history_segment_count", to_string(b.history_segment_count));
	json.Field("history_User_count", to_string(b.history_user_count));
	json.Append(",\"functions\":[\"ossie_program\",\"ossie_population_json\",\"ossie_population_compile\"],"
	            "\"query_limits\":{\"links\":10000,\"request_utf8_bytes\":16384,\"subset_products\":64,"
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
	    "\"combined_canonical_bytes\":16777216,\"history_segments\":10000,\"complete_json_utf8_bytes\":1048576},"
	    "\"validity\":\"finite DuckDB DATE [start,end), start<end\",\"conflict\":\"whole-history same User "
	    "different-target overlap rejects entire load\",\"source_identity\":\"global raw record_id, duplicates "
	    "rejected\",\"same_target_overlap\":\"distinct sources retained; derived coverage merged without "
	    "gaps\",\"link_identity\":[\"User\",\"DATE\",\"product\",\"target_id\"],\"load_metadata_not_query_counts\":"
	    "true}");
	return json.Finish();
}

string RelationProgramJSON(ClientContext &context, const Bundle &b) {
	auto &program = *b.relation_program;
	vector<string> identity_types;
	for (auto &field : b.observation_identity) {
		identity_types.push_back(b.field_types.at(field));
	}
	string json =
	    "{\"schema_version\":\"ossie-relation-program-v1\",\"profile\":" + JSONString(b.profile) +
	    ",\"snapshot_id\":" + JSONString(b.snapshot_id) + ",\"content_sha256\":" + JSONString(b.content_hash) +
	    ",\"core_sha256\":" + JSONString(b.core_hash) + ",\"ontology_sha256\":" + JSONString(b.ontology_hash) +
	    ",\"mapping_sha256\":" + JSONString(b.mapping_hash) + ",\"manifest_sha256\":" + JSONString(b.manifest_hash) +
	    ",\"root\":" + JSONString(program.root) + ",\"target_concept\":" + JSONString(program.target_concept) +
	    ",\"mapped_user_relation\":" + JSONString(program.mapped_user_relation) +
	    ",\"root_identity_fields\":" + JSONStrings(b.observation_identity) +
	    ",\"root_identity_bound_types\":" + JSONStrings(identity_types) +
	    ",\"captured_rows\":" + JSONString(to_string(b.rows)) +
	    ",\"source_types_bound\":true,\"load_constraints_validated\":true,\"query_executed\":false,"
	    "\"root_constraints\":" +
	    JSONStrings(b.relation_input.requires_list) + ",\"relations\":[";
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
	return json;
}

// Columns named *_json carry pre-encoded JSON and are embedded without the suffix; the bundle
// identity columns repeat the program header and are skipped.
string RowsJSON(const vector<string> &columns, const vector<vector<Value>> &rows) {
	static const vector<string> HEADER = {"profile",         "snapshot_id",    "content_sha256", "core_sha256",
	                                      "ontology_sha256", "mapping_sha256", "manifest_sha256"};
	string json = "[";
	for (idx_t r = 0; r < rows.size(); r++) {
		if (r) {
			json += ",";
		}
		json += "{";
		bool first = true;
		for (idx_t c = 0; c < columns.size(); c++) {
			auto name = columns[c];
			if (std::find(HEADER.begin(), HEADER.end(), name) != HEADER.end()) {
				continue;
			}
			if (!first) {
				json += ",";
			}
			first = false;
			auto &value = rows[r][c];
			bool raw = StringUtil::EndsWith(name, "_json");
			if (raw) {
				name = name.substr(0, name.size() - 5);
			}
			json += JSONString(name) + ":";
			if (value.IsNull() || (raw && StringValue::Get(value).empty())) {
				json += "null";
			} else if (raw) {
				json += StringValue::Get(value);
			} else if (value.type() == LogicalType::BOOLEAN) {
				json += value.GetValue<bool>() ? "true" : "false";
			} else if (value.type() == LogicalType::BIGINT) {
				json += to_string(value.GetValue<int64_t>());
			} else {
				json += JSONString(StringValue::Get(value));
			}
		}
		json += "}";
	}
	return json + "]";
}

string ProgramJSON(ClientContext &context, const Bundle &b) {
	vector<string> kinds;
	string program = "null";
	bool legacy = b.profile == PROFILE || b.profile == JOIN_PROFILE || b.profile == OBSERVATION_PROFILE;
	if (legacy) {
		kinds = {"objects", "metrics"};
	}
	if (b.profile == OBSERVATION_PROFILE) {
		kinds.push_back("daily_users");
		kinds.push_back("daily_states");
		program = DailyStateProgram(context, b);
	} else if (b.profile == RELATION_PROFILE) {
		kinds = {"relation_links"};
		program = RelationProgramJSON(context, b);
	} else if (b.profile == FACT_PROFILE) {
		kinds = {"fact_users", "fact_windows"};
		program = FactProgramJSON(context, b);
	} else if (b.profile == HISTORY_PROFILE) {
		kinds = {"history_links"};
		program = HistoryProgramJSON(context, b);
	}
	string json =
	    "{\"schema_version\":\"ossie-program-v1\",\"profile\":" + JSONString(b.profile) +
	    ",\"snapshot_id\":" + JSONString(b.snapshot_id) + ",\"content_sha256\":" + JSONString(b.content_hash) +
	    ",\"core_sha256\":" + JSONString(b.core_hash) + ",\"ontology_sha256\":" + JSONString(b.ontology_hash) +
	    ",\"mapping_sha256\":" + JSONString(b.mapping_hash) + ",\"manifest_sha256\":" + JSONString(b.manifest_hash) +
	    ",\"request_kinds\":" + JSONStrings(kinds) + ",\"query_executed\":false";
	if (legacy) {
		vector<vector<Value>> concepts, metrics, fields, capabilities;
		ConceptRows(b, true, concepts);
		VocabularyRows(b, "metrics", metrics);
		VocabularyRows(b, "fields", fields);
		VocabularyRows(b, "capabilities", capabilities);
		auto &row = capabilities[0];
		json += ",\"api_version\":" + JSONString(StringValue::Get(row[0])) +
		        ",\"discovery_schema\":" + JSONString(StringValue::Get(row[1])) +
		        ",\"plan_schema\":" + JSONString(StringValue::Get(row[2])) +
		        ",\"ossie_spec_version\":" + JSONString(StringValue::Get(row[3])) +
		        ",\"concepts\":" + RowsJSON(ConceptColumns(true), concepts) +
		        ",\"vocabulary\":{\"metrics\":" + RowsJSON(VocabularyColumns("metrics"), metrics) +
		        ",\"fields\":" + RowsJSON(VocabularyColumns("fields"), fields) +
		        "},\"capabilities\":" + StringValue::Get(row[4]);
	} else {
		json += ",\"api_version\":null,\"discovery_schema\":null,\"plan_schema\":null,\"ossie_spec_version\":null"
		        ",\"concepts\":null,\"vocabulary\":null,\"capabilities\":null";
	}
	json += ",\"program\":" + program + "}";
	if (json.size() > DAILY_RESULT_BYTES) {
		Fail("program", "resource", "complete program JSON exceeds 1MiB");
	}
	if (context.IsInterrupted()) {
		throw InterruptException();
	}
	return json;
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
	} else if (data.fact_query) {
		result->rows.push_back({Value(FactQueryJSON(context, data))});
	} else if (data.relation_objects) {
		result->rows.push_back({Value(RelationJSON(context, data))});
	} else if (data.discover) {
		ConceptRows(*data.bundle, false, result->rows);
	} else if (data.program) {
		result->rows.push_back({Value(ProgramJSON(context, *data.bundle))});
	} else if (data.daily_compile) {
		result->rows.push_back(
		    {Value(data.daily_states ? DailyStatePlan(context, data) : DailyPlanJSON(context, data))});
	} else if (data.compile) {
		result->rows.push_back({Value(data.plan_json)});
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
			output.data[col].Append(row[col]);
		}
		count++;
	}
	output.CheckCardinality(count);
}

unique_ptr<FunctionData> DispatchBind(ClientContext &context, const Request &request, vector<LogicalType> &types,
                                      vector<string> &names) {
	switch (request.kind) {
	case Kind::OBJECTS:
	case Kind::METRICS:
		return RequestBind(context, request, types, names);
	case Kind::DAILY_USERS:
	case Kind::DAILY_STATES:
		return DailyBind(context, request, types, names);
	case Kind::RELATION_LINKS:
		return RelationBind(context, request, types, names);
	case Kind::FACT_USERS:
	case Kind::FACT_WINDOWS:
		return FactBind(context, request, types, names);
	default:
		return HistoryBind(context, request, types, names);
	}
}

unique_ptr<FunctionData> ProgramBind(ClientContext &context, vector<LogicalType> &types, vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = Current(context);
	result->program = true;
	types = {LogicalType::VARCHAR};
	names = {"program_json"};
	return std::move(result);
}

string Argument(const TableFunctionBindInput &input, idx_t index, const string &object, const string &message) {
	if (input.inputs[index].IsNull()) {
		Fail(object, "arguments", message);
	}
	return input.inputs[index].GetValue<string>();
}

Request NativeRequest(const TableFunctionBindInput &input) {
	auto name = input.table_function.GetDefinition()->GetName().GetIdentifierName();
	Request r;
	r.operation = name;
	r.encoded = false;
	r.kind = name == "ossie_objects" ? Kind::OBJECTS : Kind::METRICS;
	r.concept = Argument(input, 0, "concept", "concept required");
	if (r.kind == Kind::METRICS) {
		r.metrics = ToStringList(input.inputs[1], "ossie_concept_query");
		r.dimensions = ToStringList(input.inputs[2], "ossie_concept_query");
	}
	r.filters = ToStringList(input.inputs.back(), "ossie_population");
	auto trace = input.named_parameters.find(Identifier("include_trace"));
	r.include_trace =
	    trace != input.named_parameters.end() && !trace->second.IsNull() && trace->second.GetValue<bool>();
	return r;
}

unique_ptr<FunctionData> BindBoundary(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                      vector<Identifier> &identifiers) {
	auto name = input.table_function.GetDefinition()->GetName().GetIdentifierName();
	string object = name;
	if (!input.inputs.empty() && !input.inputs[0].IsNull() && input.inputs[0].type() == LogicalType::VARCHAR) {
		object = input.inputs[0].GetValue<string>();
	}
	vector<string> names;
	auto bound = ErrorBoundary(object, "bind", [&]() {
		if (name == "ossie_population_json" || name == "ossie_population_compile") {
			auto request = ParseUnifiedRequest(Argument(input, 0, name, "request_json required"),
			                                   name == "ossie_population_compile", name);
			return DispatchBind(context, request, types, names);
		}
		if (name == "ossie_program") {
			return ProgramBind(context, types, names);
		}
		if (name == "ossie_bundle_load") {
			return LoadBind(context, input, types, names);
		}
		if (name == "ossie_concepts") {
			return DiscoverBind(context, types, names);
		}
		return DispatchBind(context, NativeRequest(input), types, names);
	});
	identifiers = Identifiers(names);
	return bound;
}

unique_ptr<GlobalTableFunctionState> InitBoundary(ClientContext &context, TableFunctionInitInput &input) {
	auto &data = input.bind_data->Cast<PopulationBind>();
	return ErrorBoundary(data.load ? data.path : "query", data.load ? "load" : "execute",
	                     [&]() { return Init(context, input); });
}

void ScanBoundary(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	ErrorBoundary("query", "execute", [&]() { Scan(context, input, output); });
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
	RegisterDescribe(
	    loader, TableFunction("ossie_program", {}, ScanBoundary, BindBoundary, InitBoundary),
	    "Discover the current frozen population bundle as one program_json: profile, snapshot and definition "
	    "hashes, accepted request kinds, typed concepts, vocabulary and capabilities for scalar/join/observation "
	    "profiles, and the dedicated program section for daily-state, relation, multi-fact and history profiles. "
	    "No query execution; max1MiB complete JSON.",
	    "SELECT * FROM ossie_program()");
	auto named = [](TableFunction function, const vector<string> &names) {
		NameParameters(function.GetSignature(), names);
		return function;
	};
	for (bool compile : {false, true}) {
		auto name = compile ? "ossie_population_compile" : "ossie_population_json";
		RegisterDescribe(
		    loader,
		    named(TableFunction(name, {LogicalType::VARCHAR}, ScanBoundary, BindBoundary, InitBoundary),
		          {"request_json"}),
		    compile ? "Bind one population request without executing it and return its plan_json. request_json "
		              "declares kind (objects, metrics, daily_users, daily_states, relation_links, fact_users, "
		              "fact_windows, history_links) plus that kind's request fields; the profile must support the "
		              "kind. Never scans or counts; max16KiB request/1MiB complete JSON."
		            : "Execute one population request against the frozen bundle and return its result_json. "
		              "request_json declares kind (objects, metrics, daily_users, daily_states, relation_links, "
		              "fact_users, fact_windows, history_links) plus that kind's request fields; the profile must "
		              "support the kind. Complete typed JSON, no paging or truncation; max16KiB request/1MiB JSON.",
		    string("SELECT * FROM ") + name + "('{\"kind\":\"objects\",\"concept\":\"ValidUser\",\"filters\":[]}')");
	}
	loader.RegisterFunction(
	    TableFunction("_ossie_snapshot_rows", {}, FrozenScanFunction, FrozenBindFunction, FrozenInit));
	RegisterDescribe(
	    loader,
	    named(TableFunction("ossie_bundle_load", {LogicalType::VARCHAR}, ScanBoundary, BindBoundary, InitBoundary),
	          {"path"}),
	    "Load a hash-bound local core/ontology/mapping bundle for gaap-scalar-population-v1 or "
	    "fact-dimension-population-v1 or daily-observation-population-v1 or observation-relation-program-v1 or "
	    "multi-fact-user-program-v1 or historical-observation-relation-v1. Captures "
	    "committed native sources into frozen typed batches (AST dependencies required) and "
	    "preflights entire-batch "
	    "identity before publishing. Requires autocommit, one dataset or an explicit native INTEGER-key (opt-in "
	    "String-key) "
	    "INNER/reject pair, two explicit independent same-unit facts, or an explicit observation/history pair. "
	    "At most 10000 rows/256 columns per source and "
	    "16 MiB canonical values (combined for independent facts, including metadata). Explicit reload is required to "
	    "observe source changes.",
	    "CALL ossie_bundle_load('bundle.json')");
	RegisterDescribe(loader, TableFunction("ossie_concepts", {}, ScanBoundary, BindBoundary, InitBoundary),
	                 "Discover the two executable concepts in the current frozen batch, with parent, identity, rule, "
	                 "definition hashes, source and snapshot identity. Database-wide state is captured at binding.",
	                 "SELECT * FROM ossie_concepts()");
	auto list = LogicalType::LIST(LogicalType::ANY);
	auto traced = [&](TableFunction function, const vector<string> &names) {
		NameParameters(function.GetSignature(), names);
		function.GetSignature().AddKeywordOnly("include_trace", LogicalType::BOOLEAN, Value::BOOLEAN(false));
		return function;
	};
	RegisterDescribe(
	    loader,
	    traced(TableFunction("ossie_objects", {LogicalType::VARCHAR, list}, ScanBoundary, BindBoundary, InitBoundary),
	           {"concept", "filters"}),
	    "Return every logical field for an executable concept in the bound immutable batch. Scalar/join profiles add "
	    "entity_id and record_id; observation profile preserves typed tuple columns. "
	    "Strict scalar filters can only narrow population. No implicit sorting, paging or truncation.",
	    "SELECT * FROM ossie_objects('ValidUser', [])");
	RegisterDescribe(loader,
	                 traced(TableFunction("ossie_concept_query", {LogicalType::VARCHAR, list, list, list}, ScanBoundary,
	                                      BindBoundary, InitBoundary),
	                        {"concept", "metrics", "dimensions", "filters"}),
	                 "Execute bounded SUM/COUNT/AVG metrics for a concept, using the same frozen batch and population "
	                 "predicate as ossie_objects. NULL and sparse groups follow DuckDB SQL behavior.",
	                 "SELECT * FROM ossie_concept_query('ValidUser', ['mean_gaap'], ['dws_gaap_di.ds'], [])");
}

} // namespace ossie
} // namespace duckdb
