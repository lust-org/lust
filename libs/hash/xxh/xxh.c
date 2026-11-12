/*
 * lust.hash.xxh: xxHash (statically linked from vendor/xxhash).
 *
 *   local xxh = require "lust.hash.xxh"
 *   xxh.xxh32("abc")        --> number (fits a double exactly)
 *   xxh.xxh64("abc")        --> "44bc2cf5ad770999" (hex: 64 bit do not fit
 *                               a Lua number)
 *   xxh.xxh64("abc", seed)
 */
#include <stdio.h>

#include "lust.h"
#include "xxhash.h"

/* Optional seed argument: a non-negative integer below 2^53. */
static XXH64_hash_t opt_seed(lua_State *L, int arg) {
  lua_Number n = luaL_optnumber(L, arg, 0);
  luaL_argcheck(L, n >= 0 && n < 9007199254740992.0 && n == (lua_Number)(XXH64_hash_t)n,
                arg, "seed must be an integer in [0, 2^53)");
  return (XXH64_hash_t)n;
}

static int l_xxh32(lua_State *L) {
  size_t len;
  const char *data = luaL_checklstring(L, 1, &len);
  XXH64_hash_t seed = opt_seed(L, 2);
  luaL_argcheck(L, seed <= 0xFFFFFFFFu, 2, "seed must be below 2^32");
  lua_pushnumber(L, (lua_Number)XXH32(data, len, (XXH32_hash_t)seed));
  return 1;
}

static int l_xxh64(lua_State *L) {
  size_t len;
  const char *data = luaL_checklstring(L, 1, &len);
  XXH64_hash_t seed = opt_seed(L, 2);
  char hex[17];
  snprintf(hex, sizeof hex, "%016llx",
           (unsigned long long)XXH64(data, len, seed));
  lua_pushstring(L, hex);
  return 1;
}

static const luaL_Reg funcs[] = {
    {"xxh32", l_xxh32},
    {"xxh64", l_xxh64},
    {NULL, NULL},
};

LUST_API int luaopen_lust_hash_xxh(lua_State *L) {
  lua_newtable(L);
  luaL_register(L, NULL, funcs);
  return 1;
}
