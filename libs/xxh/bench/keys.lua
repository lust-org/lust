-- Hashing many distinct, freshly built strings (as when hashing keys),
-- so string creation and interning are part of the work.
local xxh = require "lust.xxh"

local acc32, acc64 = 0, 0
for i = 1, 50000 do
  local k = "user:" .. i .. ":session"
  acc32 = (acc32 + xxh.xxh32(k)) % 4294967296
  acc64 = acc64 + xxh.xxh64(k):byte(2)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc32 == 1193010531 and acc64 == 3511913, acc32 .. " " .. acc64)
