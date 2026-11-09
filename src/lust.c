/*
 * lust: the stock LuaJIT command line interpreter, with the lust libraries
 * statically linked in.
 *
 * The upstream luajit.c is compiled unmodified, except that its call to
 * luaL_openlibs() is redirected (-DluaL_openlibs=lust_openlibs, see
 * src/meson.build) to the function below. It opens the standard libraries
 * and then makes the embedded libraries loadable by `require "lust.<name>"`
 * without touching the filesystem.
 *
 * Startup is kept minimal on purpose, it must not grow with the number of
 * libraries: nothing per library happens at startup. A single searcher
 * (package.loaders[2], right behind package.preload) looks the name up in a
 * static table when `lust.*` is required, and only then does the library
 * initialise.
 */
#include <string.h>

#include "lust.h"
#include "lust_libs.h"

/* Forward declare the entry point of every embedded library. */
#define X(name) extern int luaopen_lust_##name(lua_State *L);
LUST_EMBEDDED_LIBS(X)
#undef X

static const struct {
  const char *name;
  lua_CFunction open;
} embedded[] = {
#define X(name) {"lust." #name, luaopen_lust_##name},
    LUST_EMBEDDED_LIBS(X)
#undef X
    {NULL, NULL},
};

/* Lua 5.1 searcher: returns the loader of an embedded library, else nil
 * (which `require` ignores, no noise in its "module not found" message). */
static int lust_searcher(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  for (int i = 0; embedded[i].name; i++) {
    if (strcmp(name, embedded[i].name) == 0) {
      lua_pushcfunction(L, embedded[i].open);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

/* Append `suffix` to the string field package.<field>. Appended, so user
 * supplied LUA_PATH / LUA_CPATH entries keep precedence. */
static void append_to_package_field(lua_State *L, const char *field,
                                    const char *suffix) {
  lua_getfield(L, -1, field);
  lua_pushstring(L, suffix);
  lua_concat(L, 2);
  lua_setfield(L, -2, field);
}

void lust_openlibs(lua_State *L) {
  luaL_openlibs(L); /* the real one: the rename only applies to luajit.c */

  lua_getglobal(L, "package");

  /* Insert the searcher at position 2 of package.loaders. */
  lua_getfield(L, -1, "loaders");
  for (int i = (int)lua_objlen(L, -1); i >= 2; i--) {
    lua_rawgeti(L, -1, i);
    lua_rawseti(L, -2, i + 1);
  }
  lua_pushcfunction(L, lust_searcher);
  lua_rawseti(L, -2, 2);
  lua_pop(L, 1);

  /* The installed jit.* modules (-jv, -jdump, -bl, ...) ... */
  append_to_package_field(L, "path", ";" LUST_DATADIR "/?.lua");
  /* ... and the libraries that are only available as .so. */
  append_to_package_field(L, "cpath", ";" LUST_CMODDIR "/?.so");

  lua_pop(L, 1); /* package */
}
