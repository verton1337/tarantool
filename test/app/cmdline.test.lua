arg[-1] ~= nil
arg[0] ~= nil
string.match(arg[-1], '^/') ~= nil
string.match(arg[0], '^/') == nil

string.match(arg[-1], '/tarantool$') ~= nil
string.match(arg[0], 'app%.lua$') ~= nil
string.match(arg[3], '--signal=9$') ~= nil

io.type( io.open(arg[-1]) )
io.type( io.open(arg[0]) )

