#include "ossie/compile.hpp"
#include "ossie/parser.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/query_result.hpp"
#include <fstream>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
int checks = 0;
void Check(bool condition, const std::string &name) {
	if (!condition)
		throw std::runtime_error(name);
	checks++;
	std::cout << "PASS: " << name << "\n";
}
std::string Read(const std::string &path) {
	std::ifstream file(path);
	if (!file)
		throw std::runtime_error("missing fixture: " + path);
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}
template <class ACTION>
void Refuse(ACTION action, const std::string &reason, const std::string &name) {
	bool refused = false;
	try {
		action();
	} catch (const duckdb::InvalidInputException &error) {
		refused = std::string(error.what()).find(reason) != std::string::npos;
		if (!refused)
			throw std::runtime_error(name + ": " + error.what());
	}
	Check(refused, name);
}
void Run(const std::string &root) {
	using namespace duckdb;
	using namespace duckdb::ossie;
	auto model = ParseModel(Read(root + "test/fixtures/metric-grain/core.json"), RebindMap());
	CompileOptions options;
	auto before = CompileToSQL(model, {"amount_sum", "record_count"}, {"dimension.label"}, {}, options);
	vector<string> valid {"amount_sum",       "amount_avg",   "value_count",        "record_count", "eligible_amount",
	                      "dimension_scalar", "wrapped_fact", "constant_field_sum", "quoted_fact"};
	for (auto &metric : valid) {
		RequireMetricGrain(model, {metric}, "fact");
		Check(true, "fact grain: " + metric);
	}
	RequireMetricGrain(model, valid, "fact");
	Check(true, "independent metrics all have designated fact grain");
	RequireMetricGrain(model, {"AMOUNT_SUM"}, "FACT");
	Check(true, "case-insensitive declared metric and dataset");
	Check(CompileToSQL(model, {"dimension_count"}, {}, {}, options).find("r6_dimension") != string::npos,
	      "core dimension metric independently compiles");
	for (auto name :
	     {"dimension_count", "dimension_distinct", "mixed", "wrapped_mixed", "scalar", "unknown_function"}) {
		Refuse([&] { RequireMetricGrain(model, {name}, "fact"); }, "designated grain",
		       "reject different/absent grain: " + string(name));
	}
	for (auto name : {"star", "with_star", "one"}) {
		Refuse([&] { RequireMetricGrain(model, {name}, "fact"); }, "without a dataset",
		       "reject grainless aggregate: " + string(name));
	}
	Refuse(
	    [&] {
		    RequireMetricGrain(model, {"amount_sum", "star"}, "fact");
	    },
	    "without a dataset", "new checker does not infer standalone COUNT(*) grain from another metric");
	Check(CompileToSQL(model, {"amount_sum", "star"}, {}, {}, options).find("count_star()") != string::npos,
	      "existing core grainless inheritance retained");
	Check(CompileToSQL(model, {"with_star"}, {}, {}, options).find("count_star()") != string::npos,
	      "existing core in-metric grainless inheritance retained");
	Refuse([&] { RequireMetricGrain(model, {"windowed"}, "fact"); }, "Window metrics",
	       "window rejected by shared analysis");
	Refuse([&] { RequireMetricGrain(model, {"absent"}, "fact"); }, "no metric", "unknown metric rejected");
	Refuse([&] { RequireMetricGrain(model, {"amount_sum"}, "absent"); }, "not declared",
	       "unknown designated dataset rejected");
	Refuse([&] { RequireMetricGrain(model, {}, "fact"); }, "at least one metric", "empty request rejected");
	RequireMetricGrain(model, {"dimension_count"}, "dimension");
	Refuse([&] { CompileToSQL(model, {"dimension_count"}, {"fact.id"}, {}, options); }, "repeats each",
	       "designated grain does not replace fan-out planning");
	auto ambiguous = model;
	auto second = ambiguous.relationships[0];
	second.name = "another_path";
	ambiguous.relationships.push_back(second);
	RequireMetricGrain(ambiguous, {"amount_sum"}, "fact");
	Refuse([&] { CompileToSQL(ambiguous, {"amount_sum"}, {"dimension.label"}, {}, options); },
	       "more than one join path", "designated grain does not replace ambiguity planning");
	Check(before == CompileToSQL(model, {"amount_sum", "record_count"}, {"dimension.label"}, {}, options),
	      "validation leaves original model and SQL unchanged");

	DuckDB database(nullptr);
	Connection connection(database);
	auto rows = connection.Query(Read(root + "test/fixtures/safe-join-research/rows.sql"));
	Check(!rows->HasError(), "native binding fixture created");
	RequireMetricGrain(model, {"nested"}, "fact");
	auto nested = connection.Query(CompileToSQL(model, {"nested"}, {}, {}, options));
	Check(nested->HasError() && nested->GetError().find("cannot be nested") != string::npos,
	      "nested aggregates still require physical binder refusal");
	auto sums = connection.Query(before);
	Check(!sums->HasError() && sums->RowCount() == 2, "valid original query executes after grain checks");
	bool groups_correct = true;
	bool north = false, south = false;
	for (idx_t row = 0; row < sums->RowCount(); row++) {
		auto label = sums->GetValue(0, row).ToString();
		north |= label == "North";
		south |= label == "South";
		groups_correct &= (label == "North" && sums->GetValue(1, row).ToString() == "24.98" &&
		                   sums->GetValue(2, row).ToString() == "4") ||
		                  (label == "South" && sums->GetValue(1, row).ToString() == "30.03" &&
		                   sums->GetValue(2, row).ToString() == "2");
	}
	Check(groups_correct && north && south,
	      "group membership, totals and counts match independent hand-derived oracle");
	auto values = connection.Query(CompileToSQL(model, valid, {}, {}, options));
	Check(!values->HasError() && values->RowCount() == 1, "all designated fact metrics execute");
	Check(values->GetValue(0, 0).ToString() == "55.01" && values->GetValue(2, 0).ToString() == "5" &&
	          values->GetValue(3, 0).ToString() == "6" && values->GetValue(4, 0).ToString() == "24.98" &&
	          values->GetValue(5, 0).ToString() == "24.98" && values->GetValue(7, 0).ToString() == "6" &&
	          values->GetValue(8, 0).ToString() == "55.01",
	      "exact fact metrics/CASE/constant field match hand oracle");
	Check(std::abs(values->GetValue(1, 0).GetValue<double>() - 11.002) < 1e-12 &&
	          std::abs(values->GetValue(6, 0).GetValue<double>() - 55.01 / 6) < 1e-12,
	      "AVG and wrapped ratio match hand oracle");
	for (auto name : {"nested_star", "hidden_sum_metric", "hidden_window_metric"}) {
		RequireMetricGrain(model, {name}, "fact");
		auto bound = connection.Query(CompileToSQL(model, {name}, {}, {}, options));
		Check(bound->HasError(),
		      "grain prerequisite passes; physical binder rejects hidden/nested expression: " + string(name));
	}
	RequireMetricGrain(model, {"unrecognized_aggregate"}, "fact");
	auto escaped = connection.Query(CompileToSQL(model, {"unrecognized_aggregate"}, {}, {}, options));
	Check(!escaped->HasError() && escaped->GetValue(0, 0).ToString() == "58.01",
	      "unrecognized aggregate requires separate strict expression whitelist; binder alone is insufficient");
}
} // namespace
int main(int argc, char **argv) {
	try {
		if (argc != 2)
			throw std::runtime_error("usage: ossie_metric_grain_test PROJECT_ROOT");
		std::string root = argv[1];
		if (root.empty() || root.back() != '/')
			root += '/';
		Run(root);
		std::cout << "PASS: " << checks << " designated-grain controls\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << "\n";
		return 1;
	}
}
