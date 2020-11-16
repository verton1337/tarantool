os = require('os')
env = require('test_run')
math = require('math')
fiber = require('fiber')
test_run = env.new()
netbox = require('net.box')

orig_synchro_quorum = box.cfg.replication_synchro_quorum
orig_synchro_timeout = box.cfg.replication_synchro_timeout

NUM_INSTANCES = 5
SERVERS = {}
for i=1,NUM_INSTANCES do                                                       \
    SERVERS[i] = 'qsync' .. i                                                  \
end;
SERVERS -- print instance names

math.randomseed(os.time())
random = function(excluded_num, total)                                         \
    local r = math.random(1, total)                                            \
    if (r == excluded_num) then                                                \
        return random(excluded_num, total)                                     \
    end                                                                        \
    return r                                                                   \
end

-- Set 'broken' quorum on current leader.
-- Write value on current leader.
-- Pick a random replica in a cluster.
-- Set 'good' quorum on it and promote to a leader.
-- Make sure value is there and on an old leader.

-- Testcase setup.
test_run:create_cluster(SERVERS)
test_run:wait_fullmesh(SERVERS)
test_run:switch('qsync1')
_ = box.schema.space.create('sync', {is_sync=true, engine = test_run:get_cfg('engine')})
_ = box.space.sync:create_index('primary')
box.schema.user.grant('guest', 'write', 'space', 'sync')
test_run:switch('default')
current_leader_id = 1
test_run:eval(SERVERS[current_leader_id], "box.ctl.clear_synchro_queue()")

SOCKET_DIR = require('fio').cwd()

-- Testcase body.
for i=1,30 do                                                                  \
    test_run:eval(SERVERS[current_leader_id],                                  \
        "box.cfg{replication_synchro_quorum=6, replication_synchro_timeout=1000}") \
    c = netbox.connect(SOCKET_DIR..'/'..SERVERS[current_leader_id]..'.sock')   \
    fiber.create(function() c.space.sync:insert{i} end)                        \
    new_leader_id = random(current_leader_id, #SERVERS)                        \
    test_run:eval(SERVERS[new_leader_id],                                      \
        "box.cfg{replication_synchro_quorum=3, replication_synchro_timeout=0.01}") \
    test_run:eval(SERVERS[new_leader_id], "box.ctl.clear_synchro_queue()")     \
    c:close()                                                                  \
    replica = random(new_leader_id, #SERVERS)                                  \
    test_run:wait_cond(function() return test_run:eval(SERVERS[replica],       \
                       string.format("box.space.sync:get{%d}", i))[1] ~= nil end)  \
    test_run:wait_cond(function() return test_run:eval(SERVERS[current_leader_id], \
                       string.format("box.space.sync:get{%d}", i))[1] ~= nil end)  \
    new_leader_id = random(current_leader_id, #SERVERS)                        \
    current_leader_id = new_leader_id                                          \
end

test_run:wait_cond(function() return test_run:eval('qsync1',                   \
                   ("box.space.sync:count()")) == 30 end)

-- Teardown.
test_run:switch('default')
test_run:eval(SERVERS[current_leader_id], 'box.space.sync:drop()')
test_run:drop_cluster(SERVERS)
box.cfg{                                                                       \
    replication_synchro_quorum = orig_synchro_quorum,                          \
    replication_synchro_timeout = orig_synchro_timeout,                        \
}
