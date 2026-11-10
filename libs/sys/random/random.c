/*
 * lust.sys.random: cryptographically secure random bytes.
 *
 *   local random = require "lust.sys.random"
 *   random.bytes(16) --> 16 bytes for a key, a token, a nonce ...
 *
 * math.random() is a fast PRNG, predictable from its outputs: fine for
 * simulations, not for secrets. bytes() comes from the kernel's CSPRNG
 * via getrandom(2), which needs no file descriptor (unlike reading
 * /dev/urandom) and blocks only until the kernel has gathered enough
 * entropy once after boot.
 *
 * It never returns nil: a failing getrandom (a kernel older than 3.17)
 * raises, a caller could do nothing better than fail anyway.
 */
#define _GNU_SOURCE /* getrandom */

#include <errno.h>
#include <string.h>
#include <sys/random.h>

#include "lust.h"

/* Requests up to this size use a buffer on the C stack, larger ones one
 * from Lua (the common requests are keys and tokens, a few dozen bytes). */
#define STACK_BUF 1024

/* The largest n: LuaJIT's limit for the length of a string is just below. */
#define MAX_N 0x7fffff00

/* Fills buf with n random bytes, or raises. A request over 256 bytes may
 * be cut short (by a signal), so loop. */
static void fill(lua_State *L, char *buf, size_t n) {
  while (n > 0) {
    ssize_t got = getrandom(buf, n, 0);
    if (got < 0) {
      if (errno == EINTR) continue;
      luaL_error(L, "getrandom: %s", strerror(errno));
    }
    buf += got;
    n -= (size_t)got;
  }
}

static int l_bytes(lua_State *L) {
  lua_Number v = luaL_checknumber(L, 1);
  size_t n;
  if (!(v >= 0 && v <= MAX_N && v == (lua_Number)(size_t)v))
    luaL_argerror(L, 1, "must be an integer in [0, 2147483392]");
  n = (size_t)v;
  if (n <= STACK_BUF) {
    char buf[STACK_BUF];
    fill(L, buf, n);
    lua_pushlstring(L, buf, n);
  } else {
    /* (a userdata: freed by the GC, also if pushing the string raises) */
    char *buf = lua_newuserdata(L, n);
    fill(L, buf, n);
    lua_pushlstring(L, buf, n);
  }
  return 1;
}

static const luaL_Reg funcs[] = {
    {"bytes", l_bytes},
    {NULL, NULL},
};

LUST_API int luaopen_lust_sys_random(lua_State *L) {
  lua_createtable(L, 0, sizeof funcs / sizeof funcs[0] - 1);
  luaL_register(L, NULL, funcs);
  return 1;
}
