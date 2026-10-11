#include "ossie/relation_program.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/query_result.hpp"
#include "duckdb/common/exception.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void Check(bool ok, const std::string &message) {
	if (!ok) {
		throw std::runtime_error(message);
	}
	checks++;
	std::cout << "PASS: " << message << '\n';
}
using namespace duckdb;
using namespace duckdb::ossie;
RelationProgramInput Input() {
	RelationProgramInput input;
	input.root = "DailyObservation";
	input.user_concept = "User";
	input.mapped_user_relation = "user";
	input.dataset = "observations";
	input.user_column = "user_id";
	input.scalars = {{"classification_value", "class_value", LogicalType::INTEGER},
	                 {"other_value", "other_value", LogicalType::INTEGER}};
	input.requires = {"DailyObservation.classification_value IS NULL OR DailyObservation.classification_value >= 0"};
	input.declarations = {{"qualified_user",
	                       "User",
	                       "ManyToOne",
	                       {"DailyObservation.classification_value >= 10 AND DailyObservation.user(User)"},
	                       {}},
	                      {"audited_user", "User", "ManyToOne", {"DailyObservation.qualified_user(User)"}, {}}};
	return input;
}
template <class F>
void Refuse(F action, const std::string &name) {
	bool refused = false;
	try {
		action();
	} catch (const InvalidInputException &) {
		refused = true;
	}
	Check(refused, name);
}
void BadRule(const std::string &rule, const std::string &name) {
	auto input = Input();
	input.declarations[0].derived_by = {rule};
	Refuse([&] { BindRelationProgram(input); }, name);
}
void Run() {
	auto input = Input();
	auto program = BindRelationProgram(input);
	Check(program.relations.size() == 2 && program.relations[0].name == "qualified_user" &&
	          program.relations[1].name == "audited_user",
	      "dependency bound before caller, not declaration order");
	Check(program.relations[1].dependencies == vector<string> {"qualified_user"} &&
	          program.relations[1].dependency_depth == 2,
	      "preserve declaration dependency and depth");
	Check(program.relations[1].root == input.root && program.relations[1].target_concept == "User",
	      "preserve typed root and target, not infer from string equality");
	std::reverse(input.declarations.begin(), input.declarations.end());
	auto reordered = BindRelationProgram(input);
	Check(reordered.relations[1].predicate->Equals(*program.relations[1].predicate),
	      "declaration order independent AST");
	input = Input();
	input.declarations[0].derived_by.push_back(
	    "DailyObservation.classification_value >= 3 AND DailyObservation.user(User)");
	auto multiple = BindRelationProgram(input);
	DuckDB database(nullptr);
	Connection engine(database);
	auto setup = engine.Query("CREATE TABLE observations(user_id VARCHAR,ds DATE,product VARCHAR,class_value "
	                          "INTEGER,other_value INTEGER); INSERT INTO observations VALUES "
	                          "('A','2026-01-01','X',10,0),('A','2026-01-01','Y',5,0),('A','2026-02-01','X',NULL,0),('"
	                          "B','2026-02-01','X',2,0),(NULL,'2026-02-01','X',20,0),('BAD','2026-02-01','X',-1,0)");
	Check(!setup->HasError(), "native predicate fixture created");
	auto rows = engine.Query("SELECT user_id,ds,product FROM observations WHERE " +
	                         multiple.relations[1].predicate->ToString() + " ORDER BY user_id,ds,product");
	Check(!rows->HasError(), "lowered dependent union binds in actual DuckDB");
	Check(rows->Cast<MaterializedQueryResult>().RowCount() == 2,
	      "downstream consumes all upstream union links, excludes NULL predicate and target");
	auto chunk = rows->Fetch();
	Check(chunk && chunk->GetValue(0, 0).ToString() == "A" && chunk->GetValue(2, 0).ToString() == "X" &&
	          chunk->GetValue(0, 1).ToString() == "A" && chunk->GetValue(2, 1).ToString() == "Y",
	      "same User retains distinct root date/product rows");
	auto truth = engine.Query("SELECT class_value," + program.root_constraints[0]->ToString() +
	                          " FROM observations WHERE user_id IS NOT NULL ORDER BY class_value NULLS FIRST");
	Check(!truth->HasError(), "explicit NULL constraint binds");
	auto values = truth->Fetch();
	Check(values->GetValue(1, 0).GetValue<bool>() && !values->GetValue(1, 1).GetValue<bool>() &&
	          values->GetValue(1, 2).GetValue<bool>(),
	      "allow-NULL guard true, negative false, positive true");
	input = Input();
	input.requires = {
	    "DailyObservation.classification_value IS NOT NULL AND DailyObservation.classification_value >= 0"};
	auto nonnull = BindRelationProgram(input);
	auto null_truth = engine.Query("SELECT " + nonnull.root_constraints[0]->ToString() +
	                               " FROM observations WHERE class_value IS NULL");
	Check(!null_truth->HasError() && !null_truth->Fetch()->GetValue(0, 0).GetValue<bool>(),
	      "reject-NULL guard differs from allow-NULL guard");
	auto table = engine.Query(
	    "WITH observations AS (SELECT * FROM (VALUES (NULL::INTEGER),(-1),(0),(1)) v(class_value)) SELECT " +
	    program.root_constraints[0]->ToString() + "," + nonnull.root_constraints[0]->ToString() +
	    " FROM observations ORDER BY class_value NULLS FIRST");
	Check(!table->HasError(), "NULL/negative/equal/above guard truth table binds");
	auto truth_table = table->Fetch();
	Check(truth_table->size() == 4 && truth_table->GetValue(0, 0).GetValue<bool>() &&
	          !truth_table->GetValue(1, 0).GetValue<bool>() && !truth_table->GetValue(0, 1).GetValue<bool>() &&
	          !truth_table->GetValue(1, 1).GetValue<bool>() && truth_table->GetValue(0, 2).GetValue<bool>() &&
	          truth_table->GetValue(1, 2).GetValue<bool>() && truth_table->GetValue(0, 3).GetValue<bool>() &&
	          truth_table->GetValue(1, 3).GetValue<bool>(),
	      "both explicit guard modes have complete non-UNKNOWN truth tables");
	auto absence = engine.Query("SELECT " + program.relations[1].predicate->ToString() +
	                            " FROM observations WHERE class_value IS NULL");
	Check(!absence->HasError() && !absence->Fetch()->GetValue(0, 0).GetValue<bool>(),
	      "upstream UNKNOWN becomes absent link, boolean false downstream");
	auto violations = engine.Query("SELECT count(*) FROM observations WHERE (" +
	                               program.root_constraints[0]->ToString() + ") IS FALSE");
	Check(!violations->HasError() && violations->Fetch()->GetValue(0, 0).GetValue<int64_t>() == 1,
	      "full root constraint catches excluded negative row");
	for (auto &rule : vector<string> {
	         "DailyObservation.classification_value >= 10",
	         "DailyObservation.user(User) OR DailyObservation.classification_value >= 10",
	         "NOT DailyObservation.user(User)", "DailyObservation.user('User')", "DailyObservation.user(Other)",
	         "Other.user(User)", "DailyObservation.missing(User)", "DailyObservation.user(User,User)",
	         "DailyObservation.user(DISTINCT User)", "DailyObservation.user(User) FILTER (WHERE true)",
	         "DailyObservation.user(User ORDER BY User)", "system.DailyObservation.user(User)",
	         "DailyObservation.user(User) AND DailyObservation.classification_value = User",
	         "\"DailyObservation\".user(User)", "DailyObservation.user(User) AND EXISTS(SELECT 1)",
	         "DailyObservation.user(User) AND $1", "LAG(DailyObservation.classification_value) OVER() >= 10",
	         "DailyObservation.user(User) AND DailyObservation.classification_value >= 1.5",
	         "DailyObservation.user(User) AND DailyObservation.classification_value >= NULL"}) {
		BadRule(rule, "reject unsupported/unanchored rule: " + rule);
	}
	input = Input();
	input.declarations[0].derived_by = {"DailyObservation.audited_user(User)"};
	Refuse([&] { BindRelationProgram(input); }, "indirect cycle rejected");
	input.declarations[0].derived_by = {"DailyObservation.qualified_user(User)"};
	Refuse([&] { BindRelationProgram(input); }, "direct cycle rejected");
	input = Input();
	input.declarations[1].name = "QUALIFIED_USER";
	Refuse([&] { BindRelationProgram(input); }, "case-normalized duplicate declaration rejected");
	input = Input();
	input.declarations[0].name = "user";
	Refuse([&] { BindRelationProgram(input); }, "mapped relation shadow rejected");
	input = Input();
	input.declarations[0].role_concept = "Other";
	Refuse([&] { BindRelationProgram(input); }, "wrong declared role rejected");
	input = Input();
	input.declarations[0].multiplicity = "ManyToMany";
	Refuse([&] { BindRelationProgram(input); }, "multi-target declaration rejected");
	input = Input();
	input.scalars[0].type = LogicalType::VARCHAR;
	Refuse([&] { BindRelationProgram(input); }, "no implicit string scalar comparison");
	input = Input();
	input.scalars[0].type = LogicalType::TINYINT;
	input.declarations[0].derived_by = {"DailyObservation.classification_value >= 128 AND DailyObservation.user(User)"};
	Refuse([&] { BindRelationProgram(input); }, "out-of-range declared integral constant rejected");
	for (auto &constraint : vector<string> {
	         "DailyObservation.classification_value >= 0",
	         "DailyObservation.other_value IS NULL OR DailyObservation.classification_value >= 0",
	         "DailyObservation.classification_value IS NOT NULL OR DailyObservation.classification_value >= 0",
	         "DailyObservation.classification_value IS NULL AND DailyObservation.classification_value >= 0"}) {
		input = Input();
		input.requires = {constraint};
		Refuse([&] { BindRelationProgram(input); }, "reject non-total/wrong-field guard: " + constraint);
	}
	input = Input();
	input.declarations[0].name = "eligible";
	input.declarations[1].derived_by = {"DailyObservation.eligible(User)"};
	auto renamed = BindRelationProgram(input);
	Check(renamed.relations[1].predicate->Equals(*program.relations[1].predicate),
	      "relationship rename preserves semantics");
	input = Input();
	input.declarations[0].derived_by = {"dailyobservation.classification_value >= 10 AND dailyobservation.USER(user)"};
	Check(BindRelationProgram(input).relations[1].predicate->Equals(*program.relations[1].predicate),
	      "unquoted identifier normalization declared explicitly");
	input = Input();
	input.declarations.clear();
	for (int i = 0; i < 17; i++) {
		auto previous = i ? "r" + std::to_string(i - 1) : "user";
		input.declarations.push_back(
		    {"r" + std::to_string(i), "User", "ManyToOne", {"DailyObservation." + previous + "(User)"}, {}});
	}
	Refuse([&] { BindRelationProgram(input); }, "seventeen-level chain rejected even when upstream cached");
	input.declarations.pop_back();
	auto sixteen = BindRelationProgram(input);
	idx_t maximum_depth = 0;
	for (auto &relation : sixteen.relations) {
		maximum_depth = MaxValue(maximum_depth, relation.dependency_depth);
	}
	Check(maximum_depth == 16, "sixteen dependency levels accepted at boundary");
	input = Input();
	input.declarations.push_back({"mirror", "User", "ManyToOne", {"DailyObservation.qualified_user(User)"}, {}});
	input.declarations.push_back({"diamond",
	                              "User",
	                              "ManyToOne",
	                              {"DailyObservation.audited_user(User) AND DailyObservation.mirror(User)"},
	                              {}});
	auto diamond = BindRelationProgram(input);
	bool preserved_diamond = false;
	for (auto &relation : diamond.relations) {
		if (relation.name == "diamond") {
			preserved_diamond = relation.dependencies.size() == 2 && relation.dependency_depth == 3;
		}
	}
	Check(preserved_diamond, "diamond dependencies retained without treating repeated upstream as cycle");
	input = Input();
	input.declarations[0].derived_by = {"DailyObservation.classification_value >= 20 AND DailyObservation.user(User)"};
	auto twenty = BindRelationProgram(input);
	auto threshold =
	    engine.Query("SELECT count(*) FROM observations WHERE " + twenty.relations[1].predicate->ToString());
	Check(!threshold->HasError() && threshold->Fetch()->GetValue(0, 0).GetValue<int64_t>() == 0,
	      "changed upstream threshold propagates to dependent membership");
	input = Input();
	input.declarations.clear();
	for (int i = 0; i < 33; i++) {
		input.declarations.push_back(
		    {"r" + std::to_string(i), "User", "ManyToOne", {"DailyObservation.user(User)"}, {}});
	}
	Refuse([&] { BindRelationProgram(input); }, "thirty-three declarations refused");
	input = Input();
	input.declarations.clear();
	for (int i = 0; i < 12; i++) {
		auto call = "DailyObservation." + (i ? "r" + std::to_string(i - 1) : "user") + "(User)";
		input.declarations.push_back({"r" + std::to_string(i), "User", "ManyToOne", {call + " AND " + call}, {}});
	}
	Refuse([&] { BindRelationProgram(input); }, "exponential expansion refused before oversized predicate copy");
	input = Input();
	input.declarations[0].derived_by = {std::string(16385, ' ')};
	Refuse([&] { BindRelationProgram(input); }, "rule byte budget checked before parse");
}
} // namespace
int main() {
	try {
		Run();
		std::cout << "PASS relation program " << checks << " checks\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
