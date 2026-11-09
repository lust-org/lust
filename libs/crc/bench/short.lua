-- Call overhead: crc32/adler32 of short strings (1..64 bytes).
local crc = require "lust.crc"

local strs = {}
for n = 1, 64 do strs[n] = ("abcdefghijklmnopqrstuvwxyz0123456789"):rep(2):sub(1, n) end

local acc = 0
for i = 1, 50000 do
  local s = strs[i % 64 + 1]
  acc = (acc + crc.crc32(s) + crc.adler32(s)) % 4294967296
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 2952107477, acc)
