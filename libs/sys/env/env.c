/*
 * lust.sys.env: the setters for os.getenv.
 *
 *   local env = require "lust.sys.env"
 *   env.set("LANG", "C")   -- os.getenv("LANG") --> "C"
 *   env.unset("LANG")      -- os.getenv("LANG") --> nil
 *
 * They change the environment of the process (setenv/unsetenv), which
 * os.getenv reads and child processes (os.execute, io.popen,
 * sys.proc.run) inherit.
 *
 * glibc never frees a value once set (pointers getenv returned for it
 * stay valid): setting ever new values in a loop grows the process.
 *
 * Invalid names and values raise: they are bugs of the caller, nothing
 * the environment could hold.
 */
#define _POSIX_C_SOURCE 200809L /* setenv, unsetenv */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "lust.h"

/* A name argument: not empty, without "=" and zero bytes (setenv would
 * fail or cut it short there). */
static const char *check_name(lua_State *L, int arg) {
  size_t len;
  const char *s = luaL_checklstring(L, arg, &len);
  if (len == 0 || memchr(s, '=', len) != NULL || strlen(s) != len)
    luaL_argerror(L, arg, "name must be non-empty, without '=' and zero bytes");
  return s;
}

static int l_set(lua_State *L) {
  const char *name = check_name(L, 1);
  size_t len;
  const char *value = luaL_checklstring(L, 2, &len);
  if (strlen(value) != len) luaL_argerror(L, 2, "value contains a zero byte");
  /* (the arguments are valid, so only out of memory is left) */
  if (setenv(name, value, 1) != 0) luaL_error(L, "setenv: %s", strerror(errno));
  return 0;
}

static int l_unset(lua_State *L) {
  /* (cannot fail for a valid name) */
  unsetenv(check_name(L, 1));
  return 0;
}

static const luaL_Reg funcs[] = {
    {"set", l_set},
    {"unset", l_unset},
    {NULL, NULL},
};

LUST_API int luaopen_lust_sys_env(lua_State *L) {
  lua_createtable(L, 0, sizeof funcs / sizeof funcs[0] - 1);
  luaL_register(L, NULL, funcs);
  return 1;
}
