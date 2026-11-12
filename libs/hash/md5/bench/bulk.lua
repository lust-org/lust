-- Throughput: hashing large buffers (64 KiB and 1 MiB).
local md5 = require "lust.hash.md5"

local small = ("0123456789abcdef"):rep(4096) -- 64 KiB
local big = small:rep(16) -- 1 MiB

local acc = 0
for i = 1, 64 do
  local s = i % 16 == 0 and big or small
  acc = acc + md5.digest(s):byte(i % 16 + 1)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 7812, acc)
