-- Research witness only. No extension or production bundle is loaded.
CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER);
COPY observations FROM 'docs/research/ossie-req-001/multi-period-research/observations.csv' (HEADER);
CREATE VIEW qualifying AS SELECT * FROM observations WHERE classification_value >= 10;
SELECT 'all_products' AS scope,count(*) AS observation_count,count(DISTINCT user_id) AS user_count,count(amount_usd) AS amount_count,sum(amount_usd)::VARCHAR AS total,avg(amount_usd) AS mean FROM qualifying
UNION ALL SELECT 'X',count(*),count(DISTINCT user_id),count(amount_usd),sum(amount_usd)::VARCHAR,avg(amount_usd) FROM qualifying WHERE product='X';
SELECT ds::VARCHAR AS ds,count(*) AS observations,count(DISTINCT user_id) AS users,count(amount_usd) AS amount_count,sum(amount_usd)::VARCHAR AS total,avg(amount_usd) AS mean FROM qualifying GROUP BY ds ORDER BY ds;
SELECT avg(user_mean) AS mean_of_user_means,avg(user_total) AS mean_of_user_totals FROM (SELECT user_id,avg(amount_usd) AS user_mean,sum(amount_usd) AS user_total FROM qualifying GROUP BY user_id);
