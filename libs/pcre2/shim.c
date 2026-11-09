/*
 * lust.pcre2: the C side. A thin ABI over PCRE2 (vendor/pcre2, 8 bit code
 * units, linked statically). It knows nothing about Lua: pcre2.lua binds
 * these functions through the LuaJIT FFI.
 *
 * Why a shim at all: PCRE2 names every function after its code unit width
 * through macros, hands out opaque contexts that have to be wired up, and
 * reports offsets as PCRE2_SIZE (a 64 bit integer, which is awkward in
 * LuaJIT). The shim bundles a compiled pattern with its match data and
 * limits, and everything that crosses the boundary is a plain C type:
 *   - a regexp is an opaque `lust_pcre2 *`,
 *   - offsets are doubles (exact up to 2^53, and directly usable as Lua
 *     numbers, unlike 64 bit cdata integers),
 *   - failures are reported through return values.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2.h"

#include "lust.h"

/* Keep in sync with pcre2.lua (the FLAGS list). */
enum {
  F_CASELESS = 1u << 0,
  F_MULTILINE = 1u << 1,
  F_DOTALL = 1u << 2,
  F_EXTENDED = 1u << 3,
  F_EXTENDED_MORE = 1u << 4,
  F_UNGREEDY = 1u << 5,
  F_DOLLAR_ENDONLY = 1u << 6,
  F_NO_AUTO_CAPTURE = 1u << 7,
  F_UCP = 1u << 8,
  F_LITERAL = 1u << 9,
  F_ANCHORED = 1u << 10,
  F_DUPNAMES = 1u << 11,
  F_MATCH_UNSET_BACKREF = 1u << 12,
  F_BYTES = 1u << 13, /* not UTF-8: every byte is a character */
};

/* Flags of lust_pcre2_match, keep in sync with pcre2.lua. */
enum {
  M_ANCHORED = 1u << 0,    /* match has to start at startpos */
  M_ENDANCHORED = 1u << 1, /* match has to end at the end of the subject */
  M_NOTEMPTY_ATSTART = 1u << 2, /* no empty match at startpos */
  M_NO_UTF_CHECK = 1u << 3, /* the subject is known to be valid UTF-8 */
};

/*
 * Memory PCRE2 may use for backtracking frames per match, in KiB. Its own
 * default (20 GB) is no protection at all. The match limit (10 million
 * calls of its matching function by default) bounds the time instead.
 */
#define DEFAULT_HEAP_LIMIT_KIB (128u * 1024u)

typedef struct lust_pcre2 {
  pcre2_code *code;
  pcre2_match_data *md;
  pcre2_match_context *mctx;
  uint32_t ngroups;
} lust_pcre2;

/* Write the message of a PCRE2 error code into the caller's buffer
 * (truncated, always terminated). */
static void error_text(int code, char *buf, size_t bufsz) {
  if (buf == NULL || bufsz == 0) return;
  if (pcre2_get_error_message(code, (PCRE2_UCHAR *)buf, bufsz) < 0)
    snprintf(buf, bufsz, "error %d", code);
}

/*
 * Compile `pat`. Returns NULL on failure, with a message in `err`.
 * The limits (0: PCRE2's defaults, except for the heap, see above):
 * `match_limit` calls of the matching function per match, `depth_limit`
 * depth of nested backtracking, `max_mem` bytes of heap for the
 * backtracking frames.
 */
LUST_API lust_pcre2 *lust_pcre2_new(const char *pat, size_t len,
                                    unsigned flags, double match_limit,
                                    double depth_limit, double max_mem,
                                    char *err, size_t errsz) {
  uint32_t o = 0;
  int code;
  PCRE2_SIZE erroff;
  lust_pcre2 *r;

  if (!(flags & F_BYTES)) o |= PCRE2_UTF;
  if (flags & F_CASELESS) o |= PCRE2_CASELESS;
  if (flags & F_MULTILINE) o |= PCRE2_MULTILINE;
  if (flags & F_DOTALL) o |= PCRE2_DOTALL;
  if (flags & F_EXTENDED) o |= PCRE2_EXTENDED;
  if (flags & F_EXTENDED_MORE) o |= PCRE2_EXTENDED_MORE;
  if (flags & F_UNGREEDY) o |= PCRE2_UNGREEDY;
  if (flags & F_DOLLAR_ENDONLY) o |= PCRE2_DOLLAR_ENDONLY;
  if (flags & F_NO_AUTO_CAPTURE) o |= PCRE2_NO_AUTO_CAPTURE;
  if (flags & F_UCP) o |= PCRE2_UCP;
  if (flags & F_LITERAL) o |= PCRE2_LITERAL;
  if (flags & F_ANCHORED) o |= PCRE2_ANCHORED;
  if (flags & F_DUPNAMES) o |= PCRE2_DUPNAMES;
  if (flags & F_MATCH_UNSET_BACKREF) o |= PCRE2_MATCH_UNSET_BACKREF;

  r = calloc(1, sizeof *r);
  if (r == NULL) {
    snprintf(err, errsz, "out of memory");
    return NULL;
  }

  r->code = pcre2_compile((PCRE2_SPTR)pat, len, o, &code, &erroff, NULL);
  if (r->code == NULL) {
    char msg[200];
    error_text(code, msg, sizeof msg);
    if (err != NULL && errsz > 0)
      snprintf(err, errsz, "%s (at offset %lu)", msg, (unsigned long)erroff);
    free(r);
    return NULL;
  }

  r->mctx = pcre2_match_context_create(NULL);
  r->md = pcre2_match_data_create_from_pattern(r->code, NULL);
  if (r->mctx == NULL || r->md == NULL) {
    snprintf(err, errsz, "out of memory");
    pcre2_match_context_free(r->mctx);
    pcre2_match_data_free(r->md);
    pcre2_code_free(r->code);
    free(r);
    return NULL;
  }
  if (match_limit >= 1 && match_limit < 4294967295.0)
    pcre2_set_match_limit(r->mctx, (uint32_t)match_limit);
  if (depth_limit >= 1 && depth_limit < 4294967295.0)
    pcre2_set_depth_limit(r->mctx, (uint32_t)depth_limit);
  if (max_mem >= 1) {
    double kib = (max_mem + 1023.0) / 1024.0;
    pcre2_set_heap_limit(r->mctx, kib < 4294967295.0 ? (uint32_t)kib
                                                     : 4294967295u);
  } else {
    pcre2_set_heap_limit(r->mctx, DEFAULT_HEAP_LIMIT_KIB);
  }

  pcre2_pattern_info(r->code, PCRE2_INFO_CAPTURECOUNT, &r->ngroups);
  return r;
}

LUST_API void lust_pcre2_free(lust_pcre2 *r) {
  if (r == NULL) return;
  pcre2_match_data_free(r->md);
  pcre2_match_context_free(r->mctx);
  pcre2_code_free(r->code);
  free(r);
}

/* Number of capturing groups (not counting the whole match). */
LUST_API int lust_pcre2_ngroups(const lust_pcre2 *r) { return r->ngroups; }

/*
 * Names of the capturing groups, as a buffer holding one NUL terminated
 * name per group, in group order (1 .. ngroups); "" for a group without a
 * name. Returns the size the buffer needs; the buffer is filled only if it
 * is large enough, so call it once with (NULL, 0) to find out.
 */
LUST_API size_t lust_pcre2_group_names(const lust_pcre2 *r, char *buf,
                                       size_t bufsz) {
  uint32_t count = 0, entsize = 0, i, g;
  const unsigned char *table = NULL;
  size_t need = 0;

  pcre2_pattern_info(r->code, PCRE2_INFO_NAMECOUNT, &count);
  pcre2_pattern_info(r->code, PCRE2_INFO_NAMEENTRYSIZE, &entsize);
  pcre2_pattern_info(r->code, PCRE2_INFO_NAMETABLE, &table);

  for (g = 1; g <= r->ngroups; g++) {
    /* The name table is sorted by name; each entry is the group number
     * (two bytes, most significant first) and the NUL terminated name.
     * With duplicate names the first entry of a number wins. */
    const char *name = "";
    size_t n;
    for (i = 0; i < count; i++) {
      const unsigned char *e = table + (size_t)i * entsize;
      if ((uint32_t)(e[0] << 8 | e[1]) == g) {
        name = (const char *)e + 2;
        break;
      }
    }
    n = strlen(name);
    if (buf != NULL && need + n + 1 <= bufsz) memcpy(buf + need, name, n + 1);
    need += n + 1;
  }
  return need;
}

/* Message for a (negative) error code returned by lust_pcre2_match. */
LUST_API void lust_pcre2_error_message(int code, char *buf, size_t bufsz) {
  error_text(code, buf, bufsz);
}

/* The version of the linked PCRE2, e.g. "10.49 2026-09-28". */
LUST_API void lust_pcre2_version(char *buf, size_t bufsz) {
  char tmp[64];
  if (bufsz == 0) return;
  tmp[0] = '\0';
  pcre2_config(PCRE2_CONFIG_VERSION, tmp);
  snprintf(buf, bufsz, "%s", tmp);
}

/*
 * Match s[0, len) starting at `startpos`; the text before startpos still
 * counts as context for ^, \b and lookbehinds.
 *
 *   mflags  M_* bits
 *   nsub    how many groups to report, 0 .. ngroups + 1 (group 0 is the
 *           whole match); nsub == 0 only tests for a match
 *   out     2 * nsub doubles: the 0-based start and end (exclusive) offset
 *           of each group, both -1 for a group that did not participate
 *
 * Returns 1 on a match, 0 if there is none and a negative PCRE2 error code
 * otherwise (invalid UTF-8, match/heap limit hit, ...; see
 * lust_pcre2_error_message).
 */
LUST_API int lust_pcre2_match(const lust_pcre2 *r, const char *s, size_t len,
                              size_t startpos, unsigned mflags, int nsub,
                              double *out) {
  uint32_t o = 0;
  int rc, i;
  const PCRE2_SIZE *ov;

  if (nsub < 0 || (uint32_t)nsub > r->ngroups + 1 || startpos > len)
    return PCRE2_ERROR_BADOFFSET;
  if (mflags & M_ANCHORED) o |= PCRE2_ANCHORED;
  if (mflags & M_ENDANCHORED) o |= PCRE2_ENDANCHORED;
  if (mflags & M_NOTEMPTY_ATSTART) o |= PCRE2_NOTEMPTY_ATSTART;
  if (mflags & M_NO_UTF_CHECK) o |= PCRE2_NO_UTF_CHECK;

  rc = pcre2_match(r->code, (PCRE2_SPTR)s, len, startpos, o, r->md, r->mctx);
  if (rc == PCRE2_ERROR_NOMATCH) return 0;
  if (rc < 0) return rc;

  ov = pcre2_get_ovector_pointer(r->md);
  for (i = 0; i < nsub; i++) {
    if (ov[2 * i] == PCRE2_UNSET) {
      out[2 * i] = out[2 * i + 1] = -1;
    } else {
      out[2 * i] = (double)ov[2 * i];
      out[2 * i + 1] = (double)ov[2 * i + 1];
    }
  }
  return 1;
}
