local T = require "t"
local random = require "lust.sys.random"

T.run {
  bytes_returns_n_bytes = function()
    for _, n in ipairs { 0, 1, 16, 255, 256, 257, 1024, 1025, 100000 } do
      local s = random.bytes(n)
      T.eq(type(s), "string", "type")
      T.eq(#s, n, "length")
    end
  end,

  bytes_differ_from_call_to_call = function()
    local seen = {}
    for _ = 1, 100 do
      local s = random.bytes(16)
      assert(not seen[s], "repeated 16 bytes")
      seen[s] = true
    end
  end,

  bytes_cover_all_values = function()
    -- (100000 bytes miss one of 256 values with probability ~1e-171)
    local s, seen, n = random.bytes(100000), {}, 0
    for i = 1, #s do
      local b = s:byte(i)
      if not seen[b] then seen[b], n = true, n + 1 end
    end
    T.eq(n, 256, "distinct values")
  end,

  misuse_raises = function()
    T.raises(function() random.bytes() end, "bad argument #1")
    T.raises(function() random.bytes("x") end, "bad argument #1")
    T.raises(function() random.bytes(-1) end, "must be an integer")
    T.raises(function() random.bytes(1.5) end, "must be an integer")
    T.raises(function() random.bytes(2 ^ 31) end, "must be an integer")
  end,
}
