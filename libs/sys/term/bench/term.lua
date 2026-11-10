-- The checks a program does before printing: isatty and size of stdout
-- and stderr (pipes when benchmarked) and of a closed descriptor.
local term = require "lust.sys.term"

local acc = 0
for _ = 1, 20000 do
  for fd = 1, 2 do
    acc = acc + (term.isatty(fd) and 0 or 1)
    local cols, _, code = term.size(fd)
    acc = acc + (cols == nil and code == "ENOTTY" and 1 or 0)
  end
  local cols, _, code = term.size(12345)
  acc = acc + (cols == nil and code == "EBADF" and 1 or 0)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 20000 * 5, acc)
