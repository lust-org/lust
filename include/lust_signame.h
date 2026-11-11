/*
 * lust_sigabbrev(sig): the name of a signal without its "SIG" prefix
 * ("INT"), NULL for the realtime ones and unknown numbers. What glibc's
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

#endif /* LUST_SIGNAME_H */
