/*
 * lust.crc: checksums backed by the system zlib.
 *
 *   local crc = require "lust.crc"
 *   crc.crc32("123456789")            --> 3421780262
 *   crc.crc32("6789", crc.crc32("12345")) -- incremental
 *   crc.adler32("Wikipedia")          --> 300286872
 *   crc.zlib_version()                --> "1.3.2"
 */
#include <limits.h>
#include <zlib.h>

#include "lust.h"

/* Validated optional "running checksum" argument, `dflt` if absent. */
static uLong opt_checksum(lua_State *L, int arg, uLong dflt) {
  if (lua_isnoneornil(L, arg)) {
    return dflt;
  }
  lua_Number n = luaL_checknumber(L, arg);
  luaL_argcheck(L, n >= 0 && n <= 4294967295.0, arg,
                "checksum out of range [0, 2^32)");
  return (uLong)n;
}

/* zlib takes an `uInt` length, so feed very large strings in chunks. */
typedef uLong (*update_fn)(uLong, const Bytef *, uInt);

static int checksum(lua_State *L, update_fn update, uLong initial) {
  size_t len;
  const char *data = luaL_checklstring(L, 1, &len);
  uLong sum = opt_checksum(L, 2, initial);

  while (len > 0) {
    uInt chunk = len > UINT_MAX ? UINT_MAX : (uInt)len;
    sum = update(sum, (const Bytef *)data, chunk);
    data += chunk;
    len -= chunk;
  }
  lua_pushnumber(L, (lua_Number)sum);
  return 1;
}

static int l_crc32(lua_State *L) {
  return checksum(L, crc32, crc32(0L, Z_NULL, 0));
}

static int l_adler32(lua_State *L) {
  return checksum(L, adler32, adler32(0L, Z_NULL, 0));
}

static int l_zlib_version(lua_State *L) {
  lua_pushstring(L, zlibVersion());
  return 1;
}

static const luaL_Reg funcs[] = {
    {"crc32", l_crc32},
    {"adler32", l_adler32},
    {"zlib_version", l_zlib_version},
    {NULL, NULL},
};

LUST_API int luaopen_lust_crc(lua_State *L) {
  lua_newtable(L);
  luaL_register(L, NULL, funcs);
  return 1;
}
