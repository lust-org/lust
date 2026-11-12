/*
 * The Lua API of the Mbed TLS hashes (lust.hash.md5, lust.hash.sha1,
 * lust.hash.sha256), shared by them. Each module is one .c file which
 * defines, before including this header:
 *
 *   HASH_NAME   the algorithm, "sha256" (names the hasher's metatable)
 *   HASH_SIZE   the size of a digest in bytes
 *   HashCtx     the type of the algorithm's context (a plain struct
 *               without pointers: a copy by assignment is a clone, which
 *               is all mbedtls_<alg>_clone does)
 *   static int hash_start(HashCtx *), hash_update(HashCtx *, const
 *   unsigned char *, size_t), hash_finish(HashCtx *, unsigned char *out):
 *               init + start, add data, write the digest; each returns
 *               the mbedtls status (0: success)
 *
 * and gets hash_open, which pushes the module (its luaopen_ returns it).
 * Every module thus has its own copy of this code (calling its algorithm
 * directly), and links only the code of its own algorithm.
 *
 * API of each module:
 *   hex(str), digest(str)       one-shot, the digest as lowercase hex or
 *                               raw bytes
 *   new()                       a hasher, for data that comes in pieces:
 *   h:update(str)               adds str, returns h
 *   h:hex(), h:digest()         the digest of everything added so far;
 *                               h is not finished by it, more may be added
 *
 * A context needs no cleanup (mbedtls_<alg>_free only wipes it), so a
 * hasher is a userdata without __gc. Nothing here runs Lua code or a GC
 * step while it works on a context (docs/gc-reentrancy.md): only the
 * argument checks (before) and pushing the result (after, from a copy on
 * the C stack) do.
 */
#ifndef LUST_MBEDTLS_HASH_H
#define LUST_MBEDTLS_HASH_H

#include "lust.h"

#define HASHER_MT "lust.hash." HASH_NAME ".hasher"

/* The builtin hashes only fail on misuse (a NULL context), which this
 * code doesn't commit: a failure is a bug, so it raises. */
static void hash_check(lua_State *L, int ret) {
  if (ret != 0)
    luaL_error(L, "mbedtls error %d", ret);
}

/* Pushes digest d as lowercase hex (hex != 0) or as raw bytes. */
static void hash_push(lua_State *L, const unsigned char *d, int hex) {
  static const char digits[] = "0123456789abcdef";
  char buf[2 * HASH_SIZE];
  int i;
  if (!hex) {
    lua_pushlstring(L, (const char *)d, HASH_SIZE);
    return;
  }
  for (i = 0; i < HASH_SIZE; i++) {
    buf[2 * i] = digits[d[i] >> 4];
    buf[2 * i + 1] = digits[d[i] & 15];
  }
  lua_pushlstring(L, buf, sizeof buf);
}

/* hex(str) / digest(str) */
static int hash_oneshot(lua_State *L, int hex) {
  size_t len;
  const char *s = luaL_checklstring(L, 1, &len);
  HashCtx ctx;
  unsigned char d[HASH_SIZE];
  hash_check(L, hash_start(&ctx));
  hash_check(L, hash_update(&ctx, (const unsigned char *)s, len));
  hash_check(L, hash_finish(&ctx, d));
  hash_push(L, d, hex);
  return 1;
}

static int l_hex(lua_State *L) { return hash_oneshot(L, 1); }
static int l_digest(lua_State *L) { return hash_oneshot(L, 0); }

/* new(): hasher */
static int l_new(lua_State *L) {
  HashCtx *c = (HashCtx *)lua_newuserdata(L, sizeof *c);
  hash_check(L, hash_start(c));
  luaL_getmetatable(L, HASHER_MT);
  lua_setmetatable(L, -2);
  return 1;
}

/* h:update(str): h */
static int l_update(lua_State *L) {
  HashCtx *c = (HashCtx *)luaL_checkudata(L, 1, HASHER_MT);
  size_t len;
  const char *s = luaL_checklstring(L, 2, &len);
  hash_check(L, hash_update(c, (const unsigned char *)s, len));
  lua_settop(L, 1);
  return 1;
}

/* h:hex() / h:digest(): finishes a copy, so h can go on */
static int hash_result(lua_State *L, int hex) {
  HashCtx copy = *(HashCtx *)luaL_checkudata(L, 1, HASHER_MT);
  unsigned char d[HASH_SIZE];
  hash_check(L, hash_finish(&copy, d));
  hash_push(L, d, hex);
  return 1;
}

static int l_hasher_hex(lua_State *L) { return hash_result(L, 1); }
static int l_hasher_digest(lua_State *L) { return hash_result(L, 0); }

static const luaL_Reg hash_funcs[] = {
    {"hex", l_hex},
    {"digest", l_digest},
    {"new", l_new},
    {NULL, NULL},
};

static const luaL_Reg hasher_methods[] = {
    {"update", l_update},
    {"hex", l_hasher_hex},
    {"digest", l_hasher_digest},
    {NULL, NULL},
};

static int hash_open(lua_State *L) {
  /* the hashers' metatable: methods through __index */
  luaL_newmetatable(L, HASHER_MT);
  lua_newtable(L);
  luaL_register(L, NULL, hasher_methods);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);
  lua_newtable(L);
  luaL_register(L, NULL, hash_funcs);
  return 1;
}

#endif
