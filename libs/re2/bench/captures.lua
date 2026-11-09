-- Extracting fields: named and numbered captures as tables.
local re = require "lust.re2"
local lines = dofile(arg[0]:gsub("[^/]*$", "") .. "_lines.lua")(2000)

local log = assert(re.new(
  "(?P<level>[A-Z]+) (?P<user>\\w+)@(?P<host>[\\w.]+): id=(?P<id>\\d+) took=(\\d+)ms (.*)"))

local n = 0
for _ = 1, 5 do
  for i = 1, #lines do
    local c = log:captures(lines[i])
    n = n + #c.level + #c.user + #c.host + tonumber(c.id) + tonumber(c[5]) + #c[6] + #c[0]
  end
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(n == 521468230, n)
