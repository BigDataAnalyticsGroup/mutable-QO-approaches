----psql -d mutable -f test/ours/data/schema_postgres.sql --variable=path=/Users/luca/mutable-org/mutable

CREATE TABLE R (
    key SMALLINT NOT NULL,
    fkey SMALLINT NOT NULL,
    rfloat REAL NOT NULL,
    rstring CHAR(15) NOT NULL
);

CREATE TABLE S (
    key SMALLINT NOT NULL,
    fkey SMALLINT NOT NULL,
    rfloat REAL NOT NULL,
    rstring CHAR(15) NOT NULL
);

CREATE TABLE T (
    key SMALLINT NOT NULL,
    fkey SMALLINT NOT NULL,
    rfloat REAL NOT NULL,
    rstring CHAR(15) NOT NULL
);

CREATE TABLE D (
    key SMALLINT NOT NULL,
    rdate DATE NOT NULL,
    rdatetime TIMESTAMP NOT NULL
);

\set pathr :path/test/ours/data/R.csv
\set paths :path/test/ours/data/S.csv
\set patht :path/test/ours/data/T.csv
--\set pathd :path/test/ours/data/D.csv
COPY R(key, fkey, rfloat, rstring) FROM :'pathr' DELIMITER ',' CSV HEADER;
COPY S(key, fkey, rfloat, rstring) FROM :'paths' DELIMITER ',' CSV HEADER;
COPY T(key, fkey, rfloat, rstring) FROM :'patht' DELIMITER ',' CSV HEADER;
--COPY D(key, rdate, rdatetime) FROM :'pathd' DELIMITER ',' CSV HEADER;
