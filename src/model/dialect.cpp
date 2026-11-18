#include "ossie/dialect.hpp"
#include "ossie/names.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/to_string.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/unordered_set.hpp"
#include "duckdb/parser/expression/case_expression.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/conjunction_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
#include "duckdb/parser/expression/type_expression.hpp"
#include "duckdb/parser/expression/window_expression.hpp"
#include "duckdb/parser/literal.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"

// The Ossie expression language (core-spec/expression_language.md) is a SQL subset. Most of it is
// spelled the same in DuckDB, so the tree DuckDB's parser produces is already right. This file handles
// the rest, in four kinds:
//
//   * a function DuckDB spells differently or lacks (NVL, IF, IFF, CHARINDEX, DATEADD, ...) becomes the
//     equivalent DuckDB tree. CURRENT_DATE, CURRENT_TIMESTAMP and CURRENT_TIME are among them: the
//     language writes each with or without parentheses and gives them the types DATE, TIMESTAMP and
//     TIME, where DuckDB has one of the six spellings as a function and puts a time zone on two;
//   * a function DuckDB has under the same name but with a different result is wrapped so the result
//     matches: CONCAT, GREATEST and LEAST skip NULL arguments in DuckDB, while NULL in gives NULL out
//     for the language's own `||` and in standard SQL; DATE_TRUNC and DATEADD on a DATE give a DATE,
//     through the extension's own functions, where DuckDB's give a TIMESTAMP;
//   * a construct whose result the language does not define, and on which engines disagree, is
//     refused at load. Guessing would return a number that is right on one engine and wrong on the
//     next, which is the one outcome this extension exists to prevent;
//   * a function the language does not have at all is refused at load, even when DuckDB knows the name.
//     The expression would run here and nowhere else, and the model's claim to portability would be
//     false without anyone noticing. An ANSI_SQL variant is the place for engine-specific SQL.

namespace duckdb {
namespace ossie {

namespace {

using Children = vector<unique_ptr<ParsedExpression>>;

unique_ptr<ParsedExpression> Call(const string &name, Children children) {
	return make_uniq<FunctionExpression>(Identifier(name), std::move(children));
}

unique_ptr<ParsedExpression> Concatenate(unique_ptr<ParsedExpression> left, unique_ptr<ParsedExpression> right) {
	Children children;
	children.push_back(std::move(left));
	children.push_back(std::move(right));
	return make_uniq<FunctionExpression>(Identifier("||"), std::move(children), nullptr, nullptr, false, true);
}

unique_ptr<ParsedExpression> IsNull(unique_ptr<ParsedExpression> operand) {
	Children children;
	children.push_back(std::move(operand));
	return make_uniq<OperatorExpression>(ExpressionType::OPERATOR_IS_NULL, std::move(children));
}

//! COALESCE(left, right): the tree DuckDB's own parser builds for IFNULL, which is not a function in its
//! catalog but an operator.
unique_ptr<ParsedExpression> Coalesce(unique_ptr<ParsedExpression> left, unique_ptr<ParsedExpression> right) {
	auto result = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_COALESCE);
	result->GetChildrenMutable().push_back(std::move(left));
	result->GetChildrenMutable().push_back(std::move(right));
	return std::move(result);
}

unique_ptr<ParsedExpression> Null() {
	return ConstantExpression::Null();
}

unique_ptr<ParsedExpression> Integer(int32_t value) {
	return ConstantExpression::Integer(value);
}

//! CASE WHEN `condition` THEN `then_expr` ELSE `else_expr` END
unique_ptr<ParsedExpression> Case(unique_ptr<ParsedExpression> condition, unique_ptr<ParsedExpression> then_expr,
                                  unique_ptr<ParsedExpression> else_expr) {
	auto result = make_uniq<CaseExpression>();
	CaseCheck check;
	check.when_expr = std::move(condition);
	check.then_expr = std::move(then_expr);
	result->CaseChecksMutable().push_back(std::move(check));
	result->ElseMutable() = std::move(else_expr);
	return std::move(result);
}

//! NULL if any argument is NULL, otherwise `expr`. The arguments are copied, so `expr` keeps its own.
unique_ptr<ParsedExpression> NullIfAnyNull(const Children &arguments, unique_ptr<ParsedExpression> expr) {
	unique_ptr<ParsedExpression> any_null;
	for (auto &argument : arguments) {
		auto test = IsNull(argument->Copy());
		any_null = any_null ? make_uniq<ConjunctionExpression>(ExpressionType::CONJUNCTION_OR, std::move(any_null),
		                                                       std::move(test))
		                    : std::move(test);
	}
	return Case(std::move(any_null), Null(), std::move(expr));
}

void RequireArgumentCount(const Children &args, const string &name, idx_t minimum, idx_t maximum,
                          const string &spelling, const string &context) {
	auto count = args.size();
	if (count < minimum || count > maximum) {
		throw InvalidInputException("%s calls %s with %s argument(s); OSSIE_SQL_2026 defines it as %s", context,
		                            StringUtil::Upper(name), to_string(count), spelling);
	}
}

//! The date part of DATEADD, DATEDIFF or DATE_TRUNC. The language writes it bare (`DATEADD(day, 7, d)`),
//! which DuckDB's parser reads as a column named `day`; the quoted form (`'day'`) is accepted as well.
string DatePart(const ParsedExpression &argument, const string &function_name, const string &context) {
	string part;
	if (argument.GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto names = ColumnNames(argument.Cast<ColumnRefExpression>());
		if (names.size() == 1) {
			part = names[0];
		}
	} else if (argument.GetExpressionClass() == ExpressionClass::CONSTANT) {
		auto &literal = argument.Cast<ConstantExpression>().GetLiteral();
		if (literal.kind == LiteralKind::STRING) {
			part = literal.text;
		}
	}
	part = StringUtil::Lower(part);
	static const char *const PARTS[] = {"year", "quarter", "month", "week", "day", "hour", "minute", "second"};
	for (auto known : PARTS) {
		if (part == known) {
			return part;
		}
	}
	throw InvalidInputException("%s calls %s with date part %s; OSSIE_SQL_2026 defines year, "
	                            "quarter, month, week, day, hour, minute and second",
	                            context, StringUtil::Upper(function_name), argument.ToString());
}

//! A part that can put a time of day on a DATE. For the others, DATE_TRUNC and DATEADD go through the
//! extension's own ossie_date_trunc and ossie_dateadd, which give a DATE back for a DATE in; DuckDB's
//! date_trunc and date_add would give a TIMESTAMP at midnight.
bool IsSubDayPart(const string &part) {
	return part == "hour" || part == "minute" || part == "second";
}

//! True for the three names the language writes with or without parentheses, in lower case.
bool IsCurrentDateTimeName(const string &name) {
	return name == "current_date" || name == "current_timestamp" || name == "current_time";
}

//! CURRENT_DATE, CURRENT_TIMESTAMP or CURRENT_TIME, as the DATE, TIMESTAMP or TIME the language says it is.
//! All three are read off the one clock DuckDB has without an extension, get_current_timestamp(), which is
//! a TIMESTAMP WITH TIME ZONE: its own current_date and the wall-clock functions come with ICU, which a
//! build need not carry. The cast to TIMESTAMP is the wall clock of the session's time zone with ICU
//! loaded, and of UTC without it. There is no cast from a TIMESTAMP WITH TIME ZONE to a TIME, so the date
//! and the time are taken from that wall clock.
unique_ptr<ParsedExpression> CurrentDateTime(const string &name) {
	unique_ptr<ParsedExpression> wall_clock =
	    make_uniq<CastExpression>(LogicalType::TIMESTAMP, Call("get_current_timestamp", Children()));
	if (name == "current_timestamp") {
		return wall_clock;
	}
	return make_uniq<CastExpression>(name == "current_date" ? LogicalType::DATE : LogicalType::TIME,
	                                 std::move(wall_clock));
}

//! The name of a column reference that could be one of the language's keywords: a single part, not written
//! in quotes. Lower-cased; empty otherwise.
string BareName(const ColumnRefExpression &colref, const unordered_set<string> &quoted) {
	auto names = ColumnNames(colref);
	if (names.size() != 1 || quoted.count(names[0]) > 0) {
		return string();
	}
	return StringUtil::Lower(names[0]);
}

//! Every function the language defines, spelled the way DuckDB's parser hands it over: COUNT(*) arrives
//! as count_star, PERCENTILE_CONT(p) WITHIN GROUP (ORDER BY x) as quantile_cont, EXTRACT(part FROM d) as
//! date_part. IFNULL never reaches here; the parser turns it into COALESCE itself.
bool IsLanguageFunction(const string &name) {
	static const unordered_set<string> NAMES = {
	    // aggregates
	    "sum", "count", "count_star", "avg", "min", "max", "stddev", "stddev_pop", "stddev_samp", "variance", "var_pop",
	    "var_samp", "median", "quantile_cont", "quantile_disc", "approx_count_distinct", "approx_percentile",
	    // date and time
	    "current_date", "current_timestamp", "current_time", "year", "quarter", "month", "day", "dayofyear", "hour",
	    "minute", "second", "date_part", "date_trunc", "dateadd", "datediff", "to_date", "to_timestamp", "to_char",
	    // strings
	    "concat", "length", "lower", "upper", "trim", "ltrim", "rtrim", "left", "right", "substring", "replace",
	    "split_part", "position", "charindex", "contains", "startswith", "endswith", "regexp_like", "regexp_extract",
	    "regexp_replace", "regexp_count",
	    // math
	    "abs", "round", "floor", "ceil", "ceiling", "trunc", "truncate", "mod", "sign", "power", "sqrt", "exp", "ln",
	    "log", "log10", "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "radians", "degrees", "pi", "greatest",
	    "least",
	    // conditional
	    "if", "iff", "nullif", "coalesce", "nvl", "nvl2", "zeroifnull", "nullifzero",
	    // AGG(dataset.metric), which the parser replaces with the dataset-scoped metric's expression
	    "agg"};
	return NAMES.count(name) > 0;
}

bool IsWindowFunction(const string &name) {
	static const unordered_set<string> NAMES = {"row_number",   "rank",       "dense_rank", "ntile",
	                                            "percent_rank", "cume_dist",  "lag",        "lead",
	                                            "first_value",  "last_value", "nth_value"};
	return NAMES.count(name) > 0;
}

//! What DuckDB's parser makes of `INTERVAL 7 DAY`.
bool IsIntervalConstructor(const string &name) {
	static const unordered_set<string> NAMES = {
	    "to_millennia", "to_centuries", "to_decades", "to_years",   "to_quarters",     "to_months",      "to_weeks",
	    "to_days",      "to_hours",     "to_minutes", "to_seconds", "to_milliseconds", "to_microseconds"};
	return NAMES.count(name) > 0;
}

void RequireLanguageFunction(const string &name, const string &context) {
	if (IsLanguageFunction(name)) {
		return;
	}
	if (IsIntervalConstructor(name)) {
		throw InvalidInputException("%s uses an INTERVAL literal, which OSSIE_SQL_2026 does not define; write "
		                            "DATEADD(part, amount, date_expr), or give the expression an ANSI_SQL variant",
		                            context);
	}
	throw InvalidInputException("%s calls %s, which OSSIE_SQL_2026 does not define; give the expression an ANSI_SQL "
	                            "variant, which reaches DuckDB as written",
	                            context, StringUtil::Upper(name));
}

//! Returns the replacement for `function`, or nullptr to keep it as it is.
unique_ptr<ParsedExpression> LowerFunction(FunctionExpression &function, const string &context) {
	if (function.IsOperator()) {
		return nullptr;
	}
	auto name = StringUtil::Lower(function.FunctionName().GetIdentifierName());
	RequireLanguageFunction(name, context);
	auto &qualified = function.GetQualifiedName();
	if (!qualified.Schema().empty() || !qualified.Catalog().empty()) {
		// POSITION(x IN y), SUBSTRING(s FROM a FOR b), TRIM(BOTH ...) and EXTRACT(part FROM d) arrive qualified
		// with DuckDB's own schema; the names are the language's, and DuckDB already spells them right.
		return nullptr;
	}
	static const char *const LOWERED[] = {"nvl",          "if",           "iff",
	                                      "nvl2",         "zeroifnull",   "nullifzero",
	                                      "charindex",    "startswith",   "endswith",
	                                      "to_date",      "to_timestamp", "dateadd",
	                                      "datediff",     "date_trunc",   "concat",
	                                      "greatest",     "least",        "regexp_like",
	                                      "to_char",      "truncate",     "approx_percentile",
	                                      "regexp_count", "current_date", "current_timestamp",
	                                      "current_time"};
	bool lowered = false;
	for (auto candidate : LOWERED) {
		lowered = lowered || name == candidate;
	}
	if (!lowered) {
		return nullptr;
	}
	// Only now take the arguments out: a call left as it is must keep them. A named argument (name := value)
	// is not part of the language, so such a call is left for DuckDB's binder to judge.
	Children args;
	for (auto &argument : function.GetArgumentsMutable()) {
		if (argument.HasName()) {
			return nullptr;
		}
	}
	for (auto &argument : function.GetArgumentsMutable()) {
		args.push_back(std::move(argument.GetExpressionMutable()));
	}

	if (name == "nvl") {
		RequireArgumentCount(args, name, 2, 2, "NVL(expr, default)", context);
		return Coalesce(std::move(args[0]), std::move(args[1]));
	}
	if (name == "if" || name == "iff") {
		// IFF is IF under another name. DuckDB has IF as a function and no IFF; both become the same CASE.
		RequireArgumentCount(args, name, 3, 3, StringUtil::Upper(name) + "(condition, true_result, false_result)",
		                     context);
		return Case(std::move(args[0]), std::move(args[1]), std::move(args[2]));
	}
	if (IsCurrentDateTimeName(name)) {
		RequireArgumentCount(args, name, 0, 0, StringUtil::Upper(name) + " or " + StringUtil::Upper(name) + "()",
		                     context);
		return CurrentDateTime(name);
	}
	if (name == "nvl2") {
		RequireArgumentCount(args, name, 3, 3, "NVL2(expr, not_null_result, null_result)", context);
		return Case(IsNull(std::move(args[0])), std::move(args[2]), std::move(args[1]));
	}
	if (name == "zeroifnull") {
		RequireArgumentCount(args, name, 1, 1, "ZEROIFNULL(expr)", context);
		return Coalesce(std::move(args[0]), Integer(0));
	}
	if (name == "nullifzero") {
		RequireArgumentCount(args, name, 1, 1, "NULLIFZERO(expr)", context);
		args.push_back(Integer(0));
		return Call("nullif", std::move(args));
	}
	if (name == "charindex") {
		RequireArgumentCount(args, name, 2, 2, "CHARINDEX(substr, str)", context);
		Children swapped;
		swapped.push_back(std::move(args[1]));
		swapped.push_back(std::move(args[0]));
		return Call("strpos", std::move(swapped));
	}
	if (name == "startswith") {
		RequireArgumentCount(args, name, 2, 2, "STARTSWITH(str, prefix)", context);
		return Call("starts_with", std::move(args));
	}
	if (name == "endswith") {
		RequireArgumentCount(args, name, 2, 2, "ENDSWITH(str, suffix)", context);
		return Call("suffix", std::move(args));
	}
	if (name == "to_date" || name == "to_timestamp") {
		auto is_date = name == "to_date";
		if (args.size() != 1) {
			throw InvalidInputException(
			    "%s calls %s with a format argument. Format models differ between engines and "
			    "the format-string form is EXPERIMENTAL in OSSIE_SQL_2026; write the value as an ISO-8601 "
			    "string, which %s(string) parses the same everywhere",
			    context, StringUtil::Upper(name), StringUtil::Upper(name));
		}
		return make_uniq<CastExpression>(is_date ? LogicalType::DATE : LogicalType::TIMESTAMP, std::move(args[0]));
	}
	if (name == "dateadd") {
		RequireArgumentCount(args, name, 3, 3, "DATEADD(part, amount, date_expr)", context);
		auto part = DatePart(*args[0], name, context);
		if (IsSubDayPart(part)) {
			Children interval;
			interval.push_back(std::move(args[1]));
			Children added;
			added.push_back(std::move(args[2]));
			added.push_back(Call("to_" + part + "s", std::move(interval)));
			return Call("date_add", std::move(added));
		}
		args[0] = ConstantExpression::String(part);
		return Call("ossie_dateadd", std::move(args));
	}
	if (name == "datediff" || name == "date_trunc") {
		auto is_diff = name == "datediff";
		RequireArgumentCount(args, name, is_diff ? 3 : 2, is_diff ? 3 : 2,
		                     is_diff ? "DATEDIFF(part, start_date, end_date)" : "DATE_TRUNC(part, date_expr)", context);
		auto part = DatePart(*args[0], name, context);
		args[0] = ConstantExpression::String(part);
		return Call(!is_diff && !IsSubDayPart(part) ? "ossie_date_trunc" : name, std::move(args));
	}
	if (name == "concat") {
		if (args.empty()) {
			RequireArgumentCount(args, name, 1, NumericLimits<idx_t>::Maximum(), "CONCAT(str1, str2, ...)", context);
		}
		auto result = std::move(args[0]);
		for (idx_t i = 1; i < args.size(); i++) {
			result = Concatenate(std::move(result), std::move(args[i]));
		}
		return result;
	}
	if (name == "greatest" || name == "least") {
		Children copies;
		for (auto &argument : args) {
			copies.push_back(argument->Copy());
		}
		return NullIfAnyNull(args, Call(name, std::move(copies)));
	}
	if (name == "regexp_like") {
		throw InvalidInputException(
		    "%s calls REGEXP_LIKE, whose result OSSIE_SQL_2026 does not define: engines disagree on "
		    "whether the pattern must match the whole string (Snowflake) or any part of it (Databricks, "
		    "BigQuery). Use LIKE, or give the expression an ANSI_SQL variant that says which you mean",
		    context);
	}
	if (name == "to_char") {
		throw InvalidInputException(
		    "%s calls TO_CHAR, whose format model is EXPERIMENTAL in OSSIE_SQL_2026 and differs between engines; "
		    "give the expression an ANSI_SQL variant that uses strftime",
		    context);
	}
	if (name == "truncate") {
		RequireArgumentCount(args, name, 1, 2, "TRUNCATE(x, d)", context);
		return Call("trunc", std::move(args));
	}
	if (name == "approx_percentile") {
		RequireArgumentCount(args, name, 2, 2, "APPROX_PERCENTILE(expr, p)", context);
		return Call("approx_quantile", std::move(args));
	}
	if (name == "regexp_count") {
		RequireArgumentCount(args, name, 2, 2, "REGEXP_COUNT(str, pattern)", context);
		Children matches;
		matches.push_back(Call("regexp_extract_all", std::move(args)));
		return Call("len", std::move(matches));
	}
	return nullptr;
}

//! TIMESTAMP_NTZ is the language's wall-clock timestamp; DuckDB calls it TIMESTAMP.
void LowerCast(CastExpression &cast) {
	auto &target = cast.TargetType();
	if (!target.GetSchema().empty() || !target.GetCatalog().empty()) {
		return;
	}
	if (StringUtil::CIEquals(target.GetTypeName().GetIdentifierName(), "timestamp_ntz")) {
		cast.SetTargetType(TypeExpression::FromLogicalType(LogicalType::TIMESTAMP));
	}
}

void Lower(unique_ptr<ParsedExpression> &expr, const unordered_set<string> &quoted, const string &context) {
	ParsedExpressionIterator::EnumerateChildren(
	    *expr, [&](unique_ptr<ParsedExpression> &child) { Lower(child, quoted, context); });
	switch (expr->GetExpressionClass()) {
	case ExpressionClass::COLUMN_REF: {
		// Without parentheses CURRENT_DATE, CURRENT_TIMESTAMP and CURRENT_TIME arrive as column references.
		auto name = BareName(expr->Cast<ColumnRefExpression>(), quoted);
		if (IsCurrentDateTimeName(name)) {
			auto replacement = CurrentDateTime(name);
			replacement->SetAlias(expr->GetAlias());
			expr = std::move(replacement);
		}
		break;
	}
	case ExpressionClass::FUNCTION: {
		auto replacement = LowerFunction(expr->Cast<FunctionExpression>(), context);
		if (replacement) {
			replacement->SetAlias(expr->GetAlias());
			expr = std::move(replacement);
		}
		break;
	}
	case ExpressionClass::CAST:
		LowerCast(expr->Cast<CastExpression>());
		break;
	case ExpressionClass::WINDOW: {
		auto name = StringUtil::Lower(expr->Cast<WindowExpression>().FunctionName().GetIdentifierName());
		if (!IsWindowFunction(name)) {
			RequireLanguageFunction(name, context);
		}
		break;
	}
	default:
		break;
	}
}

void LowerAnsi(unique_ptr<ParsedExpression> &expr, const unordered_set<string> &quoted) {
	if (expr->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		ParsedExpressionIterator::EnumerateChildren(
		    *expr, [&](unique_ptr<ParsedExpression> &child) { LowerAnsi(child, quoted); });
		return;
	}
	// The functions DuckDB's own binder calls for these names, so the types are DuckDB's.
	auto name = BareName(expr->Cast<ColumnRefExpression>(), quoted);
	const char *function = nullptr;
	if (name == "current_date") {
		function = "current_date";
	} else if (name == "current_timestamp") {
		function = "get_current_timestamp";
	} else if (name == "current_time") {
		function = "get_current_time";
	}
	if (function) {
		auto replacement = Call(function, Children());
		replacement->SetAlias(expr->GetAlias());
		expr = std::move(replacement);
	}
}

} // namespace

void LowerOssieSql2026(unique_ptr<ParsedExpression> &expr, const string &sql, const string &context) {
	Lower(expr, QuotedIdentifiers(sql), context);
}

void LowerAnsiSql(unique_ptr<ParsedExpression> &expr, const string &sql) {
	LowerAnsi(expr, QuotedIdentifiers(sql));
}

bool IsCurrentDateTime(const string &name) {
	return IsCurrentDateTimeName(StringUtil::Lower(name));
}

unordered_set<string> QuotedIdentifiers(const string &sql) {
	unordered_set<string> result;
	for (idx_t i = 0; i < sql.size(); i++) {
		auto c = sql[i];
		if (c == '\'') {
			for (i++; i < sql.size(); i++) {
				if (sql[i] != '\'') {
					continue;
				}
				if (i + 1 < sql.size() && sql[i + 1] == '\'') {
					i++;
					continue;
				}
				break;
			}
		} else if (c == '"') {
			string name;
			for (i++; i < sql.size(); i++) {
				if (sql[i] != '"') {
					name += sql[i];
					continue;
				}
				if (i + 1 < sql.size() && sql[i + 1] == '"') {
					name += '"';
					i++;
					continue;
				}
				break;
			}
			result.insert(name);
		} else if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
			while (i < sql.size() && sql[i] != '\n') {
				i++;
			}
		}
	}
	return result;
}

} // namespace ossie
} // namespace duckdb
