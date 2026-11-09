-- Steady-state microbenchmark of one library flavour.
-- usage: <interpreter> throughput.lua <lib> [rounds]
--   lib: xxh (Lua C API) or xxhffi (static FFI bindings); same API.
--
-- Prints tab separated lines, one per case:
--   <case> <min ns/call> <median ns/call>
-- preceded by a "# cpus <list>" line, so the driver can verify the pinning.
local ffi = require "ffi"

ffi.cdef [[
typedef long time_t;
struct timespec { time_t tv_sec; long tv_nsec; };
int clock_gettime(int clk, struct timespec *ts);
]]
local ts = ffi.new("struct timespec[1]")
local function now_ns() -- CLOCK_MONOTONIC
  ffi.C.clock_gettime(1, ts)
  return tonumber(ts[0].tv_sec) * 1e9 + tonumber(ts[0].tv_nsec)
end

local lib, rounds = arg[1], tonumber(arg[2]) or 15
assert(lib, "usage: throughput.lua <lib> [rounds]")

do
  local f = assert(io.open("/proc/self/status"))
  print("# cpus", f:read("*a"):match("Cpus_allowed_list:%s*(%S+)"))
  f:close()
end

local mod = require("lust." .. lib)

local cases = {}
for _, fn in ipairs { "xxh32", "xxh64" } do
  for _, size in ipairs { 8, 64, 1024, 65536 } do
    cases[#cases + 1] = { name = ("%s/%d"):format(fn, size), fn = mod[fn], data = ("x"):rep(size) }
  end
end

-- Calls per timed round: enough that a round takes ~20ms, found by
-- doubling (this doubles as the JIT warm-up).
local function run(fn, data, n)
  local acc = 0
  for _ = 1, n do
    local r = fn(data)
    -- Consume the result (so nothing can be optimised away), identically
    -- for both flavours: xxh32 returns a number, xxh64 a string.
    if type(r) == "number" then acc = acc + r else acc = acc + #r end
  end
  return acc
end

local function calibrate(c)
  local n = 1024
  while true do
    collectgarbage()
    local t0 = now_ns()
    run(c.fn, c.data, n)
    if now_ns() - t0 > 20e6 then return n end
    n = n * 2
  end
end

for _, c in ipairs(cases) do
  local n = calibrate(c)
  local samples = {}
  for i = 1, rounds do
    collectgarbage()
    local t0 = now_ns()
    run(c.fn, c.data, n)
    samples[i] = (now_ns() - t0) / n
  end
  table.sort(samples)
  print(c.name, ("%.2f"):format(samples[1]), ("%.2f"):format(samples[math.floor((rounds + 1) / 2)]))
end
