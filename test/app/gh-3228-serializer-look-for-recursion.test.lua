test_run = require('test_run').new()

--
-- gh-3228: Check the error message in the case of a __serialize
-- function generating infinite recursion.
--
setmetatable({}, {__serialize = function(a) return a end})
setmetatable({}, {__serialize = function(a, b, c) return a, b, c end})
