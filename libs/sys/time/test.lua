local T = require "t"
local time = require "lust.sys.time"

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
}
