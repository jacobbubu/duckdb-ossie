-- Synthetic exact decimal/integer controls; literals never pass through JS Number.
CREATE TABLE dws_gaap_di(ds DATE, product VARCHAR, region VARCHAR, user_type VARCHAR,
                        user_id VARCHAR, gaap_val DECIMAL(38,2), ytd_gaap DECIMAL(38,2),
                        ordinal BIGINT, payload VARCHAR);
INSERT INTO dws_gaap_di VALUES
('2026-03-10','X','N','A','n01',9007199254740992.01,10,9007199254740993,'quote " / slash \ / 中文'),
('2026-03-10','X','N','A','n02',9007199254740992.02,10,9007199254740992,''),
('2026-03-10','X','S','B','n03',123456789012345678901234567890123456.78,20,9223372036854775807,'large'),
('2026-03-11','X','S','B','n04',-9007199254740992.01,20,-9007199254740993,'negative'),
('2026-03-11','X','N','A','n05',-0.01,10,-9223372036854775807,'small negative'),
('2026-03-11','X','N','A','n06',NULL,10,NULL,NULL);
