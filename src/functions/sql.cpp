#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "ossie/describe_functions.hpp"
#include "ossie/functions.hpp"
#include "ossie/relational.hpp"
#include "ossie/state.hpp"

namespace duckdb {
namespace ossie {

namespace {

// As for ossie_query, the compiled SELECT replaces the call before binding, so DuckDB plans it as though it
// had been written by hand.
unique_ptr<TableRef> SqlBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	auto snapshot = OssieState::Get(context).GetSnapshot();
	if (!snapshot.model) {
		throw InvalidInputException("ossie_sql: no Ossie model is loaded -- call ossie_load('model.json') first");
	}
	if (input.inputs[0].IsNull()) {
		throw InvalidInputException("ossie_sql: the query is NULL");
	}
	auto statement = CompileRelational(context, *snapshot.model, input.inputs[0].GetValue<string>());
	return make_uniq<SubqueryRef>(std::move(statement));
}

} // namespace

void RegisterSqlFunction(ExtensionLoader &loader) {
	TableFunction ossie_sql("ossie_sql", {LogicalType::VARCHAR}, nullptr);
	ossie_sql.bind_replace = SqlBindReplace;
	NameParameters(ossie_sql.GetSignature(), {"query"});
	CreateTableFunctionInfo info(std::move(ossie_sql));
	info.descriptions.push_back(
	    Describe({"query"},
	             "Run one SELECT written against the loaded model's relations. A dataset is the relation "
	             "model.dataset: its fields are its columns, and AGG(measure) evaluates one of its dataset-scoped "
	             "metrics over the rows of a group, in SELECT, HAVING or ORDER BY. The rest of the query is "
	             "DuckDB SQL and runs with the caller's rights, so it can read any table the caller can. "
	             "Evaluating a measure is supported where the FROM clause of its query block is one model "
	             "relation; over a join, and for a measure passed through unevaluated, the query is refused.",
	             {"SELECT * FROM ossie_sql('SELECT deptno, AGG(sum_sal) AS payroll FROM scott_model.emp GROUP BY "
	              "deptno')",
	              "SELECT * FROM ossie_sql('SELECT job, AGG(emp_count) AS staff FROM scott_model.emp WHERE sal > "
	              "1000 GROUP BY job HAVING AGG(emp_count) > 2')"},
	             {LogicalType::VARCHAR}));
	loader.RegisterFunction(std::move(info));
}

} // namespace ossie
} // namespace duckdb
