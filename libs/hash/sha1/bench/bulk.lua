-- Throughput: hashing large buffers (64 KiB and 1 MiB).
local sha1 = require "lust.hash.sha1"

local small = ("0123456789abcdef"):rep(4096) -- 64 KiB
local big = small:rep(16) -- 1 MiB

local acc = 0
for i = 1, 64 do
  local s = i % 16 == 0 and big or small
  acc = acc + sha1.digest(s):byte(i % 20 + 1)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 6891, acc)
