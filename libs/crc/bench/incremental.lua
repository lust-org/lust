-- Streaming: checksums continued chunk by chunk (the optional previous
-- value argument), as when reading a file in blocks.
local crc = require "lust.crc"

local chunks = {}
for i = 1, 256 do chunks[i] = ("%04d"):format(i):rep(256) end -- 1 KiB each

local acc = 0
for _ = 1, 40 do
  local c, a = 0, 1
  for i = 1, #chunks do
    c = crc.crc32(chunks[i], c)
    a = crc.adler32(chunks[i], a)
  end
  acc = (acc + c + a) % 4294967296
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 2472785048, acc)
