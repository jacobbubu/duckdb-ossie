CREATE TABLE r6_fact(id VARCHAR, dim_id INTEGER, amount DECIMAL(18,2), active BOOLEAN);
CREATE TABLE r6_dimension(id INTEGER, label VARCHAR, eligible BOOLEAN);
INSERT INTO r6_fact VALUES ('f1',1,10.01,true),('f2',1,20.02,true),('f3',2,30.03,false),('f4',2,NULL,true),('f5',3,-5.05,true),('f6',3,0,true);
INSERT INTO r6_dimension VALUES (1,'North',true),(2,'South',false),(3,'North',true),(4,'Unused',true);
