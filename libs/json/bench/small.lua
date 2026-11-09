-- Call overhead: many tiny documents and scalars.
local json = require "lust.json"

local docs = { '{"a":1}', "[1,2,3]", '"str"', "true", "null", "12.5", '{"k":[{"x":1},{"y":[]}]}' }
local values = { { a = 1 }, { 1, 2, 3 }, "str", true, 12.5, { k = { { x = 1 }, { y = json.empty_array } } } }

local acc = 0
for i = 1, 20000 do
  local v = json.decode(docs[i % #docs + 1])
  if type(v) == "table" then acc = acc + 1 end
  acc = acc + #json.encode(values[i % #values + 1])
end
assert(acc == 178567, acc)
