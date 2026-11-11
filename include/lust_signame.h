/*
 * lust_sigabbrev(sig): the name of a signal without its "SIG" prefix
 * ("INT"), NULL for the realtime ones and unknown numbers; lust_signum and
 * friends the other way round. What glibc's
 * sigabbrev_np returns (for which a library needs _GNU_SOURCE defined
 * before its first include), with the same names on other C libraries.
 */
#ifndef LUST_SIGNAME_H
#define LUST_SIGNAME_H

#include <signal.h>
#include <string.h>

#ifdef __GLIBC__
#define lust_sigabbrev sigabbrev_np
#else
/* sigabbrev_np is glibc only: the Linux signals, the aliases (SIGIO,
 * SIGIOT, SIGCLD, ...) under the names glibc reports for them. */
static inline const char *lust_sigabbrev(int sig) {
  switch (sig) {
#define LUST_S(name) case SIG##name: return #name;
    LUST_S(HUP) LUST_S(INT) LUST_S(QUIT) LUST_S(ILL) LUST_S(TRAP) LUST_S(ABRT)
    LUST_S(BUS) LUST_S(FPE) LUST_S(KILL) LUST_S(USR1) LUST_S(SEGV) LUST_S(USR2)
    LUST_S(PIPE) LUST_S(ALRM) LUST_S(TERM) LUST_S(STKFLT) LUST_S(CHLD)
    LUST_S(CONT) LUST_S(STOP) LUST_S(TSTP) LUST_S(TTIN) LUST_S(TTOU) LUST_S(URG)
    LUST_S(XCPU) LUST_S(XFSZ) LUST_S(VTALRM) LUST_S(PROF) LUST_S(WINCH)
    LUST_S(POLL) LUST_S(PWR) LUST_S(SYS)
#undef LUST_S
  }
  return NULL;
}
#endif

/* The number of the (not realtime) signal named name ("SIGINT", ...), 0
 * if there is none of that name. */
static inline int lust_signum_named(const char *name) {
  if (strncmp(name, "SIG", 3) != 0) return 0;
  for (int s = 1; s < NSIG; s++) {
    const char *abbrev = lust_sigabbrev(s);
    if (abbrev != NULL && strcmp(name + 3, abbrev) == 0) return s;
  }
  return 0;
}

/*
 * The number of the realtime signal named name, 0 if there is none. They
 * have no names of their own: "SIGRTMIN+<n>", as lust.sys.proc reports
 * them, decimal n without leading zeros.
 */
static inline int lust_signum_realtime(const char *name) {
  const char *d = name + 9;
  int n = 0;
  if (strncmp(name, "SIGRTMIN+", 9) != 0) return 0;
  if (*d == '\0' || (d[0] == '0' && d[1] != '\0')) return 0;
  for (; *d != '\0'; d++) {
    if (*d < '0' || *d > '9') return 0;
    n = n * 10 + (*d - '0');
    if (n > SIGRTMAX - SIGRTMIN) return 0;
  }
  return SIGRTMIN + n;
}

/* Any signal by name (lust_signum_named, then lust_signum_realtime). */
static inline int lust_signum(const char *name) {
  int sig = lust_signum_named(name);
  return sig != 0 ? sig : lust_signum_realtime(name);
}

#endif /* LUST_SIGNAME_H */
