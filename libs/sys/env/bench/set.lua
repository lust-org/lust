-- Setting and removing variables (a few values, set over and over: libc
-- keeps every distinct value), read back with os.getenv.
local env = require "lust.sys.env"

local names, values = {}, { "a", "bb", "ccc", "dddd" }
for i = 1, 10 do names[i] = "LUST_BENCH_" .. i end

local acc = 0
for _ = 1, 5000 do
  for i = 1, 10 do
    env.set(names[i], values[i % 4 + 1])
    acc = acc + #os.getenv(names[i])
  end
  for i = 1, 10, 2 do
    env.unset(names[i])
    acc = acc + (os.getenv(names[i]) == nil and 1 or 0)
  end
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 5000 * (25 + 5), acc)
