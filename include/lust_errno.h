/*
 * The failure result of lust libraries that wrap system calls:
 *
 *   return lust_push_errno(L, path, errno);
 *   --> nil, "<what>: <strerror(e)>", "<errno name>" (e.g. "ENOENT")
 *
 * Callers often branch on the reason, hence the name as a third value.
 * Not part of lust.h: it needs _GNU_SOURCE (strerrorname_np), which a
 * library has to define before its first include.
 */
#ifndef LUST_ERRNO_H
#define LUST_ERRNO_H

#ifndef _GNU_SOURCE
#error "lust_errno.h needs _GNU_SOURCE (defined before the first include)"
#endif

#include <ctype.h>
#include <string.h>

#include "lust.h"

/*
 * strerror(e) as the end of a message: *c is its first letter, lowercased
 * like all lust error messages start (unless it starts with an acronym),
 * the result the rest. For messages lust_push_errno can't build in one go:
 *
 *   rest = lust_strerror(e, &c);
 *   lua_pushnil(L);
 *   lua_pushfstring(L, "fd %d: %c%s", fd, c, rest);
 *   return lust_errno_result(L, e);
 */
static inline const char *lust_strerror(int e, int *c) {
  const char *msg = strerror(e);
  *c = (unsigned char)msg[0];
  if (isupper(*c) && !isupper((unsigned char)msg[1])) *c = tolower(*c);
  return msg[0] ? msg + 1 : "";
}

/*
 * With nil and the message on top of the stack (pushed in this order, so
 * nothing has to be moved), pushes "<errno name>" and returns 3.
 */
static inline int lust_errno_result(lua_State *L, int e) {
  const char *name = strerrorname_np(e);
  if (name != NULL) lua_pushstring(L, name);
  else lua_pushfstring(L, "E%d", e); /* (not known to this libc) */
  return 3;
}

/*
 * Returns nil, "<what>: <strerror(e)>", "<errno name>". `what` may live on
 * the stack (nothing is popped). What else is on the stack is not
 * returned.
 */
static inline int lust_push_errno(lua_State *L, const char *what, int e) {
  int c;
  const char *rest = lust_strerror(e, &c);
  lua_pushnil(L);
  lua_pushfstring(L, "%s: %c%s", what, c, rest);
  return lust_errno_result(L, e);
}

#endif /* LUST_ERRNO_H */
