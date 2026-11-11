-- Sleeps: mostly ones that return at once (the cost of the call itself:
-- a sleep of nothing, a deadline that passed), and a few real ones of
-- half a millisecond, in a steady rhythm.
local time = require "lust.sys.time"

local acc = 0
for _ = 1, 20000 do
  time.sleep(0)
  time.sleep(-1)
  time.sleep_until(0)
  acc = acc + 1
end
local next = time.monotonic()
for _ = 1, 20 do
  time.sleep(0.0005)
  next = next + 0.001
  time.sleep_until(next)
  acc = acc + (time.monotonic() >= next and 1 or 0)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 20020, acc)
