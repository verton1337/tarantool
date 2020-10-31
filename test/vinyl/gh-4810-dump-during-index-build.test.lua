test_run = require('test_run').new()

test_run:cmd("create server test with script='vinyl/low_quota.lua'")
test_run:cmd("start server test with args='13421772'")
test_run:cmd('switch test')

fiber = require 'fiber'

math.randomseed(os.time())

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {parts = {1, 'unsigned'}})

--
-- Purpose of this test is to trigger dump during secondary index build.
-- It is worth noting that dump must be triggered by exceeding memory
-- quota and not by invoking box.snapshot() since checkpoint process
-- is locked by DDL latch.
--

MAX_KEY = 1000000
MAX_VAL = 1000000
PADDING = string.rep('x', 100)

test_run:cmd("setopt delimiter ';'")

function gen_insert()
    pcall(s.insert, s, {math.random(MAX_KEY), math.random(MAX_VAL),
                        math.random(MAX_VAL), math.random(MAX_VAL), PADDING})
end;

function fill_L0_without_dump()
    local dump_watermark = box.cfg.vinyl_memory / 2
    while box.stat.vinyl().memory.level0 < dump_watermark do
        gen_insert()
    end
end;

fill_L0_without_dump();
assert(box.stat.vinyl().scheduler.dump_count == 0);

function insert_loop()
    while not stop do
        gen_insert()
    end
    ch:put(true)
end;

function idx_build()
    _ = s:create_index('i1', {unique = true, parts = {2, 'unsigned', 3, 'unsigned'}})
    ch:put(true)
end;

stop = false;
ch = fiber.channel(2);

_ = fiber.create(idx_build);
_ = fiber.create(insert_loop);

fiber.sleep(3);

stop = true;
test_run:wait_cond(function() return ch:size() ~= nil end)
for i = 1, ch:size() do
    if not test_run:wait_cond(function() return ch:get() ~= nil end) then
        log.error("error: hanged on:get()")
    end
end;

test_run:cmd("setopt delimiter ''");
assert(box.stat.vinyl().scheduler.dump_count > 0)
assert(s.index.i1 ~= nil)
s:drop()

test_run:cmd('switch default')
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
