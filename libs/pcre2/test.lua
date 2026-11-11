local T = require "t"
local pcre2 = require "lust.pcre2"

local function new(...) return assert(pcre2.new(...)) end

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
    T.eq(new("(?<=a)b"):match("ab", 2), "b")
  end,

  fullmatch = function()
    local re = new("a(b+)")
    T.eq(re:fullmatch("abbb"), "bbb")
    T.eq(re:fullmatch("abbbc"), nil)
    T.eq(re:fullmatch("xabbb"), nil)
    -- the whole subject, not just the first alternative that matches
    T.eq(new("a|ab"):fullmatch("ab"), "ab")
  end,

  captures_table = function()
    local t = new("(?<user>\\w+)@(?P<host>\\w+)"):captures("bob@example")
    T.eq(t[0], "bob@example")
    T.eq(t[1], "bob"); T.eq(t.user, "bob")
    T.eq(t[2], "example"); T.eq(t.host, "example")
    T.eq(new("x"):captures("y"), nil)
  end,

  groups_and_names = function()
    local re = new("(a)(?<n>b)(?:c)(d)")
    T.eq(re:groups(), 3)
    local gi = re:groupindex()
    T.eq(gi.n, 2)
    T.eq(next(gi, "n"), nil)
  end,

  duplicate_names = function()
    local re = new("(?:(?<x>a)|(?<x>b))", { dupnames = true })
    T.eq(re:groups(), 2)
    T.eq(re:groupindex().x, 1)
    T.eq(re:captures("b").x, "b")
    T.eq(re:captures("a").x, "a")
  end,

  gmatch = function()
    local got = {}
    for w in new("\\w+"):gmatch("one two  three") do got[#got + 1] = w end
    T.eq(table.concat(got, ","), "one,two,three")

    got = {}
    for k, v in new("(\\w)=(\\d)"):gmatch("a=1, b=2") do got[#got + 1] = k .. v end
    T.eq(table.concat(got, ","), "a1,b2")
  end,

  gmatch_independent_loops = function()
    -- nested and interleaved loops over one regexp keep their own state
    local re = new("\\w")
    local got = {}
    for a in re:gmatch("xy") do
      for b in re:gmatch("12") do got[#got + 1] = a .. b end
    end
    T.eq(table.concat(got, ","), "x1,x2,y1,y2")
    local i, j = re:gmatch("abc"), re:gmatch("de")
    T.eq(i() .. j() .. i() .. j(), "adbe")
    T.eq(j(), nil)
    T.eq(i(), "c")
    -- abandoned loops are just garbage
    for _ = 1, 1000 do for _ in re:gmatch("abc") do break end end
    collectgarbage()
  end,

  gmatch_empty_matches = function()
    local n = 0
    for _ in new("x*"):gmatch("abc") do n = n + 1 end
    T.eq(n, 4)
    -- as in Perl: an empty match may follow a non-empty one
    local got = {}
    for m in new("b*"):gmatch("abc") do got[#got + 1] = "[" .. m .. "]" end
    T.eq(table.concat(got), "[][b][][]")
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
    -- same result as Perl and Python
    T.eq((new("x*"):gsub("abc", "-")), "-a-b-c-")
    T.eq((new("b*"):gsub("abc", "-")), "-a--c-")
    T.eq((new("a*"):gsub("baaac", "-")), "-b--c-")
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
    -- (argument numbers of a method call do not count self)
    T.raises(function() new("a"):gsub("a") end, "bad argument #2")
  end,

  utf8 = function()
    local re = new("(.)")
    T.eq(re:match("é"), "é")
    T.eq(select(2, new("."):gsub("héé", "x")), 3)
    T.eq((new("x*"):gsub("é", "-")), "-é-")
    T.eq(new("é", { caseless = true }):test("É"), true)
    -- \w is ASCII only unless Unicode properties are asked for
    T.eq(new("^\\w+$"):test("é"), false)
    T.eq(new("^\\w+$", { ucp = true }):test("é"), true)
    T.eq(new("^\\p{Lu}$"):test("É"), true)
  end,

  invalid_utf8 = function()
    T.raises(function() new("."):test("\255") end, "UTF-8")
    T.raises(function() new("."):gsub("a\255b", "x") end, "UTF-8")
    -- ... also a start position inside of a character
    T.raises(function() new("."):test("é", 2) end, "UTF")
    local re, err = pcre2.new("\255")
    T.eq(re, nil)
    assert(err:find("UTF", 1, true), err)
  end,

  bytes = function()
    local re = new(".", { bytes = true })
    T.eq(select(2, re:gsub("\233\233", "x")), 2)
    T.eq(select(2, re:gsub("\255", "x")), 1)
    T.eq(select(2, new(".", { bytes = true }):gsub("é", "x")), 2)
  end,

  binary_subject = function()
    T.eq(new("b"):find("a\0b"), 3)
    T.eq(new("a\\x00b"):test("a\0b"), true)
    T.eq(new("a\0b"):test("a\0b"), true) -- NUL in the pattern itself
  end,

  options = function()
    T.eq(new("ABC", { caseless = true }):test("abc"), true)
    T.eq(new("a.c"):test("a\nc"), false)
    T.eq(new("a.c", { dotall = true }):test("a\nc"), true)
    T.eq(new("a.c", { literal = true }):test("abc"), false)
    T.eq(new("a.c", { literal = true }):test("a.c"), true)
    T.eq(new("^b", { multiline = true }):test("a\nb"), true)
    T.eq(new("^b"):test("a\nb"), false)
    T.eq(new("a b # comment", { extended = true }):test("ab"), true)
    T.eq(new("a+?", { ungreedy = true }):match("aaa"), "aaa")
    T.eq(new("a$"):test("a\n"), true)
    T.eq(new("a$", { dollar_endonly = true }):test("a\n"), false)
    T.eq(new("(a)", { no_auto_capture = true }):groups(), 0)
    T.eq(new("b", { anchored = true }):test("ab"), false)
    T.eq(new("(a)|b\\1"):test("b"), false)
    T.eq(new("(a)?b\\1", { match_unset_backref = true }):test("b"), true)
    T.raises(function() pcre2.new("a", { nope = true }) end, "unknown option")
    T.raises(function() pcre2.new("a", { match_limit = "x" }) end, "must be a number")
  end,

  perl_features = function()
    T.eq(new("(\\w)\\1"):match("abccd"), "c") -- backreference
    T.eq(new("foo(?=bar)"):match("foobar"), "foo") -- lookahead
    T.eq(new("(?<!x)y"):find("xyzy"), 4) -- lookbehind
    T.eq(new("a++a"):test("aaa"), false) -- possessive
    T.eq(new("(?>a+)b"):test("aaab"), true) -- atomic
    T.eq(new("\\((?:[^()]|(?R))*\\)"):match("x(a(b)c)y"), "(a(b)c)") -- recursion
    T.eq(new("(?i)abc"):test("ABC"), true)
    T.eq(new("^(?<a>x)(?<b>y)$"):captures("xy").b, "y")
  end,

  compile_errors = function()
    local re, err = pcre2.new("(")
    T.eq(re, nil)
    assert(err:find("missing closing parenthesis", 1, true), err)
    -- 1-based position, here right behind the pattern (#"(" + 1)
    assert(err:find("(at position 2)", 1, true), err)
    re, err = pcre2.new("a**")
    T.eq(re, nil)
    assert(err:find("(at position 4)", 1, true), err)
    re, err = pcre2.new("[b-a]")
    T.eq(re, nil)
    assert(err:find("range", 1, true), err)
  end,

  match_limit = function()
    -- classic catastrophic backtracking is stopped, not endured
    local subject = string.rep("a", 40) .. "b"
    local re = new("^(a+)+$", { match_limit = 10000 })
    T.raises(function() re:test(subject) end, "limit")
    T.raises(function() re:find(subject) end, "limit")
    T.raises(function() re:gsub(subject, "x") end, "limit")
    -- the regexp is still usable afterwards
    T.eq(re:test("aaa"), true)
    -- with the default limit as well
    T.raises(function() new("^(a+)+$"):test(string.rep("a", 60) .. "b") end, "limit")
  end,

  max_mem = function()
    -- every iteration leaves a backtracking frame behind
    local re = new("(?:a|b)*\\d", { max_mem = 2^14, match_limit = 1e9 })
    T.eq(re:test("aab1"), true)
    T.raises(function() re:test(string.rep("ab", 100000)) end, "limit")
    T.eq(new("(?:a|b)*\\d", { match_limit = 1e9 }):test(string.rep("ab", 100000) .. "1"), true)
  end,

  quote = function()
    T.eq(pcre2.quote("a.b*c"), "a\\.b\\*c")
    T.eq(pcre2.quote("a_1é"), "a_1é")
    T.eq(pcre2.quote("a\0b"), "a\\x00b")
    local nasty = "[x]+(?)\\^$|{}\n #-"
    T.eq(new(pcre2.quote(nasty)):fullmatch(nasty), nasty)
    T.eq(new(pcre2.quote(nasty), { extended = true }):fullmatch(nasty), nasty)
    T.eq(new(pcre2.quote("a\0b")):fullmatch("a\0b"), "a\0b")
  end,

  pattern = function()
    T.eq(new("a(b)"):pattern(), "a(b)")
  end,

  argument_errors = function()
    T.raises(function() pcre2.new() end, "bad argument #1")
    -- (argument numbers of a method call do not count self)
    T.raises(function() new("a"):find() end, "bad argument #1")
    T.raises(function() new("a").find("x", "a") end, "bad argument #1")
    T.raises(function() new("a"):find("a", "x") end, "bad argument #2")
  end,

  garbage_collection = function()
    -- would blow up (or leak visibly) if regexps were not freed
    for _ = 1, 20000 do pcre2.new("(a+)(b+)"):test("aabb") end
    collectgarbage()
  end,

  -- A finalizer run while a call pushes its results matches with the same
  -- regexp (whose match data every match of it shares): the outer call's
  -- results must stay its own. The outer checks allocate nothing, so the
  -- finalizer runs within the calls.
  same_regexp_during_gc = function()
    local function run(re, inner_subject)
      local parts = {}
      for i = 1, 20000 do parts[i] = "k" .. i .. "=" .. i end
      local big = table.concat(parts, " ")
      local n = T.during_gc(function()
        for _ = 1, 3 do
          local count = 0
          for k, v in re:gmatch(big) do -- "k<i>", "<i>"
            count = count + 1
            T.eq(#k == #v + 1 and k:byte(1) == 107, true, "gmatch")
          end
          T.eq(count, 20000)
          local out, nsub = re:gsub(big, "\\2\\1")
          T.eq(nsub, 20000)
          T.eq(#out, #big - 20000 * 1, "gsub")
          local init, found = 1, 0
          while true do
            local a, b, k, v = re:find(big, init)
            if not a then break end
            T.eq(b - a + 1, #k + #v + 1, "find")
            found, init = found + 1, b + 1
            if found % 1000 == 0 then
              local c = re:captures(big, a)
              T.eq(c[1], k, "captures")
              T.eq(c[0], k .. "=" .. v)
            end
          end
          T.eq(found, 20000)
        end
      end, function()
        local c = re:captures(inner_subject)
        T.eq(c[1] .. "|" .. c[2], "zz|9", "inner")
      end)
      assert(n > 0, "no finalizer ran")
    end
    run(new("(\\w+)=(\\d+)"), "zz=9")
    -- more groups than the offsets of a call have room for on the C stack
    run(new("(\\w+)=(\\d+)" .. ("()"):rep(20)), "zz=9")
  end,
}
