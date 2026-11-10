-- Reading the clock in a loop, as when timing many small pieces of work.
local time = require "lust.sys.time"

local prev, ok = time.monotonic(), 0
for _ = 1, 200000 do
  local now = time.monotonic()
  if now >= prev then ok = ok + 1 end
  prev = now
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(ok == 200000, ok)
