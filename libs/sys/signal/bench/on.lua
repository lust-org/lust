-- Setting, replacing and removing callbacks, and handling signals sent
-- by a shell (most of the instructions are the shell's: they show the
-- cost of the handling only coarsely).
local signal = require "lust.sys.signal"

local n = 0
local function cb() n = n + 1 end
local acc = 0
for _ = 1, 20000 do
  signal.on("SIGUSR1", cb)
  signal.on("SIGUSR2", cb)
  signal.on("SIGUSR1", nil)
  signal.on("SIGUSR2", nil)
  acc = acc + 1
end
signal.on("SIGUSR1", cb)
signal.on("SIGUSR2", cb)
for _ = 1, 20 do
  os.execute("kill -USR1 $PPID; kill -USR2 $PPID")
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 20000 and n == 40, acc .. " " .. n)
