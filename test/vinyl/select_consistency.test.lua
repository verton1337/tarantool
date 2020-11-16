test_run = require('test_run').new()

fiber = require 'fiber'
log = require 'log'

math.randomseed(os.time())

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {parts = {1, 'unsigned'}, page_size = 64, range_size = 256})
_ = s:create_index('i1', {unique = true, parts = {2, 'unsigned', 3, 'unsigned'}, page_size = 64, range_size = 256})
_ = s:create_index('i2', {unique = true, parts = {2, 'unsigned', 4, 'unsigned'}, page_size = 64, range_size = 256})

--
-- If called from a transaction, i1:select({k}) and i2:select({k})
-- must yield the same result. Let's check that under a stress load.
--

MAX_KEY = 100
MAX_VAL = 10
PADDING = string.rep('x', 100)

test_run:cmd("setopt delimiter ';'")

function gen_insert()
    pcall(s.insert, s, {math.random(MAX_KEY), math.random(MAX_VAL),
                        math.random(MAX_VAL), math.random(MAX_VAL), PADDING})
end;

function gen_delete()
    pcall(s.delete, s, math.random(MAX_KEY))
end;

function gen_update()
    pcall(s.update, s, math.random(MAX_KEY), {{'+', 5, 1}})
end;

failed = false

function dml_loop()
    local insert = true
    while not stop do
        if s:len() >= MAX_KEY then
            insert = false
        end
        if s:len() <= MAX_KEY / 2 then
            insert = true
        end
        if insert then
            gen_insert()
        else
            gen_delete()
        end
        gen_update()
        fiber.sleep(0)
    end
    ch:put(true)
end;

function snap_loop()
    while not stop do
        local ok, err = fiber.create(function() local ok, err = pcall(box.snapshot) return ok, err end)
        if ok == false then
            log.info("error: box.snapshot failed with error " .. err)
            failed = true
            break
        end
        fiber.sleep(0.1)
    end
    ch:put(true)
end;

stop = false;
ch = fiber.channel(3);

_ = fiber.create(dml_loop);
_ = fiber.create(dml_loop);
_ = fiber.create(snap_loop);

function run_iter()
    local val = math.random(MAX_VAL)
    box.begin()
    local res1 = s.index.i1:select({val})
    local res2 = s.index.i2:select({val})
    box.commit()
    local equal = true
    if #res1 == #res2 then
        for _, t1 in ipairs(res1) do
            local found = false
            for _, t2 in ipairs(res2) do
                if t1[1] == t2[1] then
                    found = true
                    break
                end
            end
            if not found then
                log.error("error: equal not found for #res1 = " .. #res1 .. ", #res2 = " .. #res2)
                equal = false
                break
            end
        end
    end
    fiber.sleep(0)
    return equal
end;

for i = 1, 10000 do
    if failed or not run_iter(i) then
        log.error("error: failed on iteration " .. i)
        failed = true
        break
    end
end;

stop = true;

function check_get()
    for i = 1, ch:size() do
        if not test_run:wait_cond(function() return ch:get() ~= nil end) then
            log.error("error: hanged on ch:get() on iteration " .. i)
            return false
	end
    end
    return true
end;

test_run:cmd("setopt delimiter ''");

test_run:wait_cond(function() return check_get() end)
failed

s:drop()
