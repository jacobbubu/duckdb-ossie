-- Counterexamples in ordinary SQL, not product input-validation tests.
CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER);
COPY observations FROM 'docs/research/ossie-req-001/multi-period-research/every-day/observations.csv' (HEADER);
SELECT 'empty_required_dates_vacuous_match' AS control,count(DISTINCT user_id) AS actual_users
FROM observations WHERE NOT EXISTS (SELECT 1 FROM range(0));
SELECT 'empty_product_set_has_no_witness' AS control,count(DISTINCT user_id) AS actual_users
FROM observations WHERE list_contains([]::VARCHAR[],product) AND classification_value>=10;
WITH exact_duplicate AS (SELECT * FROM observations UNION ALL SELECT * FROM observations WHERE observation_id='j1'),
conflicting_duplicate AS (SELECT * FROM observations UNION ALL SELECT observation_id,user_id,ds,product,999.00,classification_value FROM observations WHERE observation_id='j1')
SELECT 'outside_interval_exact_duplicate' AS control,count(*) AS duplicate_tuples FROM (SELECT user_id,ds,product FROM exact_duplicate GROUP BY user_id,ds,product HAVING count(*)>1)
UNION ALL SELECT 'outside_interval_conflicting_duplicate',count(*) FROM (SELECT user_id,ds,product FROM conflicting_duplicate GROUP BY user_id,ds,product HAVING count(*)>1);
