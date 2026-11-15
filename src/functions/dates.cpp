#include "duckdb/common/exception.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/interval.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/vector_operations/binary_executor.hpp"
#include "duckdb/common/vector_operations/ternary_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "ossie/describe_functions.hpp"
#include "ossie/functions.hpp"

// OSSIE_SQL_2026's DATE_TRUNC and DATEADD give a DATE back for a DATE in. DuckDB's date_trunc and date_add
// give a TIMESTAMP at midnight: the same instant under another type, which changes what a downstream
// comparison, GROUP BY or export sees. These overloads keep the input type, for the date parts that
// cannot introduce a time of day. The lowering in dialect.cpp emits them; the sub-day parts stay on
// DuckDB's own functions, where a TIMESTAMP result is the right one.

namespace duckdb {
namespace ossie {

namespace {

enum class DatePart : uint8_t { YEAR, QUARTER, MONTH, WEEK, DAY };

DatePart ParsePart(const string_t &part, const char *function_name) {
	auto name = StringUtil::Lower(part.GetString());
	if (name == "year") {
		return DatePart::YEAR;
	}
	if (name == "quarter") {
		return DatePart::QUARTER;
	}
	if (name == "month") {
		return DatePart::MONTH;
	}
	if (name == "week") {
		return DatePart::WEEK;
	}
	if (name == "day") {
		return DatePart::DAY;
	}
	throw InvalidInputException("%s: date part \"%s\" is not year, quarter, month, week or day", function_name,
	                            part.GetString());
}

date_t TruncateDate(date_t date, DatePart part) {
	if (!date.IsFinite()) {
		return date;
	}
	int32_t year, month, day;
	Date::Convert(date, year, month, day);
	switch (part) {
	case DatePart::YEAR:
		return Date::FromDate(year, 1, 1);
	case DatePart::QUARTER:
		return Date::FromDate(year, ((month - 1) / 3) * 3 + 1, 1);
	case DatePart::MONTH:
		return Date::FromDate(year, month, 1);
	case DatePart::WEEK:
		// ISO weekday: Monday is 1, so this lands on the Monday on or before `date`.
		return date_t(date.days - (Date::ExtractISODayOfTheWeek(date) - 1));
	case DatePart::DAY:
		return date;
	}
	throw InternalException("ossie_date_trunc: unreachable date part");
}

int32_t Scaled(int64_t amount, int64_t factor) {
	if (amount > NumericLimits<int32_t>::Maximum() / factor || amount < NumericLimits<int32_t>::Minimum() / factor) {
		throw OutOfRangeException("ossie_dateadd: amount %lld is out of range", amount);
	}
	return static_cast<int32_t>(amount * factor);
}

interval_t IntervalOf(DatePart part, int64_t amount) {
	interval_t result {0, 0, 0};
	switch (part) {
	case DatePart::YEAR:
		result.months = Scaled(amount, 12);
		break;
	case DatePart::QUARTER:
		result.months = Scaled(amount, 3);
		break;
	case DatePart::MONTH:
		result.months = Scaled(amount, 1);
		break;
	case DatePart::WEEK:
		result.days = Scaled(amount, 7);
		break;
	case DatePart::DAY:
		result.days = Scaled(amount, 1);
		break;
	}
	return result;
}

date_t TruncDate(string_t part, date_t date) {
	return TruncateDate(date, ParsePart(part, "ossie_date_trunc"));
}

timestamp_t TruncTimestamp(string_t part, timestamp_t timestamp) {
	if (!timestamp.IsFinite()) {
		return timestamp;
	}
	date_t date;
	dtime_t time;
	Timestamp::Convert(timestamp, date, time);
	return Timestamp::FromDatetime(TruncateDate(date, ParsePart(part, "ossie_date_trunc")), dtime_t(0));
}

date_t AddDate(string_t part, int64_t amount, date_t date) {
	return Interval::Add(date, IntervalOf(ParsePart(part, "ossie_dateadd"), amount));
}

timestamp_t AddTimestamp(string_t part, int64_t amount, timestamp_t timestamp) {
	return Interval::Add(timestamp, IntervalOf(ParsePart(part, "ossie_dateadd"), amount));
}

void DateTruncDate(DataChunk &args, ExpressionState &, Vector &result) {
	BinaryExecutor::Execute<string_t, date_t, date_t>(args.data[0], args.data[1], result, args.size(), TruncDate);
}

void DateTruncTimestamp(DataChunk &args, ExpressionState &, Vector &result) {
	BinaryExecutor::Execute<string_t, timestamp_t, timestamp_t>(args.data[0], args.data[1], result, args.size(),
	                                                            TruncTimestamp);
}

void DateAddDate(DataChunk &args, ExpressionState &, Vector &result) {
	TernaryExecutor::Execute<string_t, int64_t, date_t, date_t>(args.data[0], args.data[1], args.data[2], result,
	                                                            args.size(), AddDate);
}

void DateAddTimestamp(DataChunk &args, ExpressionState &, Vector &result) {
	TernaryExecutor::Execute<string_t, int64_t, timestamp_t, timestamp_t>(args.data[0], args.data[1], args.data[2],
	                                                                      result, args.size(), AddTimestamp);
}

} // namespace

void RegisterDateFunctions(ExtensionLoader &loader) {
	ScalarFunctionSet date_trunc("ossie_date_trunc");
	ScalarFunction trunc_date({LogicalType::VARCHAR, LogicalType::DATE}, LogicalType::DATE, DateTruncDate);
	NameParameters(trunc_date.GetSignature(), {"part", "date_expr"});
	date_trunc.AddFunction(std::move(trunc_date));
	ScalarFunction trunc_timestamp({LogicalType::VARCHAR, LogicalType::TIMESTAMP}, LogicalType::TIMESTAMP,
	                               DateTruncTimestamp);
	NameParameters(trunc_timestamp.GetSignature(), {"part", "timestamp_expr"});
	date_trunc.AddFunction(std::move(trunc_timestamp));
	CreateScalarFunctionInfo trunc_info(std::move(date_trunc));
	auto trunc_description = "OSSIE_SQL_2026 DATE_TRUNC(part, date_expr) for the parts year, quarter, month, week "
	                         "and day: truncates and keeps the input type, so a DATE stays a DATE where DuckDB's "
	                         "date_trunc would give a TIMESTAMP at midnight. The model lowering emits this; it is "
	                         "also callable directly.";
	trunc_info.descriptions.push_back(Describe({"part", "date_expr"}, trunc_description,
	                                           {"SELECT ossie_date_trunc('month', DATE '2024-02-29')"},
	                                           {LogicalType::VARCHAR, LogicalType::DATE}));
	trunc_info.descriptions.push_back(Describe({"part", "timestamp_expr"}, trunc_description,
	                                           {"SELECT ossie_date_trunc('week', TIMESTAMP '2024-02-29 10:30:00')"},
	                                           {LogicalType::VARCHAR, LogicalType::TIMESTAMP}));
	loader.RegisterFunction(std::move(trunc_info));

	ScalarFunctionSet dateadd("ossie_dateadd");
	ScalarFunction add_date({LogicalType::VARCHAR, LogicalType::BIGINT, LogicalType::DATE}, LogicalType::DATE,
	                        DateAddDate);
	NameParameters(add_date.GetSignature(), {"part", "amount", "date_expr"});
	dateadd.AddFunction(std::move(add_date));
	ScalarFunction add_timestamp({LogicalType::VARCHAR, LogicalType::BIGINT, LogicalType::TIMESTAMP},
	                             LogicalType::TIMESTAMP, DateAddTimestamp);
	NameParameters(add_timestamp.GetSignature(), {"part", "amount", "timestamp_expr"});
	dateadd.AddFunction(std::move(add_timestamp));
	CreateScalarFunctionInfo add_info(std::move(dateadd));
	auto add_description = "OSSIE_SQL_2026 DATEADD(part, amount, date_expr) for the parts year, quarter, month, "
	                       "week and day: adds and keeps the input type, so a DATE stays a DATE where DuckDB's "
	                       "date_add would give a TIMESTAMP at midnight. Month arithmetic clamps to the last day "
	                       "of the month, as DuckDB's interval arithmetic does. The model lowering emits this; it "
	                       "is also callable directly.";
	add_info.descriptions.push_back(Describe({"part", "amount", "date_expr"}, add_description,
	                                         {"SELECT ossie_dateadd('month', 1, DATE '2024-01-31')"},
	                                         {LogicalType::VARCHAR, LogicalType::BIGINT, LogicalType::DATE}));
	add_info.descriptions.push_back(Describe({"part", "amount", "timestamp_expr"}, add_description,
	                                         {"SELECT ossie_dateadd('day', 30, TIMESTAMP '2024-01-31 10:30:00')"},
	                                         {LogicalType::VARCHAR, LogicalType::BIGINT, LogicalType::TIMESTAMP}));
	loader.RegisterFunction(std::move(add_info));
}

} // namespace ossie
} // namespace duckdb
