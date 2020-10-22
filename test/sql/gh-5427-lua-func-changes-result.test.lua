test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

box.execute([[CREATE TABLE t (b STRING PRIMARY KEY);]])
box.execute([[INSERT INTO t VALUES ('aaaaaaaaaaaa');]])
test_run:cmd("setopt delimiter ';'");
box.schema.func.create('CORRUPT_SELECT', {
    language = 'LUA',
    returns = 'integer',
    body = [[
        function(x)
            box.space.T:select()
            return 1
        end]],
    is_sandboxed = false,
    param_list = { "string" },
    exports = { 'LUA', 'SQL' },
    is_deterministic = false,
    if_not_exists = true
});
box.schema.func.create('CORRUPT_GET', {
    language = 'LUA',
    returns = 'integer',
    body = [[
        function(x)
            box.space.T:get('aaaaaaaaaaaa')
            return 1
        end]],
    is_sandboxed = false,
    param_list = { "string" },
    exports = { 'LUA', 'SQL' },
    is_deterministic = false,
    if_not_exists = true
});
box.schema.func.create('CORRUPT_COUNT', {
    language = 'LUA',
    returns = 'integer',
    body = [[
        function(x)
            box.space.T:count()
            return 1
        end]],
    is_sandboxed = false,
    param_list = { "string" },
    exports = { 'LUA', 'SQL' },
    is_deterministic = false,
    if_not_exists = true
});
box.schema.func.create('CORRUPT_MAX', {
    language = 'LUA',
    returns = 'integer',
    body = [[
        function(x)
            box.space.T.index[0]:max()
            return 1
        end]],
    is_sandboxed = false,
    param_list = { "string" },
    exports = { 'LUA', 'SQL' },
    is_deterministic = false,
    if_not_exists = true
});
box.schema.func.create('CORRUPT_MIN', {
    language = 'LUA',
    returns = 'integer',
    body = [[
        function(x)
            box.space.T.index[0]:min()
            return 1
        end]],
    is_sandboxed = false,
    param_list = { "string" },
    exports = { 'LUA', 'SQL' },
    is_deterministic = false,
    if_not_exists = true
});
test_run:cmd("setopt delimiter ''");

box.execute([[select CORRUPT_SELECT(t.b) from t where t.b = ? and t.b <= ? order by t.b;]], {"aaaaaaaaaaaa", "aaaaaaaaaaaa"})
box.execute([[select CORRUPT_GET(t.b) from t where t.b = ? and t.b <= ? order by t.b;]], {"aaaaaaaaaaaa", "aaaaaaaaaaaa"})
box.execute([[select CORRUPT_COUNT(t.b) from t where t.b = ? and t.b <= ? order by t.b;]], {"aaaaaaaaaaaa", "aaaaaaaaaaaa"})
box.execute([[select CORRUPT_MAX(t.b) from t where t.b = ? and t.b <= ? order by t.b;]], {"aaaaaaaaaaaa", "aaaaaaaaaaaa"})
box.execute([[select CORRUPT_MIN(t.b) from t where t.b = ? and t.b <= ? order by t.b;]], {"aaaaaaaaaaaa", "aaaaaaaaaaaa"})

box.execute([[DROP TABLE t;]])
