/*
 * Scratch state shared by the calls of a library function, safe against
 * raising and reentrancy (see docs/gc-reentrancy.md): for tiny, hot calls
 * where a userdata per call would cost more than the work.
 *
 * The scratch is a userdata (with a __gc releasing what it owns), an
 * upvalue of the functions using it. Its struct starts with a LustScratch:
 *
 *   typedef struct { LustScratch scratch; int fd; char *buf; } State;
 *
 *   State *st = lust_scratch_acquire(L, lua_upvalueindex(1), push_state);
 *   ... (may raise, may be reentered) ...
 *   lust_scratch_release(&st->scratch);
 *
 * A call finding the scratch busy can't use it: the call it runs within
 * uses it (a __gc finalizer run by an allocation of that call called the
 * function again), or a call that raised left it busy. So it pushes a new
 * one, which replaces the upvalue for the later calls. Every call keeps
 * its scratch on the stack until it returns, so the replaced one lives
 * while it is used, and its __gc releases what it owns once it is
 * garbage. That costs an allocation only after a raise or on reentrancy.
 */
#ifndef LUST_SCRATCH_H
#define LUST_SCRATCH_H

#include "lust.h"

typedef struct {
  int busy; /* a call is using it */
} LustScratch;

/*
 * Pushes the scratch of upvalue `uv` (one value, in any case), marked
 * busy, and returns it. If it is busy, push_new pushes a new one (with
 * busy 0, its other fields initialized), which replaces the upvalue and
 * is pushed and returned instead.
 */
static inline void *lust_scratch_acquire(lua_State *L, int uv,
                                         void *(*push_new)(lua_State *L)) {
  LustScratch *s;
  lua_pushvalue(L, uv);
  s = (LustScratch *)lua_touserdata(L, -1);
  if (s->busy) {
    s = (LustScratch *)push_new(L);
    lua_pushvalue(L, -1);
    lua_replace(L, uv);
    lua_replace(L, -2); /* (the old one stays on the stack of its call) */
  }
  s->busy = 1;
  return s;
}

/* The call is done with it (it stays on the stack until the call returns). */
static inline void lust_scratch_release(LustScratch *s) { s->busy = 0; }

#endif /* LUST_SCRATCH_H */
