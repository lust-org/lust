# lust.xxhffi: the same hashes as lust.xxh, but as static FFI bindings: the
# Lua part (xxhffi.lua) binds xxHash's own functions through the LuaJIT FFI
# and there is no glue code (xxhffi.c only keeps them linked). Compare with
# libs/xxh (Lua C API).
lib_kind = 'ffi'
lib_lua = files('xxhffi.lua')
lib_sources = files('xxhffi.c')
lib_deps = [xxhash_dep]
