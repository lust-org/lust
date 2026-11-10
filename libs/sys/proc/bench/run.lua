-- Starting programs and talking to them. Most of the instructions are the
-- children's (perf counts them too), which shows a slowdown of ours only
-- coarsely; the counts are stable though.
local proc = require "lust.sys.proc"

local acc = 0
for _ = 1, 100 do
  acc = acc + proc.run { "true" }
end
local input = ("0123456789abcdef"):rep(16384) -- 256 KiB
for _ = 1, 20 do
  local status, out, err = proc.run({ "cat" }, { input = input })
  acc = acc + status + (out == input and 1 or 0) + #err
end
for _ = 1, 20 do
  local _, out = proc.run({ "true" }, { env = { LUST_BENCH = "1" }, stdout = "null" })
  acc = acc + (out == nil and 1 or 0)
end
local _, _, code = proc.run { "lust-no-such-program" }
acc = acc + (code == "ENOENT" and 1 or 0)
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 41, acc)
