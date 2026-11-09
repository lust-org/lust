local T = require "t"
local re2 = require "lust.re2"

local function new(...) return assert(re2.new(...)) end

-- Pack results including trailing falses/nils.
local function pack(...) return { n = select("#", ...), ... } end

T.run {
  test = function()
    local re = new("a+b")
    T.eq(re:test("xxaab"), true)
    T.eq(re:test("xxaa"), false)
  end,

  test_init = function()
    local re = new("a+b")
    T.eq(re:test("aab", 2), true) -- "ab"
    T.eq(re:test("aab", 3), false)
    T.eq(re:test("aab", -2), true)
    T.eq(re:test("aab", 100), false)
  end,

  find = function()
    local re = new("(a+)(b)?")
    local r = pack(re:find("xaac"))
    T.eq(r.n, 4)
    T.eq(r[1], 2); T.eq(r[2], 3); T.eq(r[3], "aa"); T.eq(r[4], false)
    T.eq(new("b"):find("abc"), 2)
    T.eq(new("z"):find("abc"), nil)
  end,

  find_without_groups = function()
    local r = pack(new("b+"):find("abbbc"))
    T.eq(r.n, 2)
    T.eq(r[1], 2); T.eq(r[2], 4)
  end,

  find_empty_match = function()
    local a, b = new("x*"):find("abc")
    T.eq(a, 1); T.eq(b, 0)
    a, b = new("x*"):find("abc", 4)
    T.eq(a, 4); T.eq(b, 3)
  end,

  match = function()
    T.eq(new("b+"):match("abbbc"), "bbb")
    local u, h = new("(\\w+)@(\\w+)"):match("hi bob@example.com")
    T.eq(u, "bob"); T.eq(h, "example")
    T.eq(new("z"):match("abc"), nil)
  end,

  match_context_of_init = function()
    -- ^ and \b look at the text before init
    T.eq(new("^b"):match("ab", 2), nil)
    T.eq(new("\\bb"):match("ab", 2), nil)
    T.eq(new("\\bb"):match("a b", 3), "b")
  end,

  fullmatch = function()
    local re = new("a(b+)")
    T.eq(re:fullmatch("abbb"), "bbb")
    T.eq(re:fullmatch("abbbc"), nil)
    T.eq(re:fullmatch("xabbb"), nil)
  end,

  captures_table = function()
    local t = new("(?P<user>\\w+)@(?P<host>\\w+)"):captures("bob@example")
    T.eq(t[0], "bob@example")
    T.eq(t[1], "bob"); T.eq(t.user, "bob")
    T.eq(t[2], "example"); T.eq(t.host, "example")
    T.eq(new("x"):captures("y"), nil)
  end,

  groups_and_names = function()
    local re = new("(a)(?P<n>b)(?:c)(d)")
    T.eq(re:groups(), 3)
    local gi = re:groupindex()
    T.eq(gi.n, 2)
    T.eq(next(gi, "n"), nil)
  end,

  gmatch = function()
    local got = {}
    for w in new("\\w+"):gmatch("one two  three") do got[#got + 1] = w end
    T.eq(table.concat(got, ","), "one,two,three")

    got = {}
    for k, v in new("(\\w)=(\\d)"):gmatch("a=1, b=2") do got[#got + 1] = k .. v end
    T.eq(table.concat(got, ","), "a1,b2")
  end,

  gmatch_empty_matches = function()
    local n = 0
    for _ in new("x*"):gmatch("abc") do n = n + 1 end
    T.eq(n, 4)
    -- an empty match right behind a non-empty one is skipped
    local got = {}
    for m in new("b*"):gmatch("abc") do got[#got + 1] = "[" .. m .. "]" end
    T.eq(table.concat(got), "[][b][]")
  end,

  gsub_string = function()
    local s, n = new("(\\w+)@(\\w+)"):gsub("bob@example", "\\2 at \\1 (\\0)")
    T.eq(s, "example at bob (bob@example)"); T.eq(n, 1)
    s, n = new("a"):gsub("banana", "o")
    T.eq(s, "bonono"); T.eq(n, 3)
    s, n = new("a"):gsub("banana", "o", 2)
    T.eq(s, "bonona"); T.eq(n, 2)
    T.eq((new("a"):gsub("banana", "\\\\")), "b\\n\\n\\")
  end,

  gsub_empty_matches = function()
    -- same result as RE2::GlobalReplace
    T.eq((new("x*"):gsub("abc", "-")), "-a-b-c-")
    T.eq((new("b*"):gsub("abc", "-")), "-a-c-")
    T.eq((new(""):gsub("", "-")), "-")
  end,

  gsub_function = function()
    local s, n = new("(\\d+)"):gsub("a1 b22", function(d) return "<" .. d * 2 .. ">" end)
    T.eq(s, "a<2> b<44>"); T.eq(n, 2)
    -- false keeps the match
    s = new("\\d"):gsub("a1b2", function(d) if d == "1" then return false end return "x" end)
    T.eq(s, "a1bx")
    -- the callback may use the very same regexp
    local re = new("\\d")
    s = re:gsub("1 2", function(d) re:find("zzz9"); return "<" .. d .. ">" end)
    T.eq(s, "<1> <2>")
  end,

  gsub_table = function()
    T.eq((new("\\$(\\w+)"):gsub("$a and $b", { a = "1" })), "1 and $b")
  end,

  gsub_bad_replacement = function()
    T.raises(function() new("a"):gsub("a", "\\1") end, "does not exist")
    T.raises(function() new("a"):gsub("a", "\\x") end, "must be followed")
    T.raises(function() new("a"):gsub("a") end, "bad argument #3")
  end,

  utf8 = function()
    local re = new("(.)")
    T.eq(re:match("é"), "é")
    T.eq(select(2, new("."):gsub("héé", "x")), 3)
    T.eq((new("x*"):gsub("é", "-")), "-é-")
    T.eq(new("é", { caseless = true }):test("É"), true)
  end,

  latin1 = function()
    local re = new(".", { latin1 = true })
    T.eq(select(2, re:gsub("\xe9\xe9", "x")), 2)
  end,

  binary_subject = function()
    T.eq(new("b"):find("a\0b"), 3)
    T.eq(new("a\\x00b"):test("a\0b"), true)
    T.eq(new("a\0b"):test("a\0b"), true) -- NUL in the pattern itself
  end,

  options = function()
    T.eq(new("ABC", { caseless = true }):test("abc"), true)
    T.eq(new("a.c"):test("a\nc"), false)
    T.eq(new("a.c", { dot_nl = true }):test("a\nc"), true)
    T.eq(new("a.c", { literal = true }):test("abc"), false)
    T.eq(new("a.c", { literal = true }):test("a.c"), true)
    T.eq(new("(a)", { never_capture = true }):groups(), 0)
    T.raises(function() re2.new("a", { nope = true }) end, "unknown option")
  end,

  longest_match = function()
    T.eq(new("a+?"):match("aaa"), "a")
    T.eq(new("a+?", { longest = true }):match("aaa"), "aaa")
  end,

  compile_errors = function()
    local re, err = re2.new("(")
    T.eq(re, nil)
    assert(err:find("missing )", 1, true), err)
    re, err = re2.new("a**")
    T.eq(re, nil)
    assert(type(err) == "string" and #err > 0)
    -- RE2 refuses what it cannot match in linear time
    re, err = re2.new("(a)\\1")
    T.eq(re, nil)
    assert(err:find("invalid escape sequence", 1, true), err)
  end,

  max_mem = function()
    T.eq(re2.new("[a-z]{500}") ~= nil, true)
    local re, err = re2.new("[a-z]{500}", { max_mem = 1 << 12 })
    T.eq(re, nil)
    assert(err:find("pattern too large", 1, true), err)
  end,

  no_catastrophic_backtracking = function()
    local subject = string.rep("a", 5000) .. "b"
    T.eq(new("^(a+)+$"):test(subject), false)
    T.eq(new("^(a*)*$"):test(subject .. "c"), false)
    T.eq(new("(x+x+)+y"):test(string.rep("x", 5000)), false)
  end,

  quote = function()
    T.eq(re2.quote("a.b*c"), "a\\.b\\*c")
    T.eq(re2.quote("a_1é"), "a_1é")
    T.eq(re2.quote("a\0b"), "a\\x00b")
    local nasty = "[x]+(?)\\^$|{}"
    T.eq(new(re2.quote(nasty)):fullmatch(nasty), nasty)
  end,

  program_size = function()
    assert(new("a"):program_size() > 0)
    assert(new("a{10}"):program_size() > new("a"):program_size())
    T.eq(new("a(b)"):pattern(), "a(b)")
  end,

  argument_errors = function()
    T.raises(function() re2.new() end, "bad argument #1")
    T.raises(function() new("a"):find() end, "bad argument #2")
    T.raises(function() new("a").find("x", "a") end, "bad argument #1")
    T.raises(function() new("a"):find("a", "x") end, "bad argument #3")
  end,

  garbage_collection = function()
    -- would blow up (or leak visibly) if regexps were not freed
    for _ = 1, 20000 do re2.new("(a+)(b+)"):test("aabb") end
    collectgarbage()
  end,
}
