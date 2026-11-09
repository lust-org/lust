/*
 * lust.xxhffi: the C side. The Lua side (xxhffi.lua) calls xxHash directly
 * through the FFI, so there is nothing to wrap. The functions are only ever
 * reached through dlsym (ffi.C / ffi.load), which the linker cannot see:
 * this table references them so that their object file is kept in the
 * module / in `lust`, and (being exported) found by the FFI.
 */
#include "lust.h"
#include "xxhash.h"

LUST_API const void *const lust_xxhffi_exports[] = {
    (const void *)XXH32,
    (const void *)XXH64,
    (const void *)XXH_versionNumber,
};
