-- Call overhead: hex/digest of short strings (1..64 bytes), many times
-- over the same few strings.
local sha256 = require "lust.hash.sha256"

local strs = {}
for k = 1, 64 do strs[k] = ("abcdefghijklmnopqrstuvwxyz0123456789"):rep(2):sub(1, k) end

local acc = 0
for i = 1, 20000 do
  local s = strs[i % 64 + 1]
  acc = acc + sha256.hex(s):byte(1) + sha256.digest(s):byte(i % 32 + 1)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 4060160, acc)
