CREATE TABLE dws_gaap_di(ds DATE, product VARCHAR, region VARCHAR, user_type VARCHAR, user_id VARCHAR, gaap_val DECIMAL(18,2), ytd_gaap DECIMAL(18,2));
INSERT INTO dws_gaap_di VALUES
(DATE '2023-12-31','X','A','control','year_end',1,20),
(DATE '2024-01-01','X','A','control','year_start',2,20),
(DATE '2024-02-01','X','A','control','feb_start',3,20),
(DATE '2024-02-28','X','A','control','feb_low',4,0),
(DATE '2024-02-29','X','B','control','leap_day',5,20),
(DATE '2024-03-01','X','B','control','march_start',6,20),
(DATE '2024-04-01','X','B','control','april_start',7,20),
(DATE '2025-02-28','X','B','control','nonleap_feb',8,20),
(NULL,'X','B','control','null_date',9,20),
(DATE 'infinity','X','B','control','positive_infinity',10,20),
(DATE '-infinity','X','B','control','negative_infinity',11,20);
