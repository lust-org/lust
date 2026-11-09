-- Writing the same document: tables (objects and arrays), strings and
-- numbers of all kinds.
local json = require "lust.json"
local data = dofile(arg[0]:gsub("[^/]*$", "") .. "_data.lua")

local value = data.records(1500)

local acc = 0
for _ = 1, 10 do
  acc = acc + #json.encode(value)
end
assert(acc == 2937570, acc)
