-- Written in this repository, not vendored.
--
-- apache/ossie's compliance/query cases are written for the Relational Query Interface: SQL that
-- reads a model's relations and evaluates a measure with AGG(). This extension has no such
-- interface, so no case can be submitted as its query.ossie.sql stands. What it has is the Wide Table
-- interface, ossie_query. This file gives, for each case whose question ONE ossie_query request can
-- ask, that request; scripts/query_compliance.py runs it against the SCOTT data and compares the
-- rows with the case's expected.sql.
--
-- A request here is one SELECT over one ossie_query call. Around the call it may order, limit,
-- select and rename columns, and compute scalars from them: what the case does to the values of
-- AGG(). It reads no table itself, and every measure value comes from the call.
--
-- A case with no such request has a `-- none:` line saying what stops it.

-- case: a01_grand_total
SELECT * FROM ossie_query('{"metrics": ["emp.sum_sal", "emp.emp_count"]}');

-- case: a02_measure_per_group
SELECT * FROM ossie_query('{"metrics": ["emp.sum_sal", "emp.emp_count"], "dimensions": ["emp.deptno"]}')
ORDER BY deptno;

-- case: a03_where_before_measure
SELECT * FROM ossie_query('{"metrics": ["emp.sum_sal", "emp.emp_count"], "dimensions": ["emp.deptno"], "filter": "emp.job <> ''CLERK''"}')
ORDER BY deptno;

-- case: a04_having_on_unselected_measure
-- HAVING is the named argument metric_filters, which may name a metric the request does not return.
SELECT * FROM ossie_query('{"metrics": ["emp.sum_sal"], "dimensions": ["emp.job"]}', metric_filters => ['emp.emp_count > 2'])
ORDER BY sum_sal DESC
LIMIT 3;

-- case: a05_scalar_ops_on_measure_values
SELECT deptno,
       CAST(sum_sal AS DOUBLE) / emp_count AS sal_per_emp,
       avg_sal,
       sum_sal + COALESCE(sum_comm, 0) AS total_pay
FROM ossie_query('{"metrics": ["emp.sum_sal", "emp.emp_count", "emp.avg_sal", "emp.sum_comm"], "dimensions": ["emp.deptno"]}')
ORDER BY deptno;

-- case: a06_nulls_follow_aggregate
SELECT * FROM ossie_query('{"metrics": ["emp.emp_count", "emp.comm_count", "emp.sum_comm", "emp.avg_comm"], "dimensions": ["emp.deptno"]}')
ORDER BY deptno;

-- case: a07_empty_input_set
SELECT * FROM ossie_query('{"metrics": ["emp.emp_count", "emp.sum_sal", "emp.avg_sal"], "filter": "emp.deptno = 40"}');

-- case: a08_group_by_all_skips_measures
-- none: the group is an expression, EXTRACT(YEAR FROM hiredate). A dimension is a field the model declares, and the model has no hire year.

-- case: a09_rollup_not_average_of_averages
-- none: ROLLUP is two grouping sets, by department and overall, and a request has one. Two requests give the two halves.

-- case: a10_cte_passes_measures_through
-- The CTE's WHERE is the filter and its renames are aliases.
SELECT * FROM ossie_query('{"metrics": [{"metric": "emp.sum_sal", "alias": "payroll"}, {"metric": "emp.emp_count", "alias": "headcount"}], "dimensions": ["emp.deptno"], "filter": "emp.job IN (''CLERK'', ''SALESMAN'')"}')
ORDER BY deptno;

-- case: a11_agg_in_where_subquery
-- none: the filter compares a field with a measure value from a subquery. A filter is a predicate over fields and may not hold a subquery.

-- case: a12_aggregate_over_agg_values
-- none: averages the values of a measure across departments and unions that with a second query. A request returns measure values; aggregating them again is the caller's SQL over two requests.

-- case: b01_many_to_one_no_fan_out
SELECT * FROM ossie_query('{"metrics": ["emp.sum_sal", "emp.emp_count"], "dimensions": ["dept.dname"]}')
ORDER BY dname;

-- case: b02_filter_on_joined_relation
SELECT * FROM ossie_query('{"metrics": ["emp.sum_sal", "emp.emp_count"], "dimensions": ["emp.job"], "filter": "dept.loc = ''NEW YORK''"}')
ORDER BY job;

-- case: b03_top_n_join_as_filter
-- none: keeps the employees of the two departments with the highest payroll, a relation computed from a measure. A filter may not hold a subquery, and a request joins only the model's datasets.

-- case: b04_one_side_counted_once
-- none: counts departments per job. dept does not reach emp (emp_to_dept is many-to-one), so the Wide Table specification refuses dept.dept_count by emp.job, and ossie_query does: E_PARTIAL_DIMENSION_REACHABILITY.

-- case: b05_manager_counted_once
-- none: joins emp to itself as employee and manager and reads measures of both. A request names a dataset, not a role of it.

-- case: b06_range_join_fan_out
-- none: joins on e.sal BETWEEN s.losal AND s.hisal. A request joins along the model's relationships, and the model has none between emp and salgrade.

-- case: b07_three_way_join_fan_out
-- none: the same range join to salgrade as b06.

-- case: b08_many_to_many_chasm_trap
-- none: joins emp to itself on deptno, a join the model has no relationship for, and reads measures of both roles.

-- case: b09_inner_join_matched_rows
-- none: reads measures of both sides of the employee-manager self-join, restricted by the INNER JOIN. A request has no roles and does not choose a join type.

-- case: b10_left_join_null_padding
-- none: as b09, with LEFT JOIN.

-- case: b11_right_join_keeps_non_managers
-- none: as b09, with RIGHT JOIN.

-- case: b12_full_join_each_row_once
-- none: as b09, with FULL JOIN.

-- case: b13_cross_join_no_inflation
-- none: as b09, with CROSS JOIN.

-- case: b14_ratio_across_relations
-- none: the INNER JOIN removes the department without employees from dept_count: 3, not 4. A request aggregates each metric over its own dataset and joins nothing between them, which is b15.

-- case: b15_left_join_changes_ratio
-- No dimension and no filter, so each metric is aggregated over its own dataset: every department, every employee.
SELECT dept_count, emp_count, sum_sal, CAST(sum_sal AS DOUBLE) / dept_count AS sal_per_dept
FROM ossie_query('{"metrics": ["dept.dept_count", "emp.emp_count", "emp.sum_sal"]}');

-- case: b16_on_filter_keeps_left_rows
-- none: the condition on e.job is in ON, so it restricts emp's measures and not dept's. A filter applies to every metric of a request, and dept does not reach emp: E_UNSUPPORTED_FILTER_PATH. Two requests give the two halves.

-- case: b17_left_join_empty_group
-- The groups of a request come from its metrics' datasets, so emp's metrics alone give no row for the
-- department without employees. dept.dept_count supplies that group and is not selected.
SELECT dname, emp_count, sum_sal, avg_sal
FROM ossie_query('{"metrics": ["emp.emp_count", "emp.sum_sal", "emp.avg_sal", "dept.dept_count"], "dimensions": ["dept.dname"]}')
ORDER BY dname;

-- case: b18_filter_restricts_one_side_measure
-- none: as b04, with a filter on emp that must restrict dept.dept_count too.

-- case: b19_nonunique_filter_no_fan_out
-- none: keeps the employees of departments that have a salesman, by joining a derived relation. A request joins only the model's datasets, and a filter may not hold a subquery.

-- case: b20_having_on_one_side_measure
-- none: as b04, with HAVING on dept.dept_count.
