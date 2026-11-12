local T = require "t"
local sha1 = require "lust.hash.sha1"

-- "aaa...": 1000000 bytes, the long vector of the SHA-1 test suites
local MILLION_A = "34aa973cd4c4daa4f61eeb2bdbad27316534016f"

-- Raw bytes as lowercase hex
local function tohex(s)
  return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end))
end

T.run {
  known_vectors = function()
    T.eq(sha1.hex(""), "da39a3ee5e6b4b0d3255bfef95601890afd80709")
    T.eq(sha1.hex("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d")
    T.eq(sha1.hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), "84983e441c3bd26ebaae4aa1f95129e5e54670f1")
    T.eq(sha1.hex(("a"):rep(1000000)), MILLION_A)
  end,

  embedded_zeros = function()
    T.eq(sha1.hex("a\0b"), "4a3dec2d1f8245280855c42db0ee4239f917fdb8")
  end,

  digest_is_raw_hex = function()
    local d = sha1.digest("abc")
    T.eq(#d, 20)
    T.eq(tohex(d), sha1.hex("abc"))
    T.eq(#sha1.digest(""), 20)
  end,

  numbers_are_strings = function()
    T.eq(sha1.hex(123), sha1.hex("123"))
  end,

  hasher_equals_oneshot = function()
    -- every split of a string spanning several 64 byte blocks
    local s = ("0123456789abcdefghijklmnopqrstuvwxyz"):rep(5)
    local want = sha1.hex(s)
    for i = 0, #s do
      local h = sha1.new()
      h:update(s:sub(1, i))
      h:update(s:sub(i + 1))
      T.eq(h:hex(), want, "split at " .. i)
    end
    T.eq(sha1.new():hex(), sha1.hex(""))
  end,

  hasher_long_input_in_pieces = function()
    local h, piece = sha1.new(), ("a"):rep(1000)
    for _ = 1, 1000 do h:update(piece) end
    T.eq(h:hex(), MILLION_A)
  end,

  hasher_update_returns_it = function()
    local h = sha1.new()
    assert(h:update("a") == h)
    T.eq(h:update("b"):update("c"):hex(), sha1.hex("abc"))
  end,

  hasher_result_does_not_finish_it = function()
    local h = sha1.new():update("ab")
    T.eq(h:hex(), sha1.hex("ab"))
    T.eq(h:hex(), sha1.hex("ab"))
    T.eq(h:digest(), sha1.digest("ab"))
    h:update("c")
    T.eq(h:hex(), sha1.hex("abc"))
  end,

  hashers_are_independent = function()
    local a, b = sha1.new(), sha1.new()
    a:update("abc")
    b:update("x")
    T.eq(a:hex(), sha1.hex("abc"))
    T.eq(b:hex(), sha1.hex("x"))
  end,

  argument_errors = function()
    T.raises(function() sha1.hex() end, "bad argument #1")
    T.raises(function() sha1.digest({}) end, "bad argument #1")
    local h = sha1.new()
    T.raises(function() h:update() end, "bad argument #1")
    T.raises(function() h.update({}, "x") end, "bad argument #1")
    T.raises(function() h.hex(io.stdout) end, "bad argument #1")
    T.raises(function() h.digest() end, "bad argument #1")
  end,
}
