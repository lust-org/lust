local T = require "t"
local sha256 = require "lust.hash.sha256"

-- "aaa...": 1000000 bytes, the long vector of the SHA-256 test suites
local MILLION_A = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"

-- Raw bytes as lowercase hex
local function tohex(s)
  return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end))
end

T.run {
  known_vectors = function()
    T.eq(sha256.hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
    T.eq(sha256.hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
    T.eq(sha256.hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")
    T.eq(sha256.hex(("a"):rep(1000000)), MILLION_A)
  end,

  embedded_zeros = function()
    T.eq(sha256.hex("a\0b"), "59b271ae1bbcb1d31d41929817f4b16fb439eb4f31520b5ad1d5ce98920a7138")
  end,

  digest_is_raw_hex = function()
    local d = sha256.digest("abc")
    T.eq(#d, 32)
    T.eq(tohex(d), sha256.hex("abc"))
    T.eq(#sha256.digest(""), 32)
  end,

  numbers_are_strings = function()
    T.eq(sha256.hex(123), sha256.hex("123"))
  end,

  hasher_equals_oneshot = function()
    -- every split of a string spanning several 64 byte blocks
    local s = ("0123456789abcdefghijklmnopqrstuvwxyz"):rep(5)
    local want = sha256.hex(s)
    for i = 0, #s do
      local h = sha256.new()
      h:update(s:sub(1, i))
      h:update(s:sub(i + 1))
      T.eq(h:hex(), want, "split at " .. i)
    end
    T.eq(sha256.new():hex(), sha256.hex(""))
  end,

  hasher_long_input_in_pieces = function()
    local h, piece = sha256.new(), ("a"):rep(1000)
    for _ = 1, 1000 do h:update(piece) end
    T.eq(h:hex(), MILLION_A)
  end,

  hasher_update_returns_it = function()
    local h = sha256.new()
    assert(h:update("a") == h)
    T.eq(h:update("b"):update("c"):hex(), sha256.hex("abc"))
  end,

  hasher_result_does_not_finish_it = function()
    local h = sha256.new():update("ab")
    T.eq(h:hex(), sha256.hex("ab"))
    T.eq(h:hex(), sha256.hex("ab"))
    T.eq(h:digest(), sha256.digest("ab"))
    h:update("c")
    T.eq(h:hex(), sha256.hex("abc"))
  end,

  hashers_are_independent = function()
    local a, b = sha256.new(), sha256.new()
    a:update("abc")
    b:update("x")
    T.eq(a:hex(), sha256.hex("abc"))
    T.eq(b:hex(), sha256.hex("x"))
  end,

  argument_errors = function()
    T.raises(function() sha256.hex() end, "bad argument #1")
    T.raises(function() sha256.digest({}) end, "bad argument #1")
    local h = sha256.new()
    T.raises(function() h:update() end, "bad argument #1")
    T.raises(function() h.update({}, "x") end, "bad argument #1")
    T.raises(function() h.hex(io.stdout) end, "bad argument #1")
    T.raises(function() h.digest() end, "bad argument #1")
  end,
}
