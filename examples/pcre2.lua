-- lust.pcre2: PCRE2 (Perl compatible) regular expressions.
--
--   build/src/lust examples/pcre2.lua

local pcre2 = require "lust.pcre2"

local log = assert(pcre2.new([[(?<level>[A-Z]+) (?<user>\w+)@(?<host>[\w.]+): (.*)]]))
local line = "ERROR bob@example.com: disk full"

print(log:find(line))                  --> 1  32  ERROR  bob  example.com  disk full
local c = log:captures(line)
print(c.level, c.user, c.host)         --> ERROR  bob  example.com

-- Iterate over all matches
for k, v in assert(pcre2.new("(\\w+)=(\\d+)")):gmatch("a=1 b=22 c=333") do
  print(k, v)
end

-- Replace; \1 .. \9 refer to groups, \0 to the whole match
print(assert(pcre2.new("(\\w+)@(\\w+)")):gsub("bob@example", "\\2: \\1"))

-- A replacement function
local n = assert(pcre2.new("\\d+"))
print((n:gsub("1 22 333", function(d) return d * 2 end)))

-- Beyond regular languages: backreferences, lookaround, recursion ...
print(assert(pcre2.new("\\b(\\w+) \\1\\b")):match("this is is a typo"))   --> is
print(assert(pcre2.new("\\((?:[^()]|(?R))*\\)")):match("f(a(b)c)"))       --> (a(b)c)

-- Options (the text is UTF-8), and quoting user input
print(assert(pcre2.new("É", { caseless = true })):test("é"))   --> true
print(pcre2.quote("1+1=2?"))                                  --> 1\+1\=2\?

-- Errors in the pattern are returned, not raised
print(pcre2.new("(unclosed"))

-- Backtracking is bounded: a pathological pattern raises instead of hanging
local evil = assert(pcre2.new("^(a+)+$"))
print(pcall(evil.test, evil, string.rep("a", 100) .. "b"))
