#pragma once

// Shared by the population translation units only. Not part of the public ossie headers.

#include "duckdb/common/common.hpp"
#include "duckdb/common/error_data.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/common/vector.hpp"
#include "yyjson.hpp"

#include <initializer_list>

namespace duckdb {
namespace ossie {

using duckdb_yyjson::yyjson_val;

constexpr const char *ERROR_SCHEMA = "ossie-population-error-v1";
constexpr const char *ERROR_MARKER = "ossie_population_error_v1: ";
constexpr const char *ERROR_INFO = "ossie_population_error_json";

string JSONString(const string &value);
string JSONStrings(const vector<string> &values);
string ErrorCode(const string &stage);

[[noreturn]] void PopulationError(const string &object, const string &stage, const string &reason, ExceptionType type,
                                  const string &source_type, unordered_map<string, string> extra = {});
[[noreturn]] void Fail(const string &object, const string &stage, const string &reason);
bool RecoverableError(ExceptionType type);
[[noreturn]] void NormalizeError(const ErrorData &error, const string &object, const string &stage,
                                 ExceptionType reported_type);

//! yyjson accessors that fail with a population error rather than returning a sentinel.
yyjson_val *Member(yyjson_val *object, const char *key);
string Text(yyjson_val *object, const char *key);
vector<yyjson_val *> Array(yyjson_val *object, const char *key, bool required = false);
string ArrayText(yyjson_val *value);
vector<string> Strings(yyjson_val *object, const char *key, bool required = false);
void Keys(yyjson_val *value, std::initializer_list<const char *> allowed, const string &object);
void Empty(yyjson_val *value, const char *key, const string &object);
void Version(yyjson_val *root);

} // namespace ossie
} // namespace duckdb
