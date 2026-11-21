-- negative: SQL in its own file is the goal, never a finding
CREATE TABLE t (a INTEGER);
SELECT a FROM t WHERE a = 1;
