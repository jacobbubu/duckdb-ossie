#include "ossie/history_program.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <set>

using namespace duckdb;
using namespace duckdb::ossie;
namespace {
size_t checks = 0;
void Check(bool ok) {
	checks++;
	if (!ok) {
		throw std::runtime_error("history program check failed");
	}
}
const auto uninterrupted = []() {
};
HistoryRecord Row(std::string id, std::string user, std::string target, int32_t start, int32_t end) {
	return {std::move(id), std::move(user), std::move(target), date_t(start), date_t(end)};
}
void Reject(const std::vector<HistoryRecord> &rows, const std::string &stage) {
	try {
		BuildHistoryIndex(rows, uninterrupted);
	} catch (const HistoryValidationError &error) {
		Check(error.stage == stage);
		return;
	}
	Check(false);
}
bool Conflict(const std::vector<HistoryRecord> &rows) {
	for (size_t i = 0; i < rows.size(); i++) {
		for (size_t j = i + 1; j < rows.size(); j++) {
			auto &a = rows[i];
			auto &b = rows[j];
			if (a.user == b.user && a.target != b.target && a.start < b.end && b.start < a.end) {
				return true;
			}
		}
	}
	return false;
}
void Compare(const std::vector<HistoryRecord> &rows) {
	if (Conflict(rows)) {
		Reject(rows, "constraint");
		return;
	}
	auto index = BuildHistoryIndex(rows, uninterrupted);
	Check(index.source_rows == rows.size());
	Check(index.segment_count <= rows.size());
	for (const auto &user : {std::string("u"), std::string("v"), std::string("U"), std::string("missing")}) {
		for (int32_t date = -1; date <= 7; date++) {
			std::set<std::string> expected;
			for (auto &row : rows) {
				if (row.user == user && row.start.days <= date && date < row.end.days) {
					expected.insert(row.target);
				}
			}
			auto target = index.Find(user, date_t(date), uninterrupted);
			Check(expected.empty() ? !target : target && expected.size() == 1 && *target == *expected.begin());
		}
	}
}
} // namespace
int main() {
	try {
		Compare({});
		// Exhaustive paired intervals/targets/Users; independent pairwise conflict and row-by-row lookup.
		for (int32_t a = 0; a < 4; a++) {
			for (int32_t b = a + 1; b <= 4; b++) {
				for (int32_t c = 0; c < 4; c++) {
					for (int32_t d = c + 1; d <= 4; d++) {
						for (const auto &user : {"u", "v", "U"}) {
							for (const auto &target : {"X", "x"}) {
								Compare({Row("a", "u", "X", a, b), Row("b", user, target, c, d)});
							}
						}
					}
				}
			}
		}
		Reject({Row("a", "no-observation", "X", 0, 6), Row("b", "no-observation", "X", 1, 2),
		        Row("c", "no-observation", "Y", 3, 4)},
		       "constraint");
		Compare({Row("a", "u", "X", 0, 1), Row("b", "u", "X", 3, 4), Row("c", "u", "Y", 1, 3)});
		std::vector<HistoryRecord> nested = {Row("a", "u", "X", 0, 5), Row("b", "u", "X", 1, 2),
		                                     Row("c", "u", "Y", 5, 6), Row("d", "v", "x", 0, 1)};
		for (size_t i = 0; i < nested.size(); i++) {
			Compare(nested);
			std::rotate(nested.begin(), nested.begin() + 1, nested.end());
		}
		Reject({Row("a", "u", "X", 0, 1), Row("a", "v", "Y", 2, 3)}, "identity");
		Reject({Row("", "u", "X", 0, 1)}, "identity");
		Reject({Row("a", "", "X", 0, 1)}, "identity");
		Reject({Row("a", "u", "", 0, 1)}, "identity");
		Reject({Row("a", "u", "X", 1, 1)}, "constraint");
		Reject({Row("a", "u", "X", 2, 1)}, "constraint");
		Reject({{"a", "u", "X", date_t::ninfinity(), date_t(1)}}, "constraint");
		Reject({{"a", "u", "X", date_t(0), date_t::infinity()}}, "constraint");
		const int32_t lo = std::numeric_limits<int32_t>::min(), hi = std::numeric_limits<int32_t>::max() - 1;
		auto extremes = BuildHistoryIndex({Row("a", "u", "X", lo, hi)}, uninterrupted);
		Check(*extremes.Find("u", date_t(lo), uninterrupted) == "X");
		Check(*extremes.Find("u", date_t(hi - 1), uninterrupted) == "X");
		Check(!extremes.Find("u", date_t(hi), uninterrupted));
		const std::string nul("u\0v", 3);
		auto raw = BuildHistoryIndex({Row("A", nul, nul, 0, 1), Row("a", "u", "X", 0, 1)}, uninterrupted);
		Check(*raw.Find(nul, date_t(0), uninterrupted) == nul);
		Check(!raw.Find("U", date_t(0), uninterrupted));
		bool nonfinite_refused = false;
		try {
			raw.Find("u", date_t::infinity(), uninterrupted);
		} catch (const HistoryValidationError &error) {
			nonfinite_refused = error.stage == "identity";
		}
		Check(nonfinite_refused);
		bool lookup_cancelled = false;
		try {
			raw.Find("u", date_t(0), [] { throw std::runtime_error("lookup cancellation"); });
		} catch (const std::runtime_error &error) {
			lookup_cancelled = std::string(error.what()) == "lookup cancellation";
		}
		Check(lookup_cancelled);
		std::vector<HistoryRecord> many;
		for (size_t i = 0; i < 10000; i++) {
			many.push_back(Row(std::to_string(i), "u", "X", 0, 4));
		}
		auto large = BuildHistoryIndex(many, uninterrupted);
		Check(large.source_rows == 10000 && large.segment_count == 1);
		Check(*large.Find("u", date_t(0), uninterrupted) == "X");
		many.push_back(Row("extra", "u", "X", 0, 1));
		Reject(many, "resource");
		many.pop_back();
		// Deterministic cancellation at validation, sorting, merging and final check; no partial index returns.
		size_t ticks = 0;
		BuildHistoryIndex(many, [&] { ticks++; });
		for (size_t cancel : {size_t(1), size_t(5), size_t(10002), ticks / 2, ticks - 3, ticks}) {
			size_t at = 0;
			bool interrupted = false;
			try {
				BuildHistoryIndex(many, [&] {
					if (++at == cancel) {
						throw std::runtime_error("controlled cancellation");
					}
				});
			} catch (const std::runtime_error &error) {
				interrupted = std::string(error.what()) == "controlled cancellation";
			}
			Check(interrupted);
			Check(BuildHistoryIndex(many, uninterrupted).segment_count == 1);
		}
		for (size_t i = 0; i < many.size(); i++) {
			many[i].start = date_t(static_cast<int32_t>(2 * i));
			many[i].end = date_t(static_cast<int32_t>(2 * i + 1));
		}
		auto sparse = BuildHistoryIndex(many, uninterrupted);
		Check(sparse.segment_count == 10000);
		size_t lookup_checks = 0;
		Check(*sparse.Find("u", date_t(19998), [&] { lookup_checks++; }) == "X");
		Check(lookup_checks <= 32);
		Check(!sparse.Find("u", date_t(19999), uninterrupted));
		lookup_checks = 0;
		lookup_cancelled = false;
		try {
			sparse.Find("u", date_t(19998), [&] {
				if (++lookup_checks == 5) {
					throw std::runtime_error("search cancellation");
				}
			});
		} catch (const std::runtime_error &error) {
			lookup_cancelled = std::string(error.what()) == "search cancellation";
		}
		Check(lookup_cancelled);
		std::cout << "PASS history index " << checks << " checks; independent interval enumeration and cancellation\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
