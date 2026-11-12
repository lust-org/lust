-- Call overhead: hex/digest of short strings (1..64 bytes), many times
-- over the same few strings.
local sha1 = require "lust.hash.sha1"

local strs = {}
for k = 1, 64 do strs[k] = ("abcdefghijklmnopqrstuvwxyz0123456789"):rep(2):sub(1, k) end

local acc = 0
for i = 1, 20000 do
  local s = strs[i % 64 + 1]
  acc = acc + sha1.hex(s):byte(1) + sha1.digest(s):byte(i % 20 + 1)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 4111740, acc)
