#include "population_internal.hpp"
#include "ossie/model.hpp"

namespace duckdb {
namespace ossie {

using duckdb_yyjson::yyjson_doc_free;
using duckdb_yyjson::yyjson_doc_get_root;
using duckdb_yyjson::yyjson_get_len;
using duckdb_yyjson::yyjson_get_str;
using duckdb_yyjson::yyjson_is_arr;
using duckdb_yyjson::yyjson_is_obj;
using duckdb_yyjson::yyjson_is_str;
using duckdb_yyjson::yyjson_obj_get;
using duckdb_yyjson::yyjson_read;

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

void PopulationError(const string &object, const string &stage, const string &reason, ExceptionType type,
                     const string &source_type, unordered_map<string, string> extra) {
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

void Fail(const string &object, const string &stage, const string &reason) {
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

void NormalizeError(const ErrorData &error, const string &object, const string &stage, ExceptionType reported_type) {
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

vector<yyjson_val *> Array(yyjson_val *object, const char *key, bool required) {
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

vector<string> Strings(yyjson_val *object, const char *key, bool required) {
	vector<string> result;
	for (auto value : Array(object, key, required)) {
		result.push_back(ArrayText(value));
	}
	return result;
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
	if (Text(root, "version") != OSSIE_SPEC_VERSION) {
		Fail("version", "parse", string("supported version is ") + OSSIE_SPEC_VERSION);
	}
}

} // namespace ossie
} // namespace duckdb
