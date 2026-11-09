/*
 * lust: the stock LuaJIT command line interpreter, with all lust libraries
 * statically linked in.
 *
 * The upstream luajit.c is compiled unmodified, except that its call to
 * luaL_openlibs() is redirected (-DluaL_openlibs=lust_openlibs, see
 * src/meson.build) to the function below. It opens the standard libraries
 * and then registers every embedded library in `package.preload`, so
 * `require "lust.<name>"` finds it without touching the filesystem, and
 * a library that is never required costs nothing but its binary size.
 */
#include "lust.h"
#include "lust_libs.h"

/* Forward declare the entry point of every embedded library. */
#define X(name) extern int luaopen_lust_##name(lua_State *L);
LUST_EMBEDDED_LIBS(X)
#undef X

void lust_openlibs(lua_State *L) {
  luaL_openlibs(L); /* the real one: the rename only applies to luajit.c */

  lua_getglobal(L, "package");

  lua_getfield(L, -1, "preload");
#define X(name)                                                                \
  lua_pushcfunction(L, luaopen_lust_##name);                                   \
  lua_setfield(L, -2, "lust." #name);
  LUST_EMBEDDED_LIBS(X)
#undef X
  lua_pop(L, 1);

  /*
   * Make the installed jit.* modules (-jv, -jdump, -bl, ...) loadable.
   * Appended, so user supplied LUA_PATH entries keep precedence.
   */
  lua_getfield(L, -1, "path");
  lua_pushliteral(L, ";" LUST_DATADIR "/?.lua");
  lua_concat(L, 2);
  lua_setfield(L, -2, "path");

  lua_pop(L, 1); /* package */
}
