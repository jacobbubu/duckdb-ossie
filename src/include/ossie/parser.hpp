#pragma once

#include "ossie/model.hpp"
#include "ossie/validate.hpp"

namespace duckdb {
class ClientContext;

namespace ossie {

//! Parse an Ossie model from JSON text. Throws InvalidInputException on any malformed input
Model ParseModel(const string &json_text, const RebindMap &rebind,
                 MetricReferencePolicy policy = MetricReferencePolicy::QUALIFIED_FIELDS);

//! Read `path` through DuckDB's FileSystem and parse it.
Model LoadModel(ClientContext &context, const string &path, const RebindMap &rebind);

} // namespace ossie
} // namespace duckdb
