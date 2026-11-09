-- Searching: test/find/match over log lines (no captures, one capture,
-- several captures, a miss, a start offset).
local re = require "lust.re2"
local lines = dofile(arg[0]:gsub("[^/]*$", "") .. "_lines.lua")(2000)

local err = assert(re.new("ERROR|WARN"))
local took = assert(re.new("took=(\\d+)ms"))
local stamp = assert(re.new("^(\\d+)-(\\d+)-(\\d+) (\\d+):(\\d+):(\\d+)"))
local none = assert(re.new("panic: [a-z]+"))

local n = 0
for _ = 1, 5 do
  for i = 1, #lines do
    local l = lines[i]
    if err:test(l) then n = n + 1 end
    n = n + tonumber(took:match(l))
    local s, e, y = stamp:find(l)
    n = n + e - s + #y
    if none:find(l) then n = n + 1 end
    if took:test(l, 30) then n = n + 1 end
  end
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(n == 5122675, n)
