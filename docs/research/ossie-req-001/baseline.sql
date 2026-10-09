SELECT ds, COUNT(*) AS record_count,
       COUNT(DISTINCT user_id) AS distinct_user_count,
       SUM(gaap_val) AS gaap_sum, AVG(gaap_val) AS gaap_average,
       ROUND(AVG(gaap_val), 2) AS gaap_average_rounded
FROM dws_gaap_di
WHERE product = 'X' AND ytd_gaap >= ?
  AND ds >= DATE '2026-03-01' AND ds < DATE '2026-04-01'
GROUP BY ds ORDER BY ds;
