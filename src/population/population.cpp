#include "ossie/compile.hpp"
#include "ossie/functions.hpp"
#include "ossie/describe_functions.hpp"
#include "ossie/parser.hpp"
#include "ossie/yaml.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/entry_lookup_info.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/table_function.hpp"
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
#include <thread>

namespace duckdb {
namespace ossie {
using namespace duckdb_yyjson;
namespace {

constexpr const char *PROFILE = "gaap-scalar-population-v1";
constexpr idx_t MAX_ROWS = 10000;
constexpr idx_t MAX_BYTES = 16 * 1024 * 1024;
std::atomic<uint64_t> batch_sequence {0};

[[noreturn]] void Fail(const string &object, const string &stage, const string &reason) {
	throw InvalidInputException("ossie_population: object=%s stage=%s reason=%s missing_capability=%s", object, stage,
	                            reason, PROFILE);
}

string Quote(const string &name) {
	return "\"" + StringUtil::Replace(name, "\"", "\"\"") + "\"";
}

template <class QUERY>
unique_ptr<QueryResult> Run(ClientContext &caller, Connection &connection, QUERY sql) {
	auto pending = connection.PendingQuery(std::move(sql));
	if (pending->HasError()) {
		Fail("query", "bind", pending->GetError());
	}
	while (true) {
		if (caller.IsInterrupted()) {
			connection.Interrupt();
			throw InterruptException();
		}
		auto status = pending->ExecuteTask();
		if (status == PendingExecutionResult::EXECUTION_ERROR) {
			Fail("query", "execute", pending->GetError());
		}
		if (PendingQueryResult::IsResultReady(status)) {
			break;
		}
		std::this_thread::yield();
	}
	auto result = pending->Execute();
	if (result->HasError()) {
		Fail("query", "execute", result->GetError());
	}
	return result;
}

string Hash(Connection &connection, const string &bytes) {
	auto result = connection.Query("SELECT system.main.sha256(?)", Value(bytes));
	if (result->HasError()) {
		Fail("hash", "load", result->GetError());
	}
	return result->Fetch()->GetValue(0, 0).GetValue<string>();
}

string Read(ClientContext &context, const string &path) {
	if (path.find("://") != string::npos) {
		Fail(path, "load", "only explicit local paths are supported");
	}
	auto handle = FileSystem::GetFileSystem(context).OpenFile(path, FileFlags::FILE_FLAGS_READ);
	auto size = handle->GetFileSize();
	if (size > 1024 * 1024) {
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
			if (yyjson_is_str(value) && string(yyjson_get_str(value), yyjson_get_len(value)).find('\0') != string::npos)
				Fail("definition", "parse", "NUL-containing strings prohibited in this profile");
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

struct Bundle {
	Model model;
	string root, derived, identity, original_source;
	string manifest_hash, core_hash, ontology_hash, mapping_hash;
	string snapshot_id, content_hash, original_rule;
	idx_t rows = 0;
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
	if (!database)
		Fail("bundle", "execute", "containing database has closed");
	return make_uniq<Connection>(*database);
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

// A separate strict profile checker never inherits the core filter-function flag.
void Strict(const ParsedExpression &expression, bool aggregates = false, bool inside_aggregate = false, idx_t depth = 0,
            idx_t *visited = nullptr) {
	idx_t nodes = 0;
	if (!visited)
		visited = &nodes;
	if (++(*visited) > 1024 || depth > 32)
		Fail(expression.ToString(), "rule", "expression exceeds 1024 nodes or depth 32");
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
			if (!type.GetCatalog().empty() || !type.GetSchema().empty())
				Fail(expression.ToString(), "rule", "qualified casts not supported");
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
		bool arithmetic = function.is_operator && (name == "+" || name == "-" || name == "*");
		if (!function.catalog.empty() || !function.schema.empty() || function.filter ||
		    (function.order_bys && !function.order_bys->orders.empty()) || function.export_state ||
		    (!arithmetic && !aggregate) || (aggregate && (!aggregates || inside_aggregate)) ||
		    (!aggregate && function.distinct) || (aggregate && function.children.size() != 1)) {
			Fail(expression.ToString(), "rule", "unsupported function or aggregate in scalar population");
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

unique_ptr<ParsedExpression> Parse(const string &sql) {
	if (sql.empty() || sql.find('\0') != string::npos)
		Fail("expression", "rule", "nonempty SQL without NUL required");
	if (sql.size() > 16384)
		Fail("expression", "rule", "request/rule exceeds 16 KiB SQL limit");
	vector<unique_ptr<ParsedExpression>> expressions;
	try {
		expressions = Parser::ParseExpressionList(sql);
	} catch (const ParserException &error) {
		Fail(sql, "rule", error.what());
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

string LogicalField(const Bundle &bundle, const string &sql) {
	auto expression = Parse(sql);
	if (expression->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		Fail(sql, "mapping", "mapping must be a single dataset.field reference");
	}
	auto &names = expression->Cast<ColumnRefExpression>().column_names;
	if (names.size() != 2 || !StringUtil::CIEquals(names[0], bundle.model.datasets[0].name) ||
	    !bundle.model.datasets[0].FindField(names[1])) {
		Fail(sql, "mapping", "mapping must reference a declared field in the single root dataset");
	}
	return bundle.model.datasets[0].FindField(names[1])->name;
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
		} else if (!ontology_only && StringUtil::CIEquals(names[0], bundle.model.datasets[0].name)) {
			field = names[1];
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
	if (datatype == "String")
		return id == LogicalTypeId::VARCHAR;
	if (datatype == "Boolean")
		return id == LogicalTypeId::BOOLEAN;
	if (datatype == "Date")
		return id == LogicalTypeId::DATE;
	if (datatype == "Decimal")
		return id == LogicalTypeId::DECIMAL;
	if (datatype == "Float")
		return id == LogicalTypeId::FLOAT || id == LogicalTypeId::DOUBLE;
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
	if (!dependency)
		Fail("_ossie_snapshot_rows", "bind",
		     "internal frozen AST dependency required; rendered SQL is diagnostic only");
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
	for (auto &expression : select.select_list)
		EngineFunctions(*expression);
	return statement;
}

void CheckBooleanType(Bundle &bundle, const ParsedExpression &expression) {
	auto sql =
	    "SELECT " + expression.ToString() + " FROM main.ossie_snapshot AS " + Quote(bundle.model.datasets[0].name);
	auto engine = Engine(bundle);
	auto prepared = engine->Prepare(FrozenSQL(bundle, sql));
	if (prepared->HasError()) {
		Fail(expression.ToString(), "bind", prepared->GetError());
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

shared_ptr<Bundle> LoadBundle(ClientContext &context, const string &manifest_path) {
	if (!context.transaction.IsAutoCommit()) {
		Fail("bundle", "snapshot", "load requires committed native tables outside an explicit transaction");
	}
	auto bundle = make_shared_ptr<Bundle>();
	bundle->owner = bundle;
	bundle->database = context.db;
	auto engine = Engine(*bundle);

	auto manifest_bytes = Read(context, manifest_path);
	Document manifest(manifest_bytes);
	manifest.NoNUL();
	auto root = manifest.Root();
	Keys(root, {"profile", "core", "ontology", "mapping"}, "bundle manifest");
	if (Text(root, "profile") != PROFILE) {
		Fail("profile", "capability", "unknown implementation profile");
	}
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
	if (bundle->model.datasets.size() != 1 || !bundle->model.relationships.empty()) {
		Fail(bundle->model.name, "capability", "profile supports exactly one dataset and no core joins");
	}
	auto &dataset = bundle->model.datasets[0];
	if (dataset.name.empty() || dataset.fields.empty() || dataset.fields.size() > 256) {
		Fail(dataset.name, "capability", "require 1..256 declared fields");
	}
	for (auto &field : dataset.fields) {
		if (field.name.empty())
			Fail(dataset.name, "bind", "empty field name");
		Strict(*field.expression.tree);
	}
	if (bundle->model.metrics.empty() || bundle->model.metrics.size() > 64)
		Fail("metrics", "capability", "require 1..64 metrics");
	for (auto &metric : bundle->model.metrics) {
		if (metric.name.empty() || StringUtil::StartsWith(StringUtil::Lower(metric.name), "_ossie_"))
			Fail(metric.name, "bind", "empty or reserved metric name");
		auto expanded = metric.expression.tree->Copy();
		Strict(*expanded, true);
		ExpandMetric(*bundle, expanded);
		Strict(*expanded, true);
		bool has_aggregate = false;
		ParsedExpressionIterator::VisitExpressionClass(
		    *expanded, ExpressionClass::FUNCTION, [&](const ParsedExpression &expression) {
			    auto name = StringUtil::Lower(expression.Cast<FunctionExpression>().function_name);
			    has_aggregate |= name == "sum" || name == "count" || name == "avg";
		    });
		if (!has_aggregate)
			Fail(metric.name, "capability", "metric requires SUM, COUNT or AVG aggregation");
		CompileToSQL(bundle->model, {metric.name}, {}, {}, CompileOptions());
	}
	Document ontology(ontology_bytes), mapping(mapping_bytes);
	Definitions(*bundle, ontology.Root(), mapping.Root());
	if (StringUtil::CIEquals(dataset.name, bundle->root) || StringUtil::CIEquals(dataset.name, bundle->derived)) {
		Fail(dataset.name, "bind", "dataset and concept namespaces must be distinct");
	}
	bundle->original_source = dataset.source_bound;
	auto parts = QualifiedName::ParseComponents(dataset.source_bound);
	if (parts.size() != 3) {
		Fail(dataset.name, "snapshot", "fully qualified catalog.schema.table source required");
	}
	Connection source(DatabaseInstance::GetDatabase(context));
	Run(context, source, "BEGIN TRANSACTION");
	auto entry = Catalog::GetEntry(*source.context, parts[0], parts[1],
	                               EntryLookupInfo(CatalogType::TABLE_ENTRY, parts[2]), OnEntryNotFound::RETURN_NULL);
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
	                   "SELECT * FROM " + Quote(parts[0]) + "." + Quote(parts[1]) + "." + Quote(parts[2]) + " LIMIT " +
	                       to_string(MAX_ROWS + 1));
	Run(context, source, "COMMIT");
	auto &materialized = capture->Cast<MaterializedQueryResult>();
	bundle->rows = materialized.RowCount();
	if (bundle->rows > MAX_ROWS || capture->types.size() > 256) {
		Fail(dataset.name, "snapshot", "batch exceeds 10000 rows or 256 physical columns; no truncation performed");
	}
	bundle->physical_names = capture->names;
	string content = "ossie-observation-v1:" + to_string(bundle->rows) + ":" + to_string(capture->types.size()) + ":";
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
		if (context.IsInterrupted())
			throw InterruptException();
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
	bundle->snapshot_id = bundle->content_hash + ":" + to_string(++batch_sequence);
	dataset.source_bound = "memory.main.ossie_snapshot";
	string projection = "SELECT ";
	for (idx_t index = 0; index < dataset.fields.size(); index++) {
		if (index)
			projection += ", ";
		projection += FieldTree(*bundle, dataset.fields[index].name)->ToString();
	}
	projection += " FROM main.ossie_snapshot AS " + Quote(dataset.name);
	auto prepared = engine->Prepare(FrozenSQL(*bundle, projection));
	if (prepared->HasError())
		Fail(dataset.name, "bind", prepared->GetError());
	for (idx_t index = 0; index < dataset.fields.size(); index++) {
		if (!Matches(dataset.fields[index].datatype, prepared->GetTypes()[index])) {
			Fail(dataset.fields[index].name, "bind",
			     "logical datatype incompatible with bound physical expression type");
		}
	}
	for (auto &field : dataset.fields)
		BooleanContexts(*bundle, *FieldTree(*bundle, field.name));
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
		for (auto &expression : statement->node->Cast<SelectNode>().select_list)
			EngineFunctions(*expression);
		auto bound = engine->Prepare(std::move(statement));
		if (bound->HasError())
			Fail(metric.name, "bind", bound->GetError());
		if (!Matches(metric.datatype, bound->GetTypes()[0]))
			Fail(metric.name, "bind", "metric datatype incompatible with bound aggregate type");
	}
	CheckBoolean(*bundle, *bundle->rule);
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
	if (concept != bundle.root && concept != bundle.derived) {
		Fail(concept, "bind", "unknown executable concept");
	}
	unique_ptr<ParsedExpression> predicate;
	if (concept == bundle.derived)
		predicate = bundle.rule->Copy();
	if (filters.size() > 64)
		Fail(concept, "capability", "at most 64 request filters supported");
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
		return Compile(bundle.model, metrics, dimensions, {}, options);
	}
	auto select = make_uniq<SelectNode>();
	for (auto name : {"entity_id", "record_id"}) {
		auto id = FieldTree(bundle, bundle.identity);
		id->SetAlias(name);
		select->select_list.push_back(std::move(id));
	}
	for (auto &field : bundle.model.datasets[0].fields) {
		auto expression = FieldTree(bundle, field.name);
		expression->SetAlias(bundle.model.datasets[0].name + "." + field.name);
		select->select_list.push_back(std::move(expression));
	}
	select->where_clause = std::move(predicate);
	select->from_table = SnapshotTable(bundle);
	auto statement = make_uniq<SelectStatement>();
	statement->node = std::move(select);
	return statement;
}

struct PopulationBind : TableFunctionData {
	shared_ptr<Bundle> bundle;
	string sql;
	unique_ptr<SelectStatement> statement;
	string path;
	bool load = false;
	bool compile = false;
	bool discover = false;
};

struct PopulationScan : GlobalTableFunctionState {
	unique_ptr<QueryResult> result;
	unique_ptr<DataChunk> chunk;
	vector<vector<Value>> rows;
	idx_t offset = 0;
};

shared_ptr<Bundle> Current(ClientContext &context) {
	auto bundle = PopulationState::Get(context).GetBundle();
	if (!bundle)
		Fail("bundle", "bind", "no bundle loaded; call ossie_bundle_load");
	return bundle;
}

unique_ptr<FunctionData> LoadBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                  vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	if (input.inputs[0].IsNull())
		Fail("bundle", "load", "manifest path required");
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
	names = {"concept",         "parent",         "identity_field", "derived_by",      "profile",
	         "snapshot_id",     "content_sha256", "core_sha256",    "ontology_sha256", "mapping_sha256",
	         "manifest_sha256", "source",         "records"};
	types = vector<LogicalType>(12, LogicalType::VARCHAR);
	types.push_back(LogicalType::BIGINT);
	return std::move(result);
}

unique_ptr<FunctionData> RequestBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                     vector<string> &names) {
	auto result = make_uniq<PopulationBind>();
	result->bundle = Current(context);
	if (input.inputs[0].IsNull())
		Fail("concept", "bind", "concept required");
	auto concept = input.inputs[0].GetValue<string>();
	bool objects = input.inputs.size() == 2;
	auto metrics = objects ? vector<string>() : ToStringList(input.inputs[1], "ossie_concept_query");
	auto dimensions = objects ? vector<string>() : ToStringList(input.inputs[2], "ossie_concept_query");
	auto filters = ToStringList(input.inputs.back(), "ossie_population");
	if (metrics.size() > 64 || dimensions.size() > 256)
		Fail(concept, "capability", "request exceeds 64 metrics or 256 dimensions");
	result->compile = input.table_function.name == "ossie_concept_compile";
	// Empty metrics explicitly select the object path for the compile endpoint only.
	objects |= result->compile && metrics.empty();
	if (objects && !dimensions.empty())
		Fail(concept, "bind", "object compile does not accept dimensions");
	lock_guard<mutex> guard(result->bundle->query_lock);
	result->statement = QueryAST(*result->bundle, concept, metrics, dimensions, filters, objects);
	auto trace = input.named_parameters.find("include_trace");
	if (trace != input.named_parameters.end() && !trace->second.IsNull() && trace->second.GetValue<bool>()) {
		auto &select = result->statement->node->Cast<SelectNode>();
		for (auto entry : {std::make_pair("_ossie_snapshot_id", result->bundle->snapshot_id),
		                   std::make_pair("_ossie_content_sha256", result->bundle->content_hash),
		                   std::make_pair("_ossie_ontology_sha256", result->bundle->ontology_hash)}) {
			auto expression = make_uniq<ConstantExpression>(Value(entry.second));
			expression->SetAlias(entry.first);
			select.select_list.push_back(std::move(expression));
		}
	}
	auto &query = result->statement->node->Cast<SelectNode>();
	for (auto &expression : query.select_list)
		EngineFunctions(*expression);
	for (auto &expression : query.groups.group_expressions)
		EngineFunctions(*expression);
	if (query.where_clause)
		EngineFunctions(*query.where_clause);
	result->sql = result->statement->ToString();
	if (result->compile) {
		names = {"sql",
		         "profile",
		         "snapshot_id",
		         "content_sha256",
		         "core_sha256",
		         "ontology_sha256",
		         "mapping_sha256",
		         "manifest_sha256",
		         "execution_target",
		         "executed"};
		types = vector<LogicalType>(9, LogicalType::VARCHAR);
		types.push_back(LogicalType::BOOLEAN);
	} else {
		auto engine = Engine(*result->bundle);
		auto prepared = engine->Prepare(result->statement->Copy());
		if (prepared->HasError())
			Fail(concept, "bind", prepared->GetError());
		types = prepared->GetTypes();
		names = prepared->GetNames();
	}
	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> Init(ClientContext &context, TableFunctionInitInput &input) {
	auto result = make_uniq<PopulationScan>();
	auto &data = input.bind_data->Cast<PopulationBind>();
	if (data.load) {
		shared_ptr<Bundle> bundle;
		try {
			bundle = LoadBundle(context, data.path);
		} catch (const InterruptException &) {
			throw;
		} catch (const std::exception &error) {
			Fail(data.path, "load", error.what());
		}
		result->rows.push_back(
		    {Value(PROFILE), Value(bundle->snapshot_id), Value(bundle->content_hash), Value::BIGINT(bundle->rows)});
		PopulationState::Get(context).SetBundle(std::move(bundle));
	} else if (data.discover) {
		auto &b = *data.bundle;
		for (auto concept : {b.root, b.derived}) {
			bool derived = concept == b.derived;
			result->rows.push_back({Value(concept), derived ? Value(b.root) : Value(LogicalType::VARCHAR),
			                        Value(b.model.datasets[0].name + "." + b.identity),
			                        derived ? Value(b.original_rule) : Value(LogicalType::VARCHAR), Value(PROFILE),
			                        Value(b.snapshot_id), Value(b.content_hash), Value(b.core_hash),
			                        Value(b.ontology_hash), Value(b.mapping_hash), Value(b.manifest_hash),
			                        Value(b.original_source), Value::BIGINT(b.rows)});
		}
	} else if (data.compile) {
		auto &b = *data.bundle;
		result->rows.push_back(
		    {Value(data.sql), Value(PROFILE), Value(b.snapshot_id), Value(b.content_hash), Value(b.core_hash),
		     Value(b.ontology_hash), Value(b.mapping_hash), Value(b.manifest_hash),
		     Value("frozen typed observation batch (AST dependency required)"), Value::BOOLEAN(false)});
	} else {
		lock_guard<mutex> guard(data.bundle->query_lock);
		auto engine = Engine(*data.bundle);
		result->result = Run(context, *engine, data.statement->Copy());
	}
	return std::move(result);
}

void Scan(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<PopulationScan>();
	if (state.result) {
		state.chunk = state.result->Fetch();
		if (state.chunk && state.chunk->size())
			output.Reference(*state.chunk);
		return;
	}
	idx_t count = 0;
	while (state.offset < state.rows.size() && count < STANDARD_VECTOR_SIZE) {
		auto &row = state.rows[state.offset++];
		for (idx_t col = 0; col < row.size(); col++)
			output.SetValue(col, count, row[col]);
		count++;
	}
	output.SetCardinality(count);
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
	RegisterDescribe(loader, TableFunction("ossie_bundle_load", {LogicalType::VARCHAR}, Scan, LoadBind, Init),
	                 "Load a hash-bound local core/ontology/mapping bundle for gaap-scalar-population-v1. Captures a "
	                 "committed native table into a frozen typed observation batch (AST dependency required) and "
	                 "preflights entire-batch "
	                 "identity before publishing. Requires autocommit, one dataset, at most 10000 rows/256 columns and "
	                 "16 MiB canonical values. Explicit reload is required to observe source changes.",
	                 "CALL ossie_bundle_load('bundle.json')");
	RegisterDescribe(loader, TableFunction("ossie_concepts", {}, Scan, DiscoverBind, Init),
	                 "Discover the two executable concepts in the current frozen batch, with parent, identity, rule, "
	                 "definition hashes, source and snapshot identity. Database-wide state is captured at binding.",
	                 "SELECT * FROM ossie_concepts()");
	RegisterDescribe(loader,
	                 traced(TableFunction("ossie_objects", {LogicalType::VARCHAR, list}, Scan, RequestBind, Init)),
	                 "Return entity_id, record_id and every logical field for a concept in the bound immutable batch. "
	                 "Strict scalar filters can only narrow population. No implicit sorting, paging or truncation.",
	                 "SELECT * FROM ossie_objects('ValidUser', [])");
	RegisterDescribe(
	    loader,
	    traced(TableFunction("ossie_concept_query", {LogicalType::VARCHAR, list, list, list}, Scan, RequestBind, Init)),
	    "Execute bounded SUM/COUNT/AVG metrics for a concept, using the same frozen batch and population "
	    "predicate as ossie_objects. NULL and sparse groups follow DuckDB SQL behavior.",
	    "SELECT * FROM ossie_concept_query('ValidUser', ['mean_gaap'], ['dws_gaap_di.ds'], [])");
	RegisterDescribe(
	    loader,
	    TableFunction("ossie_concept_compile", {LogicalType::VARCHAR, list, list, list}, Scan, RequestBind, Init),
	    "Compile a concept request without executing it, with diagnostic SQL targeting a frozen batch and pinned "
	    "definition/snapshot hashes; executed is false. Empty metrics select the object query path. "
	    "A separate query after reload may bind a different snapshot.",
	    "SELECT * FROM ossie_concept_compile('ValidUser', ['mean_gaap'], [], [])");
}

} // namespace ossie
} // namespace duckdb
