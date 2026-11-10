-- Small requests (tokens, keys) and a few large ones.
local random = require "lust.sys.random"

local acc = 0
for _ = 1, 50000 do
  acc = acc + #random.bytes(16)
end
for _ = 1, 20 do
  acc = acc + #random.bytes(65536)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 50000 * 16 + 20 * 65536, acc)
