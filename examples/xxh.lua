-- lust.xxh (Lua C API flavour): xxHash of a few strings.
--
--   build/src/lust examples/xxh.lua

local xxh = require "lust.xxh"

for _, s in ipairs { "", "abc", "hello world" } do
  -- xxh32 returns a Lua number, xxh64 a hex string (64 bit do not fit
  -- into a double).
  print(string.format("%-14q xxh32=%08x xxh64=%s", s, xxh.xxh32(s), xxh.xxh64(s)))
end

-- The seed is an optional second argument and changes the result.
print("xxh64 with seed 1:", xxh.xxh64("abc", 1))

-- Errors are regular Lua errors.
print(pcall(xxh.xxh64, "abc", -1))
