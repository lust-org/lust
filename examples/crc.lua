-- lust.crc: zlib checksums. It links the system zlib dynamically, so it is
-- not built into `lust` (that would make every start map libz); `lust` loads
-- its .so on demand from the install directory. When running from the build
-- tree, point LUA_CPATH to it:
--
--   LUA_CPATH="$PWD/build/libs/?.so" build/src/lust examples/crc.lua

local ok, crc = pcall(require, "lust.crc")
if not ok then
  io.stderr:write(
    "lust.crc is not installed. From the build tree run:\n",
    '  LUA_CPATH="$PWD/build/libs/?.so" build/src/lust examples/crc.lua\n\n',
    tostring(crc), "\n")
  os.exit(1)
end

print("zlib version:", crc.zlib_version())

print(string.format("crc32(\"123456789\")   = %08x", crc.crc32("123456789")))
print(string.format("adler32(\"Wikipedia\") = %08x", crc.adler32("Wikipedia")))

-- Checksums can be computed incrementally by passing the previous value.
local partial = crc.crc32("12345")
print(string.format("incremental crc32     = %08x", crc.crc32("6789", partial)))
