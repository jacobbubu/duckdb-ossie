#pragma once

#include "duckdb/common/types/date.hpp"
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace duckdb {
namespace ossie {

// Validated physical rows feed this pure index; raw frozen collections remain separate.
struct HistoryRecord {
	std::string record_id, user, target;
	date_t start, end;
};
struct HistorySegment {
	date_t start, end;
	std::string target;
};
struct HistoryValidationError : public std::runtime_error {
	std::string stage;
	HistoryValidationError(std::string stage, const std::string &message);
};
struct HistoryByteLess {
	bool operator()(const std::string &a, const std::string &b) const;
};
struct HistoryIndex {
	std::map<std::string, std::vector<HistorySegment>, HistoryByteLess> users;
	size_t source_rows = 0, segment_count = 0;
	// Pointer is into the immutable index; no target or record identity is normalized.
	const std::string *Find(const std::string &user, date_t date, const std::function<void()> &check) const;
};
HistoryIndex BuildHistoryIndex(const std::vector<HistoryRecord> &records, const std::function<void()> &check);

} // namespace ossie
} // namespace duckdb
