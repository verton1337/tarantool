test_run = require('test_run').new()


test_run:cmd("create server master with script='engine/low_memory.lua'")
test_run:cmd('start server master')
test_run:cmd("switch master")


test_run:cmd("setopt delimiter ';'")
function create_space(name)
    local space = box.schema.create_space(name)
    space:format({
        { name = "id",  type = "unsigned" },
        { name = "val", type = "str" }
    })
    space:create_index('primary', { parts = { 'id' } })
    return space
end;

function insert(space, i)
    space:insert({ i, string.rep(string.char(32 + math.random(127-32)), math.random(1024)) })
end;

function fill_space(space, start)
    local _, err = nil
    local i = start
    while err == nil do _, err = pcall(insert, space, i) i = i + 1 end
end;

function stress_deletion(i, spaces)
    local res, space = pcall(create_space, 'test' .. tostring(i))
    if res then spaces[i] = space return end
    fill_space(box.space.test, box.space.test:len())
    for _, s in pairs(spaces) do fill_space(s, s:len()) end
    box.space.test:delete(box.space.test:len() - 1)
end;
test_run:cmd("setopt delimiter ''");


_ = create_space('test')
for i = 0, 27000 do insert(box.space.test, i) end

spaces = {}
counter = 0
status = true
res = nil
errinj = box.error.injection
errinj.set('ERRINJ_RESERVE_EXTENTS_BEFORE_DELETE', true)
while counter < 1400 do status, res = pcall(stress_deletion, counter, spaces) counter = counter + 1 end
status
res

-- Cleanup.
test_run:cmd('switch default')
test_run:drop_cluster({'master'})
