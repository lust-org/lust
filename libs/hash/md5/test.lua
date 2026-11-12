local T = require "t"
local md5 = require "lust.hash.md5"

-- "aaa...": 1000000 bytes, the long vector of the MD5 test suites
local MILLION_A = "7707d6ae4e027c70eea2a935c2296f21"

-- Raw bytes as lowercase hex
local function tohex(s)
  return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end))
end

T.run {
  known_vectors = function()
    T.eq(md5.hex(""), "d41d8cd98f00b204e9800998ecf8427e")
    T.eq(md5.hex("abc"), "900150983cd24fb0d6963f7d28e17f72")
    T.eq(md5.hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), "8215ef0796a20bcaaae116d3876c664a")
    T.eq(md5.hex(("a"):rep(1000000)), MILLION_A)
  end,

  embedded_zeros = function()
    T.eq(md5.hex("a\0b"), "70350f6027bce3713f6b76473084309b")
  end,

  digest_is_raw_hex = function()
    local d = md5.digest("abc")
    T.eq(#d, 16)
    T.eq(tohex(d), md5.hex("abc"))
    T.eq(#md5.digest(""), 16)
  end,

  numbers_are_strings = function()
    T.eq(md5.hex(123), md5.hex("123"))
  end,

  hasher_equals_oneshot = function()
    -- every split of a string spanning several 64 byte blocks
    local s = ("0123456789abcdefghijklmnopqrstuvwxyz"):rep(5)
    local want = md5.hex(s)
    for i = 0, #s do
      local h = md5.new()
      h:update(s:sub(1, i))
      h:update(s:sub(i + 1))
      T.eq(h:hex(), want, "split at " .. i)
    end
    T.eq(md5.new():hex(), md5.hex(""))
  end,

  hasher_long_input_in_pieces = function()
    local h, piece = md5.new(), ("a"):rep(1000)
    for _ = 1, 1000 do h:update(piece) end
    T.eq(h:hex(), MILLION_A)
  end,

  hasher_update_returns_it = function()
    local h = md5.new()
    assert(h:update("a") == h)
    T.eq(h:update("b"):update("c"):hex(), md5.hex("abc"))
  end,

  hasher_result_does_not_finish_it = function()
    local h = md5.new():update("ab")
    T.eq(h:hex(), md5.hex("ab"))
    T.eq(h:hex(), md5.hex("ab"))
    T.eq(h:digest(), md5.digest("ab"))
    h:update("c")
    T.eq(h:hex(), md5.hex("abc"))
  end,

  hashers_are_independent = function()
    local a, b = md5.new(), md5.new()
    a:update("abc")
    b:update("x")
    T.eq(a:hex(), md5.hex("abc"))
    T.eq(b:hex(), md5.hex("x"))
  end,

  argument_errors = function()
    T.raises(function() md5.hex() end, "bad argument #1")
    T.raises(function() md5.digest({}) end, "bad argument #1")
    local h = md5.new()
    T.raises(function() h:update() end, "bad argument #1")
    T.raises(function() h.update({}, "x") end, "bad argument #1")
    T.raises(function() h.hex(io.stdout) end, "bad argument #1")
    T.raises(function() h.digest() end, "bad argument #1")
  end,
}
