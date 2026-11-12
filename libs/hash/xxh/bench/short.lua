-- Call overhead: xxh32/xxh64 of short strings (1..64 bytes), with and
-- without a seed, many times over the same few strings.
local xxh = require "lust.hash.xxh"

local strs = {}
for n = 1, 64 do strs[n] = ("abcdefghijklmnopqrstuvwxyz0123456789"):rep(2):sub(1, n) end

local acc32, acc64 = 0, 0
for i = 1, 20000 do
  local s = strs[i % 64 + 1]
  acc32 = (acc32 + xxh.xxh32(s) + xxh.xxh32(s, i)) % 4294967296
  acc64 = acc64 + xxh.xxh64(s):byte(1) + xxh.xxh64(s, i):byte(16)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc32 == 1049966064 and acc64 == 2831253, acc32 .. " " .. acc64)
