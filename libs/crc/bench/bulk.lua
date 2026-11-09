-- Throughput: checksums of large buffers (64 KiB and 1 MiB).
local crc = require "lust.crc"

local small = ("0123456789abcdef"):rep(4096) -- 64 KiB
local big = small:rep(16) -- 1 MiB

local acc = 0
for i = 1, 100 do
  local s = i % 16 == 0 and big or small
  acc = (acc + crc.crc32(s) + crc.adler32(s)) % 4294967296
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 656008688, acc)
