#!/usr/bin/env tarantool

local tap = require('tap')
local errno = require('errno')

local test = tap.test("errno")

test:plan(1)
test:test("primary", function(testcase)
    testcase:plan(10)
    testcase:is(type(errno), "table", "type of table")
    testcase:ok(errno.EINVAL ~= nil, "errno.EINVAL is available")
    testcase:ok(errno.EBADF ~= nil , "errno.EBADF is available" )
    testcase:ok(errno(0) ~= nil, "errno set to 0")
    testcase:is(errno(errno.EBADF), 0, "setting errno.EBADF")
    testcase:is(errno(), errno.EBADF, "checking errno.EBADF")
    testcase:is(errno(errno.EINVAL), errno.EBADF, "setting errno.EINVAL")
    testcase:is(errno(), errno.EINVAL, "checking errno.EINVAL")
    testcase:is(errno.strerror(), "Invalid argument", "checking strerror without argument")
    testcase:is(errno.strerror(errno.EBADF), "Bad file descriptor", "checking strerror with argument")
end)
