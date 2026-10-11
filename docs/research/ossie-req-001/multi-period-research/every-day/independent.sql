-- Research witness in ordinary DuckDB SQL only. No LOAD, Ossie bundle or MCP.
CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER);
COPY observations FROM 'docs/research/ossie-req-001/multi-period-research/every-day/observations.csv' (HEADER);
CREATE TABLE original(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER);
COPY original FROM 'docs/research/ossie-req-001/multi-period-research/observations.csv' (HEADER);
CREATE VIEW cases AS SELECT * FROM read_json_auto('docs/research/ossie-req-001/multi-period-research/every-day/cases.json');
CREATE VIEW scoped AS
SELECT c.id,o.* FROM cases c JOIN observations o ON c.dataset='extended'
UNION ALL SELECT c.id,o.* FROM cases c JOIN original o ON c.dataset='original';
-- Required dates come from the explicit interval, including globally missing days.
CREATE VIEW required_days AS SELECT c.id,unnest(generate_series(c.start::DATE,c.end::DATE,INTERVAL 1 DAY))::DATE AS ds FROM cases c;
CREATE VIEW candidates AS SELECT DISTINCT id,user_id FROM scoped;
CREATE VIEW covered AS SELECT DISTINCT s.id,s.user_id,s.ds FROM scoped s JOIN cases c USING(id)
WHERE s.ds BETWEEN c.start::DATE AND c.end::DATE
  AND s.classification_value >= c.threshold
  AND (c.products IS NULL OR list_contains(c.products,s.product));
-- Relational division: no required day lacks a qualifying witness for this user.
CREATE VIEW eligible AS SELECT u.* FROM candidates u WHERE NOT EXISTS (
  SELECT 1 FROM required_days d WHERE d.id=u.id AND NOT EXISTS (
    SELECT 1 FROM covered q WHERE q.id=u.id AND q.user_id=u.user_id AND q.ds=d.ds
  )
);
SELECT c.id,coalesce(list(e.user_id ORDER BY e.user_id) FILTER (WHERE e.user_id IS NOT NULL),[]::VARCHAR[]) AS eligible_ids,
  count(e.user_id) AS user_count,(SELECT count(*) FROM required_days d WHERE d.id=c.id) AS required_days
FROM cases c LEFT JOIN eligible e ON e.id=c.id GROUP BY c.id ORDER BY c.id;
