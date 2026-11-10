/*
 * lust.sys.time: clocks the Lua standard library lacks.
 *
 *   local time = require "lust.sys.time"
 *   local t0 = time.monotonic()
 *   work()
 *   print(time.monotonic() - t0) --> 0.0123 (seconds)
 *
 * os.clock() is the CPU time of the process and os.time() wall clock
 * seconds, which jump when the clock is set. monotonic() only moves
 * forward, at a steady rate, with nanosecond resolution: the clock for
 * measuring elapsed time.
 */
#define _POSIX_C_SOURCE 200809L /* clock_gettime */

#include <time.h>

#include "lust.h"

/*
 * Seconds since an arbitrary point (the boot), with fractions. A double
 * represents nanoseconds exactly for the first 104 days of uptime and to
 * well below a microsecond for centuries.
 */
static int l_monotonic(lua_State *L) {
  struct timespec ts;
  /* (cannot fail: the clock exists and ts is valid) */
  clock_gettime(CLOCK_MONOTONIC, &ts);
  lua_pushnumber(L, (lua_Number)ts.tv_sec + (lua_Number)ts.tv_nsec * 1e-9);
  return 1;
}

static const luaL_Reg funcs[] = {
    {"monotonic", l_monotonic},
    {NULL, NULL},
};

LUST_API int luaopen_lust_sys_time(lua_State *L) {
  lua_createtable(L, 0, sizeof funcs / sizeof funcs[0] - 1);
  luaL_register(L, NULL, funcs);
  return 1;
}
