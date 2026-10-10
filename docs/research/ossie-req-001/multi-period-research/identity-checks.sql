-- Extra identity witnesses; no extension or production profile is loaded.
SELECT count(DISTINCT id) AS binary_identity_count FROM (VALUES ('A'),('a'),('A '),('é'),('é')) AS t(id);
WITH t(user_id,ds,product) AS (VALUES ('a|2026-02-01',DATE '2026-02-01','p'),('a',DATE '2026-02-01','2026-02-01|p'))
SELECT count(DISTINCT (user_id,ds,product)) AS typed_tuple_count,count(DISTINCT user_id || '|' || ds::VARCHAR || '|' || product) AS unsafe_concatenation_count FROM t;
SELECT isfinite(DATE '2026-02-01') AS finite_day,isfinite(DATE 'infinity') AS infinite_day;
