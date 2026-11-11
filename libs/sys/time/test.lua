local T = require "t"
local time = require "lust.sys.time"

-- Seconds fn() took.
local function took(fn, ...)
  local t0 = time.monotonic()
  fn(...)
  return time.monotonic() - t0
end

T.run {
  monotonic_never_goes_back = function()
    local prev = time.monotonic()
    for _ = 1, 10000 do
      local now = time.monotonic()
      assert(now >= prev, "went back")
      prev = now
    end
  end,

  monotonic_measures_elapsed_time = function()
    -- (sleep 0.2 s in a child: the CPU time of this process barely moves)
    local t0, c0 = time.monotonic(), os.clock()
    os.execute("sleep 0.2")
    local dt = time.monotonic() - t0
    assert(dt >= 0.2 and dt < 5, "elapsed " .. dt)
    assert(os.clock() - c0 < 0.1, "os.clock moved like monotonic")
  end,

  monotonic_has_sub_millisecond_resolution = function()
    -- (consecutive calls are far less than a millisecond apart)
    local a, b = time.monotonic(), time.monotonic()
    while b == a do b = time.monotonic() end
    assert(b - a < 1e-3, "step " .. (b - a))
  end,

  sleep_waits = function()
    local dt = took(time.sleep, 0.05)
    assert(dt >= 0.05 and dt < 2, "slept " .. dt)
    dt = took(time.sleep, 0.0015) -- (fractions of milliseconds, too)
    assert(dt >= 0.0015 and dt < 2, "slept " .. dt)
  end,

  sleep_of_nothing_returns_at_once = function()
    for _, s in ipairs { 0, -1, -1e300 } do
      local dt = took(time.sleep, s)
      assert(dt < 0.05, "sleep(" .. s .. ") took " .. dt)
    end
  end,

  sleep_until_wakes_at_the_deadline = function()
    local deadline = time.monotonic() + 0.05
    time.sleep_until(deadline)
    local late = time.monotonic() - deadline
    assert(late >= 0 and late < 2, "late " .. late)
  end,

  sleep_until_a_past_deadline_returns_at_once = function()
    for _, d in ipairs { time.monotonic(), time.monotonic() - 10, 0, -1e300 } do
      local dt = took(time.sleep_until, d)
      assert(dt < 0.05, "sleep_until(" .. d .. ") took " .. dt)
    end
  end,

  sleep_until_keeps_a_rhythm = function()
    -- Work of 10 ms in rounds of 20 ms: the deadlines don't move with it.
    local t0 = time.monotonic()
    local next = t0
    for _ = 1, 5 do
      time.sleep(0.01)
      next = next + 0.02
      time.sleep_until(next)
    end
    local dt = time.monotonic() - t0
    assert(dt >= 0.1 and dt < 0.1 + 1, "5 rounds took " .. dt)
  end,

  a_signal_doesnt_cut_a_sleep_short = function()
    local signal = require "lust.sys.signal"
    local got = 0
    signal.on("SIGUSR1", function() got = got + 1 end)
    -- (in the background, so os.execute returns at once; $PPID: us)
    os.execute("(sleep 0.05; kill -USR1 $PPID) >/dev/null 2>&1 &")
    local dt = took(time.sleep, 0.3)
    local n = got -- (the callback ran right after the sleep, if at all)
    signal.on("SIGUSR1", nil)
    assert(dt >= 0.3, "slept " .. dt)
    T.eq(n, 1, "callbacks")
  end,

  misuse_raises = function()
    T.raises(function() time.sleep() end, "bad argument #1 to 'sleep'")
    T.raises(function() time.sleep("x") end, "bad argument #1 to 'sleep'")
    T.raises(function() time.sleep(0 / 0) end, "seconds must be a finite number")
    T.raises(function() time.sleep(math.huge) end, "seconds must be a finite number")
    T.raises(function() time.sleep_until() end, "bad argument #1 to 'sleep_until'")
    T.raises(function() time.sleep_until(0 / 0) end, "deadline must be a finite number")
    T.raises(function() time.sleep_until(-math.huge) end, "deadline must be a finite number")
  end,
}
