-- lust.re2: RE2 regular expressions (linear time, UTF-8).
--
--   # re2 is not linked into `lust`, so it is looked up in the build tree:
--   LUA_CPATH="$PWD/build/libs/?.so" build/src/lust examples/re2.lua

local re2 = require "lust.re2"

local log = assert(re2.new([[(?P<level>[A-Z]+) (?P<user>\w+)@(?P<host>[\w.]+): (.*)]]))
local line = "ERROR bob@example.com: disk full"

print(log:find(line))                  --> 1  32  ERROR  bob  example.com  disk full
local c = log:captures(line)
print(c.level, c.user, c.host)         --> ERROR  bob  example.com

-- Iterate over all matches
for k, v in assert(re2.new("(\\w+)=(\\d+)")):gmatch("a=1 b=22 c=333") do
  print(k, v)
end

-- Replace; \1 .. \9 refer to groups, \0 to the whole match
print(assert(re2.new("(\\w+)@(\\w+)")):gsub("bob@example", "\\2: \\1"))

-- A replacement function
local n = assert(re2.new("\\d+"))
print((n:gsub("1 22 333", function(d) return d * 2 end)))

-- Options (the text is UTF-8), and quoting user input
print(assert(re2.new("É", { caseless = true })):test("é"))   --> true
print(re2.quote("1+1=2?"))                                  --> 1\+1\=2\?

-- Errors are returned, not raised
print(re2.new("(unclosed"))

-- No catastrophic backtracking, whatever the pattern
local evil = assert(re2.new("^(a+)+$"))
print(evil:test(string.rep("a", 100000) .. "b"))
