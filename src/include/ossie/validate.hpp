#pragma once

#include "ossie/model.hpp"

namespace duckdb {
namespace ossie {

//! Structural checks over a parsed model. Does not touch the catalog.
enum class MetricReferencePolicy : uint8_t { QUALIFIED_FIELDS, DECLARED_METRICS };
void ValidateModel(const Model &model, MetricReferencePolicy policy = MetricReferencePolicy::QUALIFIED_FIELDS);

} // namespace ossie
} // namespace duckdb
