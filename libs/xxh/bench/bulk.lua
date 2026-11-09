-- Throughput: hashing large buffers (64 KiB and 1 MiB).
local xxh = require "lust.xxh"

local small = ("0123456789abcdef"):rep(4096) -- 64 KiB
local big = small:rep(16) -- 1 MiB

local acc32, acc64 = 0, 0
for i = 1, 200 do
  local s = i % 16 == 0 and big or small
  acc32 = (acc32 + xxh.xxh32(s, i)) % 4294967296
  acc64 = acc64 + xxh.xxh64(s, i):byte(1)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc32 == 3016611464 and acc64 == 13653, acc32 .. " " .. acc64)
