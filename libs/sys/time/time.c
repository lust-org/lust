/*
 * lust.sys.time: clocks the Lua standard library lacks.
 *
 *   local time = require "lust.sys.time"
 *   local t0 = time.monotonic()
 *   work()
 *   print(time.monotonic() - t0) --> 0.0123 (seconds)
 *   time.sleep(0.25)
 *   local next = time.monotonic()
 *   while true do  -- once a second, however long sample() takes
 *     sample()
 *     next = next + 1
 *     time.sleep_until(next)
 *   end
 *
 * os.clock() is the CPU time of the process and os.time() wall clock
 * seconds, which jump when the clock is set. monotonic() only moves
 * forward, at a steady rate, with nanosecond resolution: the clock for
 * measuring elapsed time, and the one both sleeps measure by.
 *
 * Both sleep until an absolute point of that clock (clock_nanosleep with
 * TIMER_ABSTIME): a signal interrupting the sleep (EINTR) only means
 * asking again for the same point, nothing is lost or added. Lua has no
 * sleep at all; os.execute("sleep 1") starts two programs.
 */
#define _POSIX_C_SOURCE 200809L /* clock_gettime, clock_nanosleep */

#include <errno.h>
#include <math.h> /* isfinite (a macro, no libm) */
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

/*
 * Seconds as a timespec, rounded up to whole nanoseconds (a sleep must
 * not end before the point asked for). Negative: 0. Beyond 2^62 seconds
 * (a time_t can't hold much more; a hundred billion years): 2^62.
 */
static struct timespec to_timespec(lua_Number t) {
  struct timespec ts;
  lua_Number ns;
  if (!(t > 0)) t = 0;
  if (t > 0x1p62) t = 0x1p62;
  /* (casts truncate, i.e. floor t >= 0: no floor/ceil, no libm) */
  ts.tv_sec = (time_t)t;
  ns = (t - (lua_Number)ts.tv_sec) * 1e9;
  ts.tv_nsec = (long)ns;
  if ((lua_Number)ts.tv_nsec < ns) ts.tv_nsec++;
  if (ts.tv_nsec >= 1000000000L) { /* (rounded up from just below 1 s) */
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000L;
  }
  return ts;
}

/* Sleeps until the monotonic clock reaches ts (at once if it has). */
static void sleep_until(const struct timespec *ts) {
  /* (returns the error rather than setting errno; EINTR is the only one
   * a valid ts can get) */
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ts, NULL) == EINTR) {}
}

/* A finite number argument (NaN or an infinite sleep: a bug, there'd be
 * no way to wake up). */
static lua_Number check_finite(lua_State *L, int arg, const char *what) {
  lua_Number t = luaL_checknumber(L, arg);
  if (!isfinite(t)) luaL_argerror(L, arg, lua_pushfstring(L, "%s must be a finite number", what));
  return t;
}

/* time.sleep(seconds): seconds <= 0 returns at once. */
static int l_sleep(lua_State *L) {
  lua_Number t = check_finite(L, 1, "seconds");
  struct timespec now, rel, until;
  if (!(t > 0)) return 0;
  /* now + t in timespec arithmetic, not doubles: no rounding of now */
  rel = to_timespec(t);
  clock_gettime(CLOCK_MONOTONIC, &now);
  until.tv_sec = now.tv_sec + rel.tv_sec;
  until.tv_nsec = now.tv_nsec + rel.tv_nsec;
  if (until.tv_nsec >= 1000000000L) {
    until.tv_sec++;
    until.tv_nsec -= 1000000000L;
  }
  sleep_until(&until);
  return 0;
}

/* time.sleep_until(deadline): deadline in monotonic() seconds. */
static int l_sleep_until(lua_State *L) {
  struct timespec until = to_timespec(check_finite(L, 1, "deadline"));
  sleep_until(&until);
  return 0;
}

static const luaL_Reg funcs[] = {
    {"monotonic", l_monotonic},
    {"sleep", l_sleep},
    {"sleep_until", l_sleep_until},
    {NULL, NULL},
};

LUST_API int luaopen_lust_sys_time(lua_State *L) {
  lua_createtable(L, 0, sizeof funcs / sizeof funcs[0] - 1);
  luaL_register(L, NULL, funcs);
  return 1;
}
