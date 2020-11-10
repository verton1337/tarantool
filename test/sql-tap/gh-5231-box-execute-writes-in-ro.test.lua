#!/usr/bin/env tarantool

test = require("sqltester")
test:plan(9)

local expected_err = "Can't modify data because this instance is in read-only mode."

test:execsql([[
    CREATE TABLE TEST (A INT, B INT, PRIMARY KEY (A));
    INSERT INTO TEST (A, B) VALUES (3, 3);
]])

box.cfg{read_only = true}

test:do_catchsql_test(
    "sql-errors-1.1",
    [[
        INSERT INTO TEST (A, B) VALUES (1, 1);
    ]], {
        -- <sql-errors-1.1>
        1, expected_err
        -- </sql-errors-1.1>
    })

test:do_catchsql_test(
    "sql-errors-1.2",
    [[
        DELETE FROM TEST;
    ]], {
        -- <sql-errors-1.2>
        1, expected_err
        -- </sql-errors-1.2>
    })

test:do_catchsql_test(
    "sql-errors-1.3",
    [[
        REPLACE INTO TEST VALUES (1, 2);
    ]], {
        -- <sql-errors-1.3>
        1, expected_err
        -- </sql-errors-1.3>
    })

test:do_catchsql_test(
    "sql-errors-1.4",
    [[
        UPDATE TEST SET B=4 WHERE A=3;
    ]], {
        -- <sql-errors-1.4>
        1, expected_err
        -- </sql-errors-1.4>
    })

test:do_catchsql_test(
    "sql-errors-1.5",
    [[
        TRUNCATE TABLE TEST;
    ]], {
        -- <sql-errors-1.5>
        1, expected_err
        -- </sql-errors-1.5>
    })

test:do_catchsql_test(
    "sql-errors-1.6",
    [[
        CREATE TABLE TEST2 (A INT, PRIMARY KEY (A));
    ]], {
        -- <sql-errors-1.6>
        1, expected_err
        -- </sql-errors-1.6>
    })

test:do_catchsql_test(
    "sql-errors-1.7",
    [[
        ALTER TABLE TEST ADD CONSTRAINT UK_CON UNIQUE (B);
    ]], {
        -- <sql-errors-1.7>
        1, expected_err
        -- </sql-errors-1.7>
    })

test:do_catchsql_test(
    "sql-errors-1.8",
    [[
        ALTER TABLE TEST RENAME TO TEST2;
    ]], {
        -- <sql-errors-1.8>
        1, expected_err
        -- </sql-errors-1.8>
    })

test:do_catchsql_test(
    "sql-errors-1.9",
    [[
        DROP TABLE TEST;
    ]], {
        -- <sql-errors-1.9>
        1, expected_err
        -- </sql-errors-1.9>
    })

test:finish_test()
