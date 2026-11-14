CREATE TABLE dws_gaap_di(ds DATE, product VARCHAR, region VARCHAR, user_type VARCHAR, user_id VARCHAR, gaap_val DECIMAL(18,2), ytd_gaap DECIMAL(18,2));
INSERT INTO dws_gaap_di VALUES
(DATE '2026-03-01','X','A','control','positive',10,2),
(DATE '2026-03-01','X','A','control','negative',-6,3),
(DATE '2026-03-01','X','B','control','fraction',1,3),
(DATE '2026-03-01','X','B','control','negative_denominator',2,-2),
(DATE '2026-03-01','X',NULL,'control','zero_denominator',2,0),
(DATE '2026-03-01','X',NULL,'control','zero_zero',0,0),
(DATE '2026-03-01','X','C','control','null_numerator',NULL,5),
(DATE '2026-03-01','X','C','control','null_denominator',5,NULL);
