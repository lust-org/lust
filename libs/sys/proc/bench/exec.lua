-- exec: failing ones (the program is searched in PATH, the descriptors
-- prepared and restored, the working directory changed and changed back),
-- then one that replaces this process with `true` (whose instructions
-- perf counts, too: the same process).
local proc = require "lust.sys.proc"

local acc = 0
local argv, in_root = { "lust-no-such-program", "arg" }, { cwd = "/" }
for _ = 1, 500 do
  local _, _, code = proc.exec(argv)
  acc = acc + (code == "ENOENT" and 1 or 0)
  _, _, code = proc.exec(argv, in_root)
  acc = acc + (code == "ENOENT" and 1 or 0)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 1000, acc)
assert(proc.exec { "true" })
