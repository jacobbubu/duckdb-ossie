#include "ossie/history_program.hpp"
#include <algorithm>
#include <set>
#include <utility>

namespace duckdb {
namespace ossie {

HistoryValidationError::HistoryValidationError(std::string stage_p, const std::string &message)
    : std::runtime_error(message), stage(std::move(stage_p)) {
}

bool HistoryByteLess::operator()(const std::string &a, const std::string &b) const {
	return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
		return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
	});
}

HistoryIndex BuildHistoryIndex(const std::vector<HistoryRecord> &records, const std::function<void()> &check) {
	check();
	if (records.size() > 10000) {
		throw HistoryValidationError("resource", "history source exceeds10000 records");
	}
	HistoryIndex result;
	result.source_rows = records.size();
	std::set<std::string, HistoryByteLess> ids;
	using Targets = std::map<std::string, std::vector<HistorySegment>, HistoryByteLess>;
	std::map<std::string, Targets, HistoryByteLess> groups;
	for (auto &record : records) {
		check();
		if (record.record_id.empty() || record.user.empty() || record.target.empty()) {
			throw HistoryValidationError("identity", "nonempty raw record_id/User/target identities required");
		}
		if (!ids.insert(record.record_id).second) {
			throw HistoryValidationError("identity", "duplicate history record_id across complete capture");
		}
		if (!Date::IsFinite(record.start) || !Date::IsFinite(record.end) || record.start >= record.end) {
			throw HistoryValidationError("constraint", "finite history DATE endpoints require start<end");
		}
		groups[record.user][record.target].push_back({record.start, record.end, record.target});
	}
	for (auto &user : groups) {
		check();
		auto &combined = result.users[user.first];
		for (auto &target : user.second) {
			auto &segments = target.second;
			std::sort(segments.begin(), segments.end(), [&](const HistorySegment &a, const HistorySegment &b) {
				check();
				return a.start == b.start ? a.end < b.end : a.start < b.start;
			});
			for (auto &segment : segments) {
				check();
				if (!combined.empty() && combined.back().target == segment.target &&
				    segment.start <= combined.back().end) {
					if (segment.end > combined.back().end) {
						combined.back().end = segment.end;
					}
				} else {
					combined.push_back(segment);
				}
			}
		}
		std::sort(combined.begin(), combined.end(), [&](const HistorySegment &a, const HistorySegment &b) {
			check();
			return a.start == b.start ? a.end < b.end : a.start < b.start;
		});
		for (size_t i = 0; i < combined.size(); i++) {
			check();
			// Same-target overlaps are already merged. Every remaining overlap is a full-capture conflict.
			if (i && combined[i].start < combined[i - 1].end) {
				throw HistoryValidationError("constraint", "different history targets overlap for the same raw User");
			}
		}
		result.segment_count += combined.size();
	}
	check();
	return result;
}

const std::string *HistoryIndex::Find(const std::string &user, date_t date, const std::function<void()> &check) const {
	check();
	if (!Date::IsFinite(date)) {
		throw HistoryValidationError("identity", "finite observation DATE required");
	}
	auto found = users.find(user);
	if (found == users.end()) {
		return nullptr;
	}
	auto &segments = found->second;
	auto next =
	    std::upper_bound(segments.begin(), segments.end(), date, [&](date_t value, const HistorySegment &segment) {
		    check();
		    return value < segment.start;
	    });
	if (next == segments.begin()) {
		return nullptr;
	}
	--next;
	check();
	return date < next->end ? &next->target : nullptr;
}

} // namespace ossie
} // namespace duckdb
