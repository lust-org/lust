-- Children we talk to: spawn with pipes, line by line reads, writes to
-- stdin, poll, kill and wait. Most of the instructions are the children's
-- (perf counts them too); the counts are stable though.
local proc = require "lust.sys.proc"

local acc = 0
-- Lines streamed from a child (one read fills the buffer with many).
for _ = 1, 20 do
  local c = assert(proc.spawn({ "seq", "2000" }, { stdout = "pipe" }))
  for line in c.stdout:lines() do acc = acc + #line end
  acc = acc + c:wait()
end
-- A conversation: a line written, one read back.
local c = assert(proc.spawn({ "cat" }, { stdin = "pipe", stdout = "pipe" }))
for i = 1, 200 do
  c.stdin:write("line ", i, "\n")
  acc = acc + #c.stdout:read()
end
local status, rest = c:wait()
acc = acc + status + #rest
-- Polling the output of two children, then killing one.
for _ = 1, 20 do
  local a = assert(proc.spawn({ "echo", "a" }, { stdout = "pipe" }))
  -- (killed once it's blocked reading: where it is then doesn't vary)
  local b = assert(proc.spawn({ "cat" }, { stdin = "pipe", stdout = "pipe" }))
  b.stdin:write("b\n")
  acc = acc + #b.stdout:read()
  local ready = proc.poll { b.stdout, a.stdout }
  acc = acc + #ready + #a.stdout:read()
  assert(b:kill())
  acc = acc + (b:wait() == "SIGTERM" and 1 or 0) + a:wait()
end
-- The result, so the work can neither be skipped nor silently go wrong.
-- (seq: 6893 digits; "line <i>": 5 bytes + 492 digits in all)
assert(acc == 20 * 6893 + (200 * 5 + 492) + 20 * 4, acc)
