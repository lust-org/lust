-- Hashers: many small pieces of freshly built strings (as when hashing
-- lines or records as they come), one hasher per record.
local sha256 = require "lust.hash.sha256"

local acc = 0
for i = 1, 5000 do
  local h = sha256.new()
  for j = 1, 10 do h:update("line " .. i .. ":" .. j .. "\n") end
  acc = acc + h:hex():byte(2)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 350231, acc)
