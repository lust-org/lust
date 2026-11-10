local T = require "t"
local xxh = require "lust.xxh"

T.run {
  xxh32_known_vectors = function()
    T.eq(xxh.xxh32(""), 0x02CC5D05)
    T.eq(xxh.xxh32("", 1), 0x0B2CB792)
  end,

  xxh64_known_vectors = function()
    T.eq(xxh.xxh64(""), "ef46db3751d8e999")
    T.eq(xxh.xxh64("abc"), "44bc2cf5ad770999")
  end,

  xxh64_seed_changes_result = function()
    assert(xxh.xxh64("abc", 1) ~= xxh.xxh64("abc"))
  end,

  argument_errors = function()
    T.raises(function() xxh.xxh64() end, "bad argument #1")
    T.raises(function() xxh.xxh64("x", -1) end, "seed must be")
    T.raises(function() xxh.xxh32("x", 2^32) end, "seed must be")
  end,
}
