/*
 * Loading the Lua part of a library (lib_lua). It is embedded twice:
 *
 *   - as LuaJIT bytecode, luaJIT_BC_lust_<id>[] (`luajit -b`, the header
 *     LUST_LUA_BC_H / LUST_FFI_BC_H): loads fastest, but only into a
 *     LuaJIT with the bytecode format of the one that built it,
 *   - as source, lust_lua_src_<id>[] (tools/embed-lua-source.py, the
 *     header LUST_LUA_SRC_H / LUST_FFI_SRC_H): for everything else, PUC
 *     Lua 5.1 or a stock LuaJIT whose bytecode format differs.
 *
 * Those reject the bytecode by its header, before reading anything else
 * of it, so trying it first is safe.
 */
#ifndef LUST_LUA_H
#define LUST_LUA_H

#include <stddef.h>

#include "lust.h"

/* Pushes the chunk, raises if neither form loads (out of memory, or the
 * source uses syntax the running VM lacks). */
static inline void lust_load_lua(lua_State *L, const unsigned char *bc,
                                 size_t bc_size, const unsigned char *src,
                                 size_t src_size, const char *chunkname) {
  if (luaL_loadbuffer(L, (const char *)bc, bc_size, chunkname) == 0) return;
  lua_pop(L, 1); /* (the reason the bytecode was rejected) */
  if (luaL_loadbuffer(L, (const char *)src, src_size, chunkname) != 0)
    lua_error(L);
}

#endif
