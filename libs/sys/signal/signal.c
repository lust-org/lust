/*
 * lust.sys.signal: Lua callbacks for signals.
 *
 *   local signal = require "lust.sys.signal"
 *   signal.on("SIGTERM", function(name) stop = true end)
 *   signal.on("SIGINT", function() error("interrupted", 0) end)
 *   signal.on("SIGTERM", nil) -- back to what it was before
 *   os.exit(128 + signal.number("SIGINT")) -- (a shell's status for it)
 *
 * A signal arrives at any point of the program, also in the middle of a
 * Lua instruction or of a C function, where no Lua code may run. So the
 * C handler only notes the signal as pending and sets a Lua hook (the
 * way luajit.c handles Ctrl-C). The VM calls the hook before the next
 * instruction it interprets, and the hook calls the callback: callbacks
 * run in between the instructions of the program, like any other Lua
 * function call. An error raised by a callback is raised there.
 *
 * Consequences:
 * - Signals of the same kind that arrive before their callback ran call
 *   it once.
 * - A blocking C function (a read, a wait) isn't interrupted (SA_RESTART):
 *   the callback runs when it returned.
 * - Code compiled by the JIT doesn't look at hooks: a compiled loop that
 *   calls no function the JIT can't compile runs on until it exits. Most
 *   loops do call one (any I/O does).
 * - A hook set with debug.sethook is put aside while ours is due and
 *   restored after.
 *
 * Signal handlers belong to the process, so only one Lua state can have
 * callbacks. Closing it (lua_close) restores the signals it handled.
 */
#define _GNU_SOURCE /* sigabbrev_np */

#include <errno.h>
#include <signal.h>
#include <string.h>

#include "lust.h"
#include "lust_signame.h"

/* Registry keys (their addresses): the callbacks, indexed by signal
 * number, and the guard restoring the signals when the state is closed.
 */
static char callbacks_key, guard_key;

/* ------------------------------------------------------------------ */
/* State shared with the signal handler                               */
/* ------------------------------------------------------------------ */

/* Signals that arrived and whose callback didn't run yet. */
static volatile sig_atomic_t pending[NSIG];

/*
 * The Lua state (a thread of it, anchored in its registry) whose hook the
 * handler sets, NULL while no state has callbacks. The handler only reads
 * it, it is only changed while no signal is handled (see block_all).
 */
static lua_State *owner;

/* The debug hook the handler put aside for ours (see on_signal). */
static lua_Hook saved_hook;
static int saved_mask, saved_count;

/* For every signal with a callback: its handling before (for on(sig,
 * nil) and closing the state). */
static struct sigaction previous[NSIG];
static char handled[NSIG];

static void run_pending(lua_State *L, lua_Debug *ar);

/*
 * The handler. All signals are blocked while it runs (sa_mask), so it is
 * not interrupted by another one. Async-signal-safe: lua_sethook only
 * stores the hook and switches the VM's dispatch table (luajit.c's
 * Ctrl-C handler relies on that too).
 */
static void on_signal(int sig) {
  pending[sig] = 1;
  if (lua_gethook(owner) != run_pending) {
    saved_hook = lua_gethook(owner);
    saved_mask = lua_gethookmask(owner);
    saved_count = lua_gethookcount(owner);
    /* (count 1: before the next instruction, call/return: also when that
     * is a call to a C function) */
    lua_sethook(owner, run_pending, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
  }
}

/* Blocks all signals (so the handler can't run in between), saving the
 * mask in old. */
static void block_all(sigset_t *old) {
  sigset_t all;
  sigfillset(&all);
  sigprocmask(SIG_BLOCK, &all, old);
}

/* ------------------------------------------------------------------ */
/* The hook                                                            */
/* ------------------------------------------------------------------ */

/*
 * Runs the callback of one pending signal (the lowest number). Ours stays
 * the hook while others are pending, so if the callback raises, they
 * still run (before the next instruction where the error is caught).
 * Otherwise the put aside hook is restored first: a signal arriving while
 * the callback runs sets ours again, and LuaJIT doesn't call hooks from
 * within a hook, so its callback runs right after this one.
 */
static void run_pending(lua_State *L, lua_Debug *ar) {
  sigset_t old;
  int sig = 0, more = 0;
  (void)ar;
  block_all(&old);
  for (int s = 1; s < NSIG; s++) {
    if (!pending[s]) continue;
    if (sig == 0) {
      sig = s;
      pending[s] = 0;
    } else {
      more = 1;
      break;
    }
  }
  if (!more) lua_sethook(L, saved_hook, saved_mask, saved_count);
  sigprocmask(SIG_SETMASK, &old, NULL);
  if (sig == 0) return;

  lua_pushlightuserdata(L, &callbacks_key);
  lua_rawget(L, LUA_REGISTRYINDEX);
  lua_rawgeti(L, -1, sig);
  lua_remove(L, -2);
  if (lua_isfunction(L, -1)) {
    lua_pushfstring(L, "SIG%s", lust_sigabbrev(sig));
    lua_call(L, 1, 0);
  } else {
    lua_pop(L, 1); /* (removed meanwhile) */
  }
}

/* ------------------------------------------------------------------ */
/* on                                                                  */
/* ------------------------------------------------------------------ */

/* Stops handling sig: its handling before is back, a pending one is
 * dropped. */
static void unhandle(int sig) {
  sigaction(sig, &previous[sig], NULL);
  handled[sig] = 0;
  pending[sig] = 0;
}

/* __gc of the guard, i.e. the state is closed: restores all signals. */
static int l_guard_gc(lua_State *L) {
  sigset_t old;
  block_all(&old);
  for (int s = 1; s < NSIG; s++) {
    if (handled[s]) unhandle(s);
  }
  if (lua_gethook(L) == run_pending) lua_sethook(L, saved_hook, saved_mask, saved_count);
  owner = NULL;
  sigprocmask(SIG_SETMASK, &old, NULL);
  return 0;
}

/* Signals whose callback could never run: they can't be caught, or they
 * are faults the handler would return into (and fault again). */
static int uncatchable(int sig) {
  switch (sig) {
  case SIGKILL: case SIGSTOP:
  case SIGSEGV: case SIGBUS: case SIGFPE: case SIGILL: case SIGTRAP: case SIGSYS:
  case SIGABRT: /* (abort() kills the process right after the handler) */
    return 1;
  }
  return 0;
}

static int unknown_signal(lua_State *L, int arg) {
  return luaL_argerror(L, arg, lua_pushfstring(L, "unknown signal '%s'", lua_tostring(L, arg)));
}

/* The number of the signal named at arg (one that can be handled; not a
 * realtime one, which has no name for the callback), or raises. */
static int check_signal(lua_State *L, int arg) {
  int sig = lust_signum_named(luaL_checkstring(L, arg));
  if (sig == 0) unknown_signal(L, arg);
  if (uncatchable(sig))
    luaL_argerror(L, arg, lua_pushfstring(L, "%s cannot be handled", lua_tostring(L, arg)));
  return sig;
}

/*
 * Pushes the callbacks table of this state, creating it (and the guard)
 * and making this state the owner on first use. Raises if another state
 * is the owner.
 *
 * Creating them allocates, which may run a finalizer calling on, too
 * (docs/gc-reentrancy.md): everything is allocated first, then checked
 * again and put in place with nothing in between that runs a GC step.
 * Else both calls would make a guard, and the one left over would, when
 * collected, stop handling all signals behind the other's back.
 */
static void push_callbacks(lua_State *L) {
  lua_pushlightuserdata(L, &callbacks_key);
  lua_rawget(L, LUA_REGISTRYINDEX);
  if (!lua_isnil(L, -1)) return;
  lua_pop(L, 1);
  if (owner != NULL) luaL_error(L, "signals are handled by another Lua state");

  /* the guard (its metatable is set last: an unused one must not run its
   * __gc), the metatable, the callbacks table */
  lua_newuserdata(L, 1);
  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, l_guard_gc);
  lua_setfield(L, -2, "__gc");
  /* (the thread is a value in there, which keeps it alive for the
   * handler) */
  lua_createtable(L, NSIG, 1);
  lua_pushthread(L);
  lua_setfield(L, -2, "thread");

  /* From here on, nothing runs a GC step. Made meanwhile? */
  lua_pushlightuserdata(L, &callbacks_key);
  lua_rawget(L, LUA_REGISTRYINDEX);
  if (!lua_isnil(L, -1)) {
    lua_replace(L, -4); /* (theirs instead of our guard) */
    lua_pop(L, 2);
    return;
  }
  lua_pop(L, 1);
  if (owner != NULL) luaL_error(L, "signals are handled by another Lua state");
  /* stack: guard, its metatable, callbacks */
  lua_insert(L, -3);            /* callbacks, guard, metatable */
  lua_setmetatable(L, -2);      /* callbacks, guard */
  lua_pushlightuserdata(L, &guard_key);
  lua_insert(L, -2);
  lua_rawset(L, LUA_REGISTRYINDEX); /* callbacks */
  lua_pushlightuserdata(L, &callbacks_key);
  lua_pushvalue(L, -2);
  lua_rawset(L, LUA_REGISTRYINDEX);
  owner = L;
}

static int l_on(lua_State *L) {
  int sig = check_signal(L, 1);
  int remove = lua_isnoneornil(L, 2);
  if (!remove) luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_settop(L, 2);
  push_callbacks(L);
  lua_pushvalue(L, 2);
  lua_rawseti(L, -2, sig);

  if (remove && handled[sig]) {
    sigset_t old;
    block_all(&old);
    unhandle(sig);
    sigprocmask(SIG_SETMASK, &old, NULL);
  } else if (!remove && !handled[sig]) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sa.sa_flags = SA_RESTART;
    sigfillset(&sa.sa_mask);
    if (sigaction(sig, &sa, &previous[sig]) != 0)
      luaL_error(L, "sigaction: %s", strerror(errno));
    handled[sig] = 1;
  }
  return 0;
}

/* signal.number(sig) -> number: any signal, also the uncatchable and
 * realtime ones. */
static int l_number(lua_State *L) {
  int sig = lust_signum(luaL_checkstring(L, 1));
  if (sig == 0) unknown_signal(L, 1);
  lua_pushinteger(L, sig);
  return 1;
}

static const luaL_Reg funcs[] = {
    {"on", l_on},
    {"number", l_number},
    {NULL, NULL},
};

LUST_API int luaopen_lust_sys_signal(lua_State *L) {
  lua_createtable(L, 0, sizeof funcs / sizeof funcs[0] - 1);
  luaL_register(L, NULL, funcs);
  return 1;
}
