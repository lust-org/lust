local T = require "t"
local crc = require "lust.crc"

T.run {
  crc32_known_vector = function()
    T.eq(crc.crc32("123456789"), 0xCBF43926)
    T.eq(crc.crc32(""), 0)
  end,

  crc32_incremental = function()
    T.eq(crc.crc32("6789", crc.crc32("12345")), crc.crc32("123456789"))
  end,

  adler32_known_vector = function()
    T.eq(crc.adler32("Wikipedia"), 0x11E60398)
    T.eq(crc.adler32(""), 1)
  end,

  zlib_version = function()
    T.eq(type(crc.zlib_version()), "string")
  end,

  argument_errors = function()
    T.raises(function() crc.crc32() end, "bad argument #1")
    T.raises(function() crc.crc32("x", -1) end, "out of range")
  end,
}
