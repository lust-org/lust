/*
 * Common header for all lust libraries.
 *
 * Every library is compiled twice from the same sources: once into a
 * loadable module (.so, for the stock luajit) and once into a static
 * archive (linked into the `lust` binary). Both flavours expose the very
 * same `luaopen_lust_<name>` entry point.
 */
#ifndef LUST_H
#define LUST_H

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

/*
 * Everything is built with -fvisibility=hidden, so only symbols tagged
 * with LUST_API (i.e. the luaopen_* entry points) are exported.
 */
#define LUST_API __attribute__((visibility("default")))

#endif /* LUST_H */
