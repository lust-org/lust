-- lust.xxhffi: same API as lust.xxh, but bound through the LuaJIT FFI.
--
--   build/src/lust examples/xxhffi.lua

local xxh = require "lust.xxhffi"

print("xxHash version:", xxh.version())

for _, s in ipairs { "", "abc", "hello world" } do
  print(string.format("%-14q xxh32=%08x xxh64=%s", s, xxh.xxh32(s), xxh.xxh64(s)))
end

-- Both flavours give identical results; compare them if the C API one is
-- available too.
local ok, capi = pcall(require, "lust.xxh")
if ok then
  assert(capi.xxh64("abc") == xxh.xxh64("abc"))
  assert(capi.xxh32("abc", 7) == xxh.xxh32("abc", 7))
  print("lust.xxh and lust.xxhffi agree")
end
