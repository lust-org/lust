/*
 * lust.meta: facts about lust itself.
 *
 *   local meta = require "lust.meta"
 *   meta.versions() --> {lust = "0.0.1", luajit = "2.1.1794236686",
 *                        xxhash = "0.8.4", yyjson = "0.13.0",
 *                        pcre2 = "10.49 2026-09-28"}
 *
 * The LuaJIT version is the running one's (jit.version without "LuaJIT "):
 * the .so may be loaded by a luajit other than the one it was built with.
 *
 * The versions of the bundled dependencies come from their headers, at
 * build time (libs/meta/meson.build passes LUST_VERSION and the
 * META_HAVE_<dep> flags of the libraries in the build). So requiring this
 * loads no other library, and the module links none of them.
 */
#include <string.h>

#include "lust.h"

#ifndef LUST_VERSION
#error "LUST_VERSION must be defined by the build system"
#endif

#ifdef META_HAVE_XXHASH
#include "xxhash.h"
#endif
#ifdef META_HAVE_YYJSON
#include "yyjson.h"
#endif
#ifdef META_HAVE_PCRE2
#define PCRE2_CODE_UNIT_WIDTH 8 /* (pcre2.h insists on one) */
#include "pcre2.h"
#endif

#define STRING(a) #a
#define XSTRING(s) STRING(s)

/*
 * Pushes the running LuaJIT's version, from jit.version (the jit library
 * is opened by every luajit, and `lust`'s reports LuaJIT's version there,
 * too) without its "LuaJIT " prefix. Nothing when there is no jit library.
 */
static int push_luajit_version(lua_State *L) {
  static const char prefix[] = "LuaJIT ";
  const char *v;
  lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
  lua_getfield(L, -1, "jit");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }
  lua_getfield(L, -1, "version");
  v = lua_tostring(L, -1);
  if (v == NULL) {
    lua_pop(L, 3);
    return 0;
  }
  if (strncmp(v, prefix, sizeof prefix - 1) == 0)
    lua_pushstring(L, v + sizeof prefix - 1);
  else
    lua_pushvalue(L, -1);
  lua_replace(L, -4);
  lua_pop(L, 2);
  return 1;
}

/* A new table each call: the caller may change it. */
static int l_versions(lua_State *L) {
  lua_createtable(L, 0, 5);
  lua_pushliteral(L, LUST_VERSION);
  lua_setfield(L, -2, "lust");
  if (push_luajit_version(L))
    lua_setfield(L, -2, "luajit");
#ifdef META_HAVE_XXHASH
  lua_pushliteral(L, XSTRING(XXH_VERSION_MAJOR) "." XSTRING(
      XXH_VERSION_MINOR) "." XSTRING(XXH_VERSION_RELEASE));
  lua_setfield(L, -2, "xxhash");
#endif
#ifdef META_HAVE_YYJSON
  lua_pushliteral(L, YYJSON_VERSION_STRING);
  lua_setfield(L, -2, "yyjson");
#endif
#ifdef META_HAVE_PCRE2
  /* The same string as pcre2_config(PCRE2_CONFIG_VERSION), built the same
   * way as in pcre2_config.c: "<major>.<minor>[<prerelease>] <date>" (the
   * Z keeps XSTRING of an empty PCRE2_PRERELEASE from being "") */
  lua_pushstring(L, XSTRING(Z PCRE2_PRERELEASE)[1] == 0
                        ? XSTRING(PCRE2_MAJOR.PCRE2_MINOR PCRE2_DATE)
                        : XSTRING(PCRE2_MAJOR.PCRE2_MINOR) XSTRING(
                              PCRE2_PRERELEASE PCRE2_DATE));
  lua_setfield(L, -2, "pcre2");
#endif
  return 1;
}

static const luaL_Reg funcs[] = {
    {"versions", l_versions},
    {NULL, NULL},
};

LUST_API int luaopen_lust_meta(lua_State *L) {
  lua_createtable(L, 0, sizeof funcs / sizeof funcs[0] - 1);
  luaL_register(L, NULL, funcs);
  return 1;
}
