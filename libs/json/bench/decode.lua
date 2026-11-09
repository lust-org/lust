-- Parsing a document of objects, arrays, strings, integers and doubles.
local json = require "lust.json"
local data = dofile(arg[0]:gsub("[^/]*$", "") .. "_data.lua")

local text = data.records_text(1500)

local acc = 0
for _ = 1, 10 do
  local t = json.decode(text)
  acc = acc + #t + t[#t].id + #t[1].tags + math.floor(t[7].address.geo[1] * 1000 + 0.5)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 30100, acc)
