-- lust.sys.*: processes, environment, clocks, randomness, the terminal
-- and signals. Each is a library of its own: require only what you use.
--
--   build/src/lust examples/sys.lua

local proc = require "lust.sys.proc"
local env = require "lust.sys.env"
local time = require "lust.sys.time"
local random = require "lust.sys.random"
local term = require "lust.sys.term"
local signal = require "lust.sys.signal"

-- Elapsed time: monotonic() never jumps, unlike os.time().
local t0 = time.monotonic()

-- Run a program without a shell: the arguments need no quoting.
local status, out, err = proc.run { "ls", "-d", "/", "/nonexistent" }
print("run", status, out, err) --> 2  /\n  ls: cannot access ...
-- Feed it input, change its environment and working directory.
status, out = proc.run({ "sh", "-c", 'sort; echo "$GREETING from $PWD"' },
  { input = "b\na\n", env = { GREETING = "hello" }, cwd = "/" })
io.write(out) --> a  b  hello from /
-- A program that cannot be started is the only failure.
print("run", proc.run { "no-such-program" }) --> nil  no-such-program: ...  ENOENT

-- Set variables for us (os.getenv) and every child started afterwards.
env.set("EXAMPLE", "42")
print("env", os.getenv("EXAMPLE"), select(2, proc.run { "printenv", "EXAMPLE" }))
env.unset("EXAMPLE")

-- Secrets need the kernel's randomness, not math.random().
local token = random.bytes(16):gsub(".", function(c)
  return string.format("%02x", c:byte())
end)
print("token", token)

-- Colors only for a terminal; its size, or why there is none.
print("term", term.isatty(1), term.size(1))

-- Signals: the callback runs in between Lua instructions, so it may do
-- anything, an error it raises stops the program where it was.
local hups = 0
signal.on("SIGHUP", function() hups = hups + 1 end)
os.execute("kill -HUP $PPID") -- (the shell's parent: us)
print("signal", hups) --> 1
signal.on("SIGHUP", nil) -- (back to the default)

-- Sleep in a steady rhythm: deadlines on the monotonic clock, so the time
-- the work takes doesn't add up as drift.
local next = time.monotonic()
for _ = 1, 3 do
  next = next + 0.01
  time.sleep_until(next)
end
time.sleep(0.01)

print("took", string.format("%.3f s", time.monotonic() - t0))
