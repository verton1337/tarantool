#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test("string extensions")

test:plan(7)

test:test("split", function(testcase)
    testcase:plan(10)

    -- testing basic split (works over gsplit)
    testcase:ok(not pcall(string.split, "", ""), "empty separator")
    testcase:ok(not pcall(string.split, "a", ""), "empty separator")
    testcase:is_deeply((""):split("z"), {""},  "empty split")
    testcase:is_deeply(("a"):split("a"), {"", ""}, "split self")
    testcase:is_deeply(
        (" 1 2  3  "):split(),
        {"1", "2", "3"},
        "complex split on empty separator"
    )
    testcase:is_deeply(
        (" 1 2  3  "):split(" "),
        {"", "1", "2", "", "3", "", ""},
        "complex split on space separator"
    )
    testcase:is_deeply(
        (" 1 2  \n\n\n\r\t\n3  "):split(),
        {"1", "2", "3"},
        "complex split on empty separator"
    )
    testcase:is_deeply(
        ("a*bb*c*ddd"):split("*"),
        {"a", "bb", "c", "ddd"},
        "another * separator"
    )
    testcase:is_deeply(
        ("dog:fred:bonzo:alice"):split(":", 2),
        {"dog", "fred", "bonzo:alice"},
        "testing max separator"
    )
    testcase:is_deeply(
        ("///"):split("/"),
        {"", "", "", ""},
        "testing splitting on one char"
    )
end)

-- gh-2214 - string.ljust()/string.rjust() Lua API
test:test("ljust/rjust/center", function(testcase)
    testcase:plan(18)

    testcase:is(("help"):ljust(0),  "help", "ljust, length 0, do nothing")
    testcase:is(("help"):rjust(0),  "help", "rjust, length 0, do nothing")
    testcase:is(("help"):center(0), "help", "center, length 0, do nothing")

    testcase:is(("help"):ljust(3),  "help", "ljust, length 3, do nothing")
    testcase:is(("help"):rjust(3),  "help", "rjust, length 3, do nothing")
    testcase:is(("help"):center(3), "help", "center, length 3, do nothing")

    testcase:is(("help"):ljust(5),  "help ", "ljust, length 5, one extra charachter")
    testcase:is(("help"):rjust(5),  " help", "rjust, length 5, one extra charachter")
    testcase:is(("help"):center(5), "help ", "center, length 5, one extra charachter")

    testcase:is(("help"):ljust(6),  "help  ", "ljust, length 6, two extra charachters")
    testcase:is(("help"):rjust(6),  "  help", "rjust, length 6, two extra charachters")
    testcase:is(("help"):center(6), " help ", "center, length 6, two extra charachters")

    testcase:is(("help"):ljust(6, '.'),  "help..", "ljust, length 6, two extra charachters, custom fill char")
    testcase:is(("help"):rjust(6, '.'),  "..help", "rjust, length 6, two extra charachters, custom fill char")
    testcase:is(("help"):center(6, '.'), ".help.", "center, length 6, two extra charachters, custom fill char")
    local errmsg = "%(char expected, got string%)"
    local _, err = pcall(function() ("help"):ljust(6, "XX") end)
    testcase:ok(err and err:match(errmsg), "wrong params")
    _, err = pcall(function() ("help"):rjust(6, "XX") end)
    testcase:ok(err and err:match(errmsg), "wrong params")
    _, err = pcall(function() ("help"):center(6, "XX") end)
    testcase:ok(err and err:match(errmsg), "wrong params")
end)

-- gh-2215 - string.startswith()/string.endswith() Lua API
test:test("startswith/endswith", function(testcase)
    testcase:plan(21)

    testcase:ok((""):startswith(""),      "empty+empty startswith")
    testcase:ok((""):endswith(""),        "empty+empty endswith")
    testcase:ok(not (""):startswith("a"), "empty+non-empty startswith")
    testcase:ok(not (""):endswith("a"),   "empty+non-empty endswith")
    testcase:ok(("a"):startswith(""),     "non-empty+empty startswith")
    testcase:ok(("a"):endswith(""),       "non-empty+empty endswith")

    testcase:ok(("12345"):startswith("123")            , "simple startswith")
    testcase:ok(("12345"):startswith("123", 1, 5)      , "startswith with good begin/end")
    testcase:ok(("12345"):startswith("123", 1, 3)      , "startswith with good begin/end")
    testcase:ok(("12345"):startswith("123", -5, 3)     , "startswith with good negative begin/end")
    testcase:ok(("12345"):startswith("123", -5, -3)    , "startswith with good negative begin/end")
    testcase:ok(not ("12345"):startswith("123", 2, 5)  , "bad startswith with good begin/end")
    testcase:ok(not ("12345"):startswith("123", 1, 2)  , "bad startswith with good begin/end")

    testcase:ok(("12345"):endswith("345")              , "simple endswith")
    testcase:ok(("12345"):endswith("345", 1, 5)        , "endswith with good begin/end")
    testcase:ok(("12345"):endswith("345", 3, 5)        , "endswith with good begin/end")
    testcase:ok(("12345"):endswith("345", -3, 5)       , "endswith with good begin/end")
    testcase:ok(("12345"):endswith("345", -3, -1)      , "endswith with good begin/end")
    testcase:ok(not ("12345"):endswith("345", 1, 4)    , "bad endswith with good begin/end")
    testcase:ok(not ("12345"):endswith("345", 4, 5)    , "bad endswith with good begin/end")

    local _, err = pcall(function() ("help"):startswith({'n', 1}) end)
    testcase:ok(err and err:match("%(string expected, got table%)"), "wrong params")
end)

test:test("hex", function(testcase)
    testcase:plan(2)
    testcase:is(string.hex("hello"), "68656c6c6f", "hex non-empty string")
    testcase:is(string.hex(""), "", "hex empty string")
end)

test:test("fromhex", function(testcase)
    testcase:plan(11)
    testcase:is(string.fromhex("48656c6c6f"), "Hello", "from hex to bin")
    testcase:is(string.fromhex("4c696e7578"), "Linux", "from hex to bin")
    testcase:is(string.fromhex("6C6F72656D"), "lorem", "from hex to bin")
    testcase:is(string.fromhex("697073756D"), "ipsum", "from hex to bin")
    testcase:is(string.fromhex("6c6f72656d"), "lorem", "from hex to bin")
    testcase:is(string.fromhex("697073756d"), "ipsum", "from hex to bin")
    testcase:is(string.fromhex("6A6B6C6D6E6F"), "jklmno", "from hex to bin")
    testcase:is(string.fromhex("6a6b6c6d6e6f"), "jklmno", "from hex to bin")
    local _, err = pcall(string.fromhex, "aaa")
    testcase:ok(err and err:match("(even amount of chars expected," ..
                              " got odd amount)"))
    _, err = pcall(string.fromhex, "qq")
    testcase:ok(err and err:match("(hex string expected, got non hex chars)"))
    _, err = pcall(string.fromhex, 795)
    testcase:ok(err and err:match("(string expected, got number)"))
end)

test:test("strip", function(testcase)
    testcase:plan(45)
    local str = "  Hello world! "
    testcase:is(string.strip(str), "Hello world!", "strip (without chars)")
    testcase:is(string.lstrip(str), "Hello world! ", "lstrip (without chars)")
    testcase:is(string.rstrip(str), "  Hello world!", "rstrip (without chars)")
    str = ""
    testcase:is(string.strip(str), str, "strip (0-len inp without chars)")
    testcase:is(string.lstrip(str), str, "lstrip (0-len inp without chars)")
    testcase:is(string.rstrip(str), str, "rstrip (0-len inp without chars)")
    str = " "
    testcase:is(string.strip(str), "", "strip (1-len inp without chars)")
    testcase:is(string.lstrip(str), "", "lstrip (1-len inp without chars)")
    testcase:is(string.rstrip(str), "", "rstrip (1-len inp without chars)")
    str = "\t\v"
    testcase:is(string.strip(str), "", "strip (strip everything without chars)")
    testcase:is(string.lstrip(str), "", "lstrip (strip everything without chars)")
    testcase:is(string.rstrip(str), "", "rstrip (strip everything without chars)")
    str = "hello"
    testcase:is(string.strip(str), str, "strip (strip nothing without chars)")
    testcase:is(string.lstrip(str), str, "lstrip (strip nothing without chars)")
    testcase:is(string.rstrip(str), str, "rstrip (strip nothing without chars)")
    str = " \t\n\v\f\rTEST \t\n\v\f\r"
    testcase:is(string.strip(str), "TEST", "strip (all space characters without chars)")
    testcase:is(string.lstrip(str), "TEST \t\n\v\f\r", "lstrip (all space characters without chars)")
    testcase:is(string.rstrip(str), " \t\n\v\f\rTEST", "rstrip (all space characters without chars)")

    local chars = "#\0"
    str = "##Hello world!#"
    testcase:is(string.strip(str, chars), "Hello world!", "strip (with chars)")
    testcase:is(string.lstrip(str, chars), "Hello world!#", "lstrip (with chars)")
    testcase:is(string.rstrip(str, chars), "##Hello world!", "rstrip (with chars)")
    str = ""
    testcase:is(string.strip(str, chars), str, "strip (0-len inp with chars)")
    testcase:is(string.lstrip(str, chars), str, "lstrip (0-len inp with chars)")
    testcase:is(string.rstrip(str, chars), str, "rstrip (0-len inp with chars)")
    str = "#"
    testcase:is(string.strip(str, chars), "", "strip (1-len inp with chars)")
    testcase:is(string.lstrip(str, chars), "", "lstrip (1-len inp with chars)")
    testcase:is(string.rstrip(str, chars), "", "rstrip (1-len inp with chars)")
    str = "##"
    testcase:is(string.strip(str, chars), "", "strip (strip everything with chars)")
    testcase:is(string.lstrip(str, chars), "", "lstrip (strip everything with chars)")
    testcase:is(string.rstrip(str, chars), "", "rstrip (strip everything with chars)")
    str = "hello"
    testcase:is(string.strip(str, chars), str, "strip (strip nothing with chars)")
    testcase:is(string.lstrip(str, chars), str, "lstrip (strip nothing with chars)")
    testcase:is(string.rstrip(str, chars), str, "rstrip (strip nothing with chars)")
    str = "\0\0\0TEST\0"
    testcase:is(string.strip(str, chars), "TEST", "strip (embedded 0s with chars)")
    testcase:is(string.lstrip(str, chars), "TEST\0", "lstrip (embedded 0s with chars)")
    testcase:is(string.rstrip(str, chars), "\0\0\0TEST", "rstrip (embedded 0s with chars)")
    chars = ""
    testcase:is(string.strip(str, chars), str, "strip (0-len chars)")
    testcase:is(string.lstrip(str, chars), str, "lstrip (0-len chars)")
    testcase:is(string.rstrip(str, chars), str, "rstrip (0-len chars)")

    local _, err = pcall(string.strip, 12)
    testcase:ok(err and err:match("#1 to '.-%.strip' %(string expected, got number%)"), "strip err 1")
    _, err = pcall(string.lstrip, 12)
    testcase:ok(err and err:match("#1 to '.-%.lstrip' %(string expected, got number%)"), "lstrip err 1")
    _, err = pcall(string.rstrip, 12)
    testcase:ok(err and err:match("#1 to '.-%.rstrip' %(string expected, got number%)"), "rstrip err 1")

    _, err = pcall(string.strip, "foo", 12)
    testcase:ok(err and err:match("#2 to '.-%.strip' %(string expected, got number%)"), "strip err 2")
    _, err = pcall(string.lstrip, "foo", 12)
    testcase:ok(err and err:match("#2 to '.-%.lstrip' %(string expected, got number%)"), "lstrip err 2")
    _, err = pcall(string.rstrip, "foo", 12)
    testcase:ok(err and err:match("#2 to '.-%.rstrip' %(string expected, got number%)"), "rstrip err 2")
end)

test:test("unicode", function(testcase)
    testcase:plan(104)
    local str = 'хеЛлоу вОрЛд ё Ё я Я э Э ъ Ъ hElLo WorLd 1234 i I İ 勺#☢༺'
    local upper_res = 'ХЕЛЛОУ ВОРЛД Ё Ё Я Я Э Э Ъ Ъ HELLO WORLD 1234 I I İ 勺#☢༺'
    local lower_res = 'хеллоу ворлд ё ё я я э э ъ ъ hello world 1234 i i i̇ 勺#☢༺'
    local s = utf8.upper(str)
    testcase:is(s, upper_res, 'default locale upper')
    s = utf8.lower(str)
    testcase:is(s, lower_res, 'default locale lower')
    testcase:is(utf8.upper(''), '', 'empty string upper')
    testcase:is(utf8.lower(''), '', 'empty string lower')
    local err, _
    _, err = pcall(utf8.upper, true)
    testcase:isnt(err:find('Usage'), nil, 'upper usage is checked')
    _, err = pcall(utf8.lower, true)
    testcase:isnt(err:find('Usage'), nil, 'lower usage is checked')

    testcase:is(utf8.isupper('a'), false, 'isupper("a")')
    testcase:is(utf8.isupper('A'), true, 'isupper("A")')
    testcase:is(utf8.islower('a'), true, 'islower("a")')
    testcase:is(utf8.islower('A'), false, 'islower("A")')
    testcase:is(utf8.isalpha('a'), true, 'isalpha("a")')
    testcase:is(utf8.isalpha('A'), true, 'isalpha("A")')
    testcase:is(utf8.isalpha('aa'), false, 'isalpha("aa")')
    testcase:is(utf8.isalpha('勺'), true, 'isalpha("勺")')
    testcase:is(utf8.isupper('Ё'), true, 'isupper("Ё")')
    testcase:is(utf8.islower('ё'), true, 'islower("ё")')
    testcase:is(utf8.isdigit('a'), false, 'isdigit("a")')
    testcase:is(utf8.isdigit('1'), true, 'isdigit("1")')
    testcase:is(utf8.isdigit('9'), true, 'isdigit("9")')

    testcase:is(utf8.len(str), 56, 'len works on complex string')
    s = '12İ☢勺34'
    testcase:is(utf8.len(s), 7, 'len works no options')
    testcase:is(utf8.len(s, 1), 7, 'default start is 1')
    testcase:is(utf8.len(s, 2), 6, 'start 2')
    testcase:is(utf8.len(s, 3), 5, 'start 3')
    local c
    c, err = utf8.len(s, 4)
    testcase:isnil(c, 'middle of symbol offset is error')
    testcase:is(err, 4, 'error on 4 byte')
    testcase:is(utf8.len(s, 5), 4, 'start 5')
    _, err = utf8.len(s, 6)
    testcase:is(err, 6, 'error on 6 byte')
    _, err = utf8.len(s, 0)
    testcase:is(err, 'position is out of string', 'range is out of string')
    testcase:is(utf8.len(s, #s), 1, 'start from the end')
    testcase:is(utf8.len(s, #s + 1), 0, 'position is out of string')
    testcase:is(utf8.len(s, 1, -1), 7, 'default end is -1')
    testcase:is(utf8.len(s, 1, -2), 6, 'end -2')
    testcase:is(utf8.len(s, 1, -3), 5, 'end -3')
    testcase:is(utf8.len(s, 1, -4), 5, 'end in the middle of symbol')
    testcase:is(utf8.len(s, 1, -5), 5, 'end in the middle of symbol')
    testcase:is(utf8.len(s, 1, -6), 5, 'end in the middle of symbol')
    testcase:is(utf8.len(s, 1, -7), 4, 'end -7')
    testcase:is(utf8.len(s, 2, -7), 3, '[2, -7]')
    testcase:is(utf8.len(s, 3, -7), 2, '[3, -7]')
    _, err = utf8.len(s, 4, -7)
    testcase:is(err, 4, '[4, -7] is error - start from the middle of symbol')
    testcase:is(utf8.len(s, 10, -100), 0, 'it is ok to be out of str by end pos')
    testcase:is(utf8.len(s, 10, -10), 0, 'it is ok to swap end and start pos')
    testcase:is(utf8.len(''), 0, 'empty len')
    testcase:is(utf8.len(s, -6, -1), 3, 'pass both negative offsets')
    testcase:is(utf8.len(s, 3, 3), 1, "end in the middle on the same symbol as start")
    _, err = utf8.len('a\xF4')
    testcase:is(err, 2, "invalid unicode in the middle of the string")

    local chars = {}
    local codes = {}
    for _, code in utf8.next, s do
        table.insert(chars, utf8.char(code))
        table.insert(codes, code)
    end
    testcase:is(table.concat(chars), s, "next and char works")
    _, err = pcall(utf8.char, 'kek')
    testcase:isnt(err:find('bad argument'), nil, 'char usage is checked')
    _, err = pcall(utf8.next, true)
    testcase:isnt(err:find('Usage'), nil, 'next usage is checked')
    _, err = pcall(utf8.next, '1234', true)
    testcase:isnt(err:find('bad argument'), nil, 'next usage is checked')
    local offset
    offset, c = utf8.next('')
    testcase:isnil(offset, 'next on empty - nil offset')
    testcase:isnil(c, 'next on empty - nil code')
    offset, c = utf8.next('123', 100)
    testcase:isnil(offset, 'out of string - nil offset')
    testcase:isnil(c, 'out of string - nil code')
    testcase:is(utf8.char(unpack(codes)), s, 'char with multiple values')

    local uppers = 0
    local lowers = 0
    local digits = 0
    local letters = 0
    for _, code in utf8.next, str do
        if utf8.isupper(code) then uppers = uppers + 1 end
        if utf8.islower(code) then lowers = lowers + 1 end
        if utf8.isalpha(code) then letters = letters + 1 end
        if utf8.isdigit(code) then digits = digits + 1 end
    end
    testcase:is(uppers, 13, 'uppers by code')
    testcase:is(lowers, 19, 'lowers by code')
    testcase:is(letters, 33, 'letters by code')
    testcase:is(digits, 4, 'digits by code')

    s = '12345678'
    testcase:is(utf8.sub(s, 1, 1), '1', 'sub [1]')
    testcase:is(utf8.sub(s, 1, 2), '12', 'sub [1:2]')
    testcase:is(utf8.sub(s, 2, 2), '2', 'sub [2:2]')
    testcase:is(utf8.sub(s, 0, 2), '12', 'sub [0:2]')
    testcase:is(utf8.sub(s, 3, 7), '34567', 'sub [3:7]')
    testcase:is(utf8.sub(s, 7, 3), '', 'sub [7:3]')
    testcase:is(utf8.sub(s, 3, 100), '345678', 'sub [3:100]')
    testcase:is(utf8.sub(s, 100, 3), '', 'sub [100:3]')

    testcase:is(utf8.sub(s, 5), '5678', 'sub [5:]')
    testcase:is(utf8.sub(s, 1, -1), s, 'sub [1:-1]')
    testcase:is(utf8.sub(s, 1, -2), '1234567', 'sub [1:-2]')
    testcase:is(utf8.sub(s, 2, -2), '234567', 'sub [2:-2]')
    testcase:is(utf8.sub(s, 3, -3), '3456', 'sub [3:-3]')
    testcase:is(utf8.sub(s, 5, -4), '5', 'sub [5:-4]')
    testcase:is(utf8.sub(s, 7, -7), '', 'sub[7:-7]')

    testcase:is(utf8.sub(s, -2, -1), '78', 'sub [-2:-1]')
    testcase:is(utf8.sub(s, -1, -1), '8', 'sub [-1:-1]')
    testcase:is(utf8.sub(s, -4, -2), '567', 'sub [-4:-2]')
    testcase:is(utf8.sub(s, -400, -2), '1234567', 'sub [-400:-2]')
    testcase:is(utf8.sub(s, -3, -5), '', 'sub [-3:-5]')

    testcase:is(utf8.sub(s, -6, 5), '345', 'sub [-6:5]')
    testcase:is(utf8.sub(s, -5, 4), '4', 'sub [-5:4]')
    testcase:is(utf8.sub(s, -2, 2), '', 'sub [-2:2]')
    testcase:is(utf8.sub(s, -1, 8), '8', 'sub [-1:8]')

    _, err = pcall(utf8.sub)
    testcase:isnt(err:find('Usage'), nil, 'usage is checked')
    _, err = pcall(utf8.sub, true)
    testcase:isnt(err:find('Usage'), nil, 'usage is checked')
    _, err = pcall(utf8.sub, '123')
    testcase:isnt(err:find('Usage'), nil, 'usage is checked')
    _, err = pcall(utf8.sub, '123', true)
    testcase:isnt(err:find('bad argument'), nil, 'usage is checked')
    _, err = pcall(utf8.sub, '123', 1, true)
    testcase:isnt(err:find('bad argument'), nil, 'usage is checked')

    local s1 = '☢'
    local s2 = 'İ'
    testcase:is(s1 < s2, false, 'test binary cmp')
    testcase:is(utf8.cmp(s1, s2) < 0, true, 'test unicode <')
    testcase:is(utf8.cmp(s1, s1) == 0, true, 'test unicode eq')
    testcase:is(utf8.cmp(s2, s1) > 0, true, 'test unicode >')
    testcase:is(utf8.casecmp('a', 'A') == 0, true, 'test icase ==')
    testcase:is(utf8.casecmp('b', 'A') > 0, true, 'test icase >, first')
    testcase:is(utf8.casecmp('B', 'a') > 0, true, 'test icase >, second >')
    testcase:is(utf8.cmp('', '') == 0, true, 'test empty compare')
    testcase:is(utf8.cmp('', 'a') < 0, true, 'test left empty compare')
    testcase:is(utf8.cmp('a', '') > 0, true, 'test right empty compare')
    testcase:is(utf8.casecmp('', '') == 0, true, 'test empty icompare')
    testcase:is(utf8.casecmp('', 'a') < 0, true, 'test left empty icompare')
    testcase:is(utf8.casecmp('a', '') > 0, true, 'test right empty icompare')

    -- gh-3709: utf8 can not handle an empty string.
    testcase:is(utf8.lower(''), '', 'lower empty')
    testcase:is(utf8.upper(''), '', 'upper empty')
end)

os.exit(test:check() == true and 0 or -1)
