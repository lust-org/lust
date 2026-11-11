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
#include <errno.h>
#include <string.h>

#include "lust.h"

#ifdef __GLIBC__
#define lust_errno_name strerrorname_np
#else
/*
 * strerrorname_np is glibc only: for other C libraries (musl) the same
 * names from a table of the Linux errno values. The aliases (EWOULDBLOCK,
 * EDEADLOCK, ENOTSUP) are left out, glibc reports the names below for
 * their values, too. NULL for an unknown value.
 */
static inline const char *lust_errno_name(int e) {
  switch (e) {
#define LUST_E(name) case name: return #name;
    LUST_E(EPERM) LUST_E(ENOENT) LUST_E(ESRCH) LUST_E(EINTR) LUST_E(EIO)
    LUST_E(ENXIO) LUST_E(E2BIG) LUST_E(ENOEXEC) LUST_E(EBADF) LUST_E(ECHILD)
    LUST_E(EAGAIN) LUST_E(ENOMEM) LUST_E(EACCES) LUST_E(EFAULT)
    LUST_E(ENOTBLK) LUST_E(EBUSY) LUST_E(EEXIST) LUST_E(EXDEV) LUST_E(ENODEV)
    LUST_E(ENOTDIR) LUST_E(EISDIR) LUST_E(EINVAL) LUST_E(ENFILE)
    LUST_E(EMFILE) LUST_E(ENOTTY) LUST_E(ETXTBSY) LUST_E(EFBIG) LUST_E(ENOSPC)
    LUST_E(ESPIPE) LUST_E(EROFS) LUST_E(EMLINK) LUST_E(EPIPE) LUST_E(EDOM)
    LUST_E(ERANGE) LUST_E(EDEADLK) LUST_E(ENAMETOOLONG) LUST_E(ENOLCK)
    LUST_E(ENOSYS) LUST_E(ENOTEMPTY) LUST_E(ELOOP) LUST_E(ENOMSG)
    LUST_E(EIDRM) LUST_E(ECHRNG) LUST_E(EL2NSYNC) LUST_E(EL3HLT)
    LUST_E(EL3RST) LUST_E(ELNRNG) LUST_E(EUNATCH) LUST_E(ENOCSI)
    LUST_E(EL2HLT) LUST_E(EBADE) LUST_E(EBADR) LUST_E(EXFULL) LUST_E(ENOANO)
    LUST_E(EBADRQC) LUST_E(EBADSLT) LUST_E(EBFONT) LUST_E(ENOSTR)
    LUST_E(ENODATA) LUST_E(ETIME) LUST_E(ENOSR) LUST_E(ENONET) LUST_E(ENOPKG)
    LUST_E(EREMOTE) LUST_E(ENOLINK) LUST_E(EADV) LUST_E(ESRMNT) LUST_E(ECOMM)
    LUST_E(EPROTO) LUST_E(EMULTIHOP) LUST_E(EDOTDOT) LUST_E(EBADMSG)
    LUST_E(EOVERFLOW) LUST_E(ENOTUNIQ) LUST_E(EBADFD) LUST_E(EREMCHG)
    LUST_E(ELIBACC) LUST_E(ELIBBAD) LUST_E(ELIBSCN) LUST_E(ELIBMAX)
    LUST_E(ELIBEXEC) LUST_E(EILSEQ) LUST_E(ERESTART) LUST_E(ESTRPIPE)
    LUST_E(EUSERS) LUST_E(ENOTSOCK) LUST_E(EDESTADDRREQ) LUST_E(EMSGSIZE)
    LUST_E(EPROTOTYPE) LUST_E(ENOPROTOOPT) LUST_E(EPROTONOSUPPORT)
    LUST_E(ESOCKTNOSUPPORT) LUST_E(EOPNOTSUPP) LUST_E(EPFNOSUPPORT)
    LUST_E(EAFNOSUPPORT) LUST_E(EADDRINUSE) LUST_E(EADDRNOTAVAIL)
    LUST_E(ENETDOWN) LUST_E(ENETUNREACH) LUST_E(ENETRESET)
    LUST_E(ECONNABORTED) LUST_E(ECONNRESET) LUST_E(ENOBUFS) LUST_E(EISCONN)
    LUST_E(ENOTCONN) LUST_E(ESHUTDOWN) LUST_E(ETOOMANYREFS) LUST_E(ETIMEDOUT)
    LUST_E(ECONNREFUSED) LUST_E(EHOSTDOWN) LUST_E(EHOSTUNREACH)
    LUST_E(EALREADY) LUST_E(EINPROGRESS) LUST_E(ESTALE) LUST_E(EUCLEAN)
    LUST_E(ENOTNAM) LUST_E(ENAVAIL) LUST_E(EISNAM) LUST_E(EREMOTEIO)
    LUST_E(EDQUOT) LUST_E(ENOMEDIUM) LUST_E(EMEDIUMTYPE) LUST_E(ECANCELED)
    LUST_E(ENOKEY) LUST_E(EKEYEXPIRED) LUST_E(EKEYREVOKED)
    LUST_E(EKEYREJECTED) LUST_E(EOWNERDEAD) LUST_E(ENOTRECOVERABLE)
    LUST_E(ERFKILL) LUST_E(EHWPOISON)
#undef LUST_E
  }
  return NULL;
}
#endif

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
  const char *name = lust_errno_name(e);
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
