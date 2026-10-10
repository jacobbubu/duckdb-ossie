-- Independent contract checks; no extension/profile is loaded.
CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER);
COPY observations FROM 'docs/research/ossie-req-001/multi-period-research/observations.csv' (HEADER);
SELECT 'empty' AS scope,count(*) AS observation_count,count(DISTINCT user_id) AS user_count,count(amount_usd) AS amount_value_count,sum(amount_usd)::VARCHAR AS amount_sum,avg(amount_usd) AS mean_observation_amount FROM observations WHERE FALSE
UNION ALL SELECT 'null-only-C',count(*),count(DISTINCT user_id),count(amount_usd),sum(amount_usd)::VARCHAR,avg(amount_usd) FROM observations WHERE user_id='C' AND classification_value>=10
UNION ALL SELECT 'qualified-X',count(*),count(DISTINCT user_id),count(amount_usd),sum(amount_usd)::VARCHAR,avg(amount_usd) FROM observations WHERE classification_value>=10 AND product='X';
WITH variants AS (
 SELECT 'identical-duplicate' AS test,* FROM observations UNION ALL SELECT 'identical-duplicate',* FROM observations WHERE observation_id='o1'
 UNION ALL SELECT 'conflicting-duplicate',* FROM observations UNION ALL SELECT 'conflicting-duplicate','new-id',user_id,ds,product,60.00,classification_value FROM observations WHERE observation_id='o2'
)
SELECT test,user_id,ds::VARCHAR AS ds,product,count(*) AS tuple_records FROM variants GROUP BY test,user_id,ds,product HAVING count(*)>1 ORDER BY test;
SELECT count(*) AS null_classifier_selected FROM (SELECT NULL::INTEGER AS classification_value) WHERE classification_value>=10;
SELECT count(DISTINCT user_id) AS raw_identifier_values FROM (VALUES ('A'),('a'),('A ')) AS t(user_id);
