fiber = require('fiber')
test_run = require('test_run').new()

err = nil

test_run:cmd("setopt delimiter ';'")
function test()
    _, err = pcall(function() fiber.cond():wait() end)
end;
test_run:cmd("setopt delimiter ''");

f = fiber.new(test)
fiber.yield()
f:cancel()
fiber.yield()
f
err
