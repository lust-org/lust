-- The numbers of signals by name: classic ones (looked up among all the
-- names) and a realtime one (parsed once none of them matched: the
-- costly case). Usually called once, not in a loop: few rounds.
local signal = require "lust.sys.signal"

local names = { "SIGHUP", "SIGINT", "SIGKILL", "SIGTERM", "SIGWINCH", "SIGRTMIN+3" }
local acc = 0
for _ = 1, 2000 do
  for i = 1, #names do
    acc = acc + (signal.number(names[i]) > 0 and 1 or 0)
  end
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 12000, acc)
