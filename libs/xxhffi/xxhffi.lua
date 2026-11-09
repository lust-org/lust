-- lust.xxhffi: xxHash (statically linked from vendor/xxhash) through the
-- LuaJIT FFI. Same API as lust.xxh:
--
--   local xxh = require "lust.xxhffi"
--   xxh.xxh32("abc")        --> number (fits a double exactly)
--   xxh.xxh64("abc")        --> "44bc2cf5ad770999" (hex: 64 bit do not fit
--                               a Lua number)
--   xxh.xxh64("abc", seed)
--   xxh.version()           --> "0.8.4"
--
-- This chunk is compiled to bytecode and run by libs/ffi_shim.c, which
-- passes the path of the shared object as `...` (nil inside `lust`).
local ffi = require "ffi"
local bit = require "bit"

local C = (...) and ffi.load((...)) or ffi.C

ffi.cdef [[
uint32_t XXH32(const void *input, size_t length, uint32_t seed);
uint64_t XXH64(const void *input, size_t length, uint64_t seed);
unsigned XXH_versionNumber(void);
]]

local XXH32, XXH64 = C.XXH32, C.XXH64
local tohex = bit.tohex
local format = string.format

-- Argument checking with the same messages as the C API flavour.
local function check_str(s, fname)
  if type(s) == "string" then return s end
  if type(s) == "number" then return tostring(s) end
  error(format("bad argument #1 to '%s' (string expected, got %s)",
    fname, s == nil and "no value" or type(s)), 3)
end

-- Optional seed: a non-negative integer below 2^53.
local function check_seed(n, max, fname)
  if n == nil then return 0 end
  if type(n) ~= "number" then
    error(format("bad argument #2 to '%s' (number expected, got %s)",
      fname, type(n)), 3)
  end
  if not (n >= 0 and n < 9007199254740992 and n % 1 == 0) then
    error(format("bad argument #2 to '%s' (seed must be an integer in [0, 2^53))",
      fname), 3)
  end
  if n >= max then
    error(format("bad argument #2 to '%s' (seed must be below 2^32)", fname), 3)
  end
  return n
end

local M = {}

function M.xxh32(s, seed)
  s = check_str(s, "xxh32")
  return tonumber(XXH32(s, #s, check_seed(seed, 4294967296, "xxh32")))
end

function M.xxh64(s, seed)
  s = check_str(s, "xxh64")
  return tohex(XXH64(s, #s, check_seed(seed, math.huge, "xxh64")), 16)
end

function M.version()
  local v = C.XXH_versionNumber()
  return format("%d.%d.%d", math.floor(v / 10000), math.floor(v / 100) % 100, v % 100)
end

return M
