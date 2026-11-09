/*
 * lust.pcre2: PCRE2 (Perl compatible) regular expressions (vendor/pcre2,
 * 8 bit code units, statically linked) through the Lua C API. Perl/PCRE
 * syntax with backreferences, lookaround, atomic groups, possessive
 * quantifiers, recursion, Unicode properties ... (see `man pcre2pattern`).
 * It is not a Lua pattern language, and it works on UTF-8 unless told
 * otherwise.
 *
 * PCRE2 backtracks, so a pattern can take exponential time. Every match is
 * therefore bounded (`match_limit`, `depth_limit`, `max_mem`): when a limit
 * is hit, the matching function raises an error instead of hanging.
 *
 *   local pcre2 = require "lust.pcre2"
 *   local re = assert(pcre2.new("(\\w+)@(?<host>\\w+)\\.com"))
 *   re:test("mail bob@example.com")       --> true
 *   re:find("mail bob@example.com")       --> 6, 20, "bob", "example"
 *   re:match("mail bob@example.com")      --> "bob", "example"
 *   re:captures("bob@example.com")        --> {[0]="bob@example.com", "bob", "example", host="example"}
 *   for user, host in re:gmatch(s) do ... end
 *   re:gsub("bob@example.com", "\\2 at \\1")   --> "example at bob", 1
 *   pcre2.new("a.c", {caseless = true, dotall = true})
 *   pcre2.new("(") --> nil, "missing closing parenthesis (at offset 1)"
 *   pcre2.quote("a.b")                    --> "a\\.b"
 *   pcre2.version                         --> "10.49 2026-09-28"
 *
 * Positions are 1-based byte offsets like in string.find; `init` may be
 * negative. A group that did not take part in a match is `false`. A regexp
 * is garbage collected like any other value.
 *
 * Why the Lua C API and not the FFI: requiring an FFI library costs the
 * FFI itself plus a `cdef`, bytecode to load and a symbol lookup per
 * function (some 230k instructions for this module), the C API module
 * only registers its functions. Regular expressions are dominated by the
 * time PCRE2 spends matching, not by the call into it, so the JIT could
 * not make them faster anyway.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2.h"

#include "lust.h"

/* Name of the metatable of a compiled regexp. */
#define RE_MT "lust.pcre2"

/*
 * Memory PCRE2 may use for backtracking frames per match, in KiB. Its own
 * default (20 GB) is no protection at all. The match limit (10 million
 * calls of its matching function by default) bounds the time instead.
 */
#define DEFAULT_HEAP_LIMIT_KIB (128u * 1024u)

/*
 * A compiled regexp, the userdata of a Lua regexp object. It bundles the
 * pattern with the match data and the limits, so a match allocates nothing.
 * The match data is shared by all matches of this regexp: the offsets in it
 * are only valid until the next match, callbacks of gsub must not rely on
 * them (see l_gsub).
 */
typedef struct {
  pcre2_code *code;
  pcre2_match_data *md;
  pcre2_match_context *mctx;
  uint32_t ngroups; /* capturing groups, not counting the whole match */
  int bytes;        /* not UTF-8: every byte is a character */
  size_t srclen;
  char src[1]; /* the pattern (allocated behind the struct), NUL terminated */
} Re;

static Re *check_re(lua_State *L, int arg) {
  return (Re *)luaL_checkudata(L, arg, RE_MT);
}

/* __gc. The fields are NULL if pcre2.new failed half way. */
static int l_gc(lua_State *L) {
  Re *r = (Re *)luaL_checkudata(L, 1, RE_MT);
  pcre2_match_data_free(r->md);
  pcre2_match_context_free(r->mctx);
  pcre2_code_free(r->code);
  r->md = NULL;
  r->mctx = NULL;
  r->code = NULL;
  return 0;
}

/* ------------------------------------------------------------------ */
/* pcre2.new                                                           */
/* ------------------------------------------------------------------ */

/* Option name -> PCRE2 compile option; "bytes" is handled separately. */
static const struct {
  const char *name;
  uint32_t flag;
} OPTIONS[] = {
    {"caseless", PCRE2_CASELESS},         /* case insensitive */
    {"multiline", PCRE2_MULTILINE},       /* ^ and $ also match at \n */
    {"dotall", PCRE2_DOTALL},             /* "." matches "\n" */
    {"extended", PCRE2_EXTENDED},         /* ignore whitespace and #comments */
    {"extended_more", PCRE2_EXTENDED_MORE}, /* ... also blanks in classes */
    {"ungreedy", PCRE2_UNGREEDY},         /* quantifiers are lazy by default */
    {"dollar_endonly", PCRE2_DOLLAR_ENDONLY}, /* $ only at the very end */
    {"no_auto_capture", PCRE2_NO_AUTO_CAPTURE}, /* only (?<name>...) captures */
    {"ucp", PCRE2_UCP},                   /* \d \s \w \b use Unicode properties */
    {"literal", PCRE2_LITERAL},           /* the pattern is a plain string */
    {"anchored", PCRE2_ANCHORED},         /* has to match at the start */
    {"dupnames", PCRE2_DUPNAMES},         /* several groups with one name */
    {"match_unset_backref", PCRE2_MATCH_UNSET_BACKREF},
};

/* A numeric limit of the options table, 0 if the option is not set. */
static double limit_option(lua_State *L, const char *key) {
  if (lua_type(L, -1) != LUA_TNUMBER)
    luaL_argerror(L, 2, lua_pushfstring(L, "%s must be a number", key));
  return (double)lua_tonumber(L, -1);
}

/* pcre2.new(pattern [, opts]) -> regexp | nil, message */
static int l_new(lua_State *L) {
  size_t len, i;
  const char *pat = luaL_checklstring(L, 1, &len);
  uint32_t o = 0;
  int bytes = 0, code;
  double match_limit = 0, depth_limit = 0, max_mem = 0;
  PCRE2_SIZE erroff;
  Re *r;

  if (!lua_isnoneornil(L, 2)) {
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 2)) {
      /* key at -2, value at -1; a non-string key is never a valid option */
      const char *k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : NULL;
      int known = 0;
      if (k != NULL) {
        known = 1;
        if (strcmp(k, "match_limit") == 0) {
          match_limit = limit_option(L, k);
        } else if (strcmp(k, "depth_limit") == 0) {
          depth_limit = limit_option(L, k);
        } else if (strcmp(k, "max_mem") == 0) {
          max_mem = limit_option(L, k);
        } else if (strcmp(k, "bytes") == 0) {
          bytes = lua_toboolean(L, -1);
        } else {
          known = 0;
          for (i = 0; i < sizeof OPTIONS / sizeof *OPTIONS; i++) {
            if (strcmp(k, OPTIONS[i].name) == 0) {
              if (lua_toboolean(L, -1)) o |= OPTIONS[i].flag;
              known = 1;
              break;
            }
          }
        }
      }
      if (!known)
        luaL_argerror(L, 2, lua_pushfstring(L, "unknown option '%s'",
                                            k != NULL ? k : lua_typename(L, lua_type(L, -2))));
      lua_pop(L, 1); /* the value; the key stays for lua_next */
    }
  }
  if (!bytes) o |= PCRE2_UTF;

  /* The userdata comes first: if anything below raises an error, the
   * garbage collector releases what has been allocated so far. */
  r = (Re *)lua_newuserdata(L, offsetof(Re, src) + len + 1);
  memset(r, 0, offsetof(Re, src));
  luaL_getmetatable(L, RE_MT);
  lua_setmetatable(L, -2);
  r->bytes = bytes;
  r->srclen = len;
  memcpy(r->src, pat, len);
  r->src[len] = '\0';

  r->code = pcre2_compile((PCRE2_SPTR)pat, len, o, &code, &erroff, NULL);
  if (r->code == NULL) {
    char msg[200];
    if (pcre2_get_error_message(code, (PCRE2_UCHAR *)msg, sizeof msg) < 0)
      strcpy(msg, "compile error");
    lua_pushnil(L);
    lua_pushfstring(L, "%s (at offset %d)", msg, (int)erroff);
    return 2;
  }

  r->mctx = pcre2_match_context_create(NULL);
  r->md = pcre2_match_data_create_from_pattern(r->code, NULL);
  if (r->mctx == NULL || r->md == NULL) return luaL_error(L, "out of memory");

  /* The limits (0: PCRE2's defaults, except for the heap, see above):
   * `match_limit` calls of the matching function per match, `depth_limit`
   * depth of nested backtracking, `max_mem` bytes of heap for the
   * backtracking frames. */
  if (match_limit >= 1 && match_limit < 4294967295.0)
    pcre2_set_match_limit(r->mctx, (uint32_t)match_limit);
  if (depth_limit >= 1 && depth_limit < 4294967295.0)
    pcre2_set_depth_limit(r->mctx, (uint32_t)depth_limit);
  if (max_mem >= 1) {
    double kib = (max_mem + 1023.0) / 1024.0;
    pcre2_set_heap_limit(r->mctx, kib < 4294967295.0 ? (uint32_t)kib : 4294967295u);
  } else {
    pcre2_set_heap_limit(r->mctx, DEFAULT_HEAP_LIMIT_KIB);
  }

  pcre2_pattern_info(r->code, PCRE2_INFO_CAPTURECOUNT, &r->ngroups);
  return 1;
}

/*
 * pcre2.quote(s): escape everything that has a special meaning in a
 * regexp, so that the result matches s literally.
 */
static int l_quote(lua_State *L) {
  size_t len, i;
  const char *s = luaL_checklstring(L, 1, &len);
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  for (i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    /* Letters, digits, "_" and bytes >= 0x80 are kept. A backslash before
     * anything else is a literal in PCRE2, except that "\0" would be an
     * octal escape: NUL is written as \x00. */
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c >= 0x80) {
      luaL_addchar(&b, c);
    } else if (c == 0) {
      luaL_addlstring(&b, "\\x00", 4);
    } else {
      luaL_addchar(&b, '\\');
      luaL_addchar(&b, c);
    }
  }
  luaL_pushresult(&b);
  return 1;
}

/* ------------------------------------------------------------------ */
/* matching                                                            */
/* ------------------------------------------------------------------ */

/*
 * Optional 1-based start position at stack index `arg`, as in string.find
 * (negative: from the end). Sets *pos to the 0-based offset in [0, len]
 * and returns 1, or returns 0 if the position is beyond the end (no match
 * is possible).
 */
static int start_offset(lua_State *L, int arg, size_t len, size_t *pos) {
  lua_Number init;
  if (lua_isnoneornil(L, arg)) {
    *pos = 0;
    return 1;
  }
  init = luaL_checknumber(L, arg);
  init = init < 0 ? ceil(init) : floor(init); /* truncate */
  if (init < 0) {
    init = (lua_Number)len + init + 1;
    if (init < 1) init = 1;
  } else if (init == 0) {
    init = 1;
  }
  /* (also false for NaN) */
  if (!(init <= (lua_Number)len + 1)) return 0;
  *pos = (size_t)init - 1;
  return 1;
}

/*
 * Match s[0, len) starting at the 0-based offset `pos`; the text before
 * pos still counts as context for ^, \b and lookbehinds. Returns 1 on a
 * match, with the offsets of the whole match and all groups in *ov (pairs
 * of start and end, exclusive; PCRE2_UNSET for a group that did not take
 * part), and 0 if there is none. Raises an error if matching fails: invalid
 * UTF-8 in the subject, or one of the limits (match_limit, depth_limit,
 * max_mem) was hit.
 */
static int exec(lua_State *L, const Re *r, const char *s, size_t len,
                size_t pos, uint32_t mflags, const PCRE2_SIZE **ov) {
  int rc = pcre2_match(r->code, (PCRE2_SPTR)s, len, pos, mflags, r->md, r->mctx);
  if (rc == PCRE2_ERROR_NOMATCH) return 0;
  if (rc < 0) {
    char msg[256];
    if (pcre2_get_error_message(rc, (PCRE2_UCHAR *)msg, sizeof msg) < 0)
      strcpy(msg, "match error");
    return luaL_error(L, "lust.pcre2: %s", msg);
  }
  *ov = pcre2_get_ovector_pointer(r->md);
  return 1;
}

/* Push group i of the last match (false if it did not take part). */
static void push_group(lua_State *L, const char *s, const PCRE2_SIZE *ov, uint32_t i) {
  if (ov[2 * i] == PCRE2_UNSET)
    lua_pushboolean(L, 0);
  else
    lua_pushlstring(L, s + ov[2 * i], ov[2 * i + 1] - ov[2 * i]);
}

/*
 * Push the captures of the last match; the whole match if the regexp has
 * no groups (like string.match). Returns how many values were pushed.
 */
static int push_captures(lua_State *L, const Re *r, const char *s, const PCRE2_SIZE *ov) {
  uint32_t i;
  if (r->ngroups == 0) {
    push_group(L, s, ov, 0);
    return 1;
  }
  luaL_checkstack(L, (int)r->ngroups, "too many captures");
  for (i = 1; i <= r->ngroups; i++) push_group(L, s, ov, i);
  return (int)r->ngroups;
}

/* re:test(s [, init]) -> boolean */
static int l_test(lua_State *L) {
  const Re *r = check_re(L, 1);
  size_t len, pos;
  const char *s = luaL_checklstring(L, 2, &len);
  const PCRE2_SIZE *ov = NULL; /* set by a successful match */
  if (!start_offset(L, 3, len, &pos)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  lua_pushboolean(L, exec(L, r, s, len, pos, 0, &ov));
  return 1;
}

/* re:find(s [, init]) -> start, end (inclusive), captures... | nil */
static int l_find(lua_State *L) {
  const Re *r = check_re(L, 1);
  size_t len, pos;
  const char *s = luaL_checklstring(L, 2, &len);
  const PCRE2_SIZE *ov = NULL; /* set by a successful match */
  if (!start_offset(L, 3, len, &pos) || !exec(L, r, s, len, pos, 0, &ov)) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushinteger(L, (lua_Integer)ov[0] + 1);
  lua_pushinteger(L, (lua_Integer)ov[1]);
  if (r->ngroups == 0) return 2;
  return 2 + push_captures(L, r, s, ov);
}

/* re:match(s [, init]) -> captures... | whole match | nil */
static int l_match(lua_State *L) {
  const Re *r = check_re(L, 1);
  size_t len, pos;
  const char *s = luaL_checklstring(L, 2, &len);
  const PCRE2_SIZE *ov = NULL; /* set by a successful match */
  if (!start_offset(L, 3, len, &pos) || !exec(L, r, s, len, pos, 0, &ov)) {
    lua_pushnil(L);
    return 1;
  }
  return push_captures(L, r, s, ov);
}

/* re:fullmatch(s): like match, but the regexp has to match all of s. */
static int l_fullmatch(lua_State *L) {
  const Re *r = check_re(L, 1);
  size_t len;
  const char *s = luaL_checklstring(L, 2, &len);
  const PCRE2_SIZE *ov = NULL; /* set by a successful match */
  if (!exec(L, r, s, len, 0, PCRE2_ANCHORED | PCRE2_ENDANCHORED, &ov)) {
    lua_pushnil(L);
    return 1;
  }
  return push_captures(L, r, s, ov);
}

/* The name table of a regexp: see group_name. */
typedef struct {
  uint32_t count, entsize;
  const unsigned char *table;
} Names;

static void names_init(const Re *r, Names *nm) {
  nm->count = nm->entsize = 0;
  nm->table = NULL;
  pcre2_pattern_info(r->code, PCRE2_INFO_NAMECOUNT, &nm->count);
  pcre2_pattern_info(r->code, PCRE2_INFO_NAMEENTRYSIZE, &nm->entsize);
  pcre2_pattern_info(r->code, PCRE2_INFO_NAMETABLE, &nm->table);
}

/* Name of group g, NULL if it has none. The table is sorted by name; each
 * entry is the group number (two bytes, most significant first) and the
 * NUL terminated name. With duplicate names the first entry of a number
 * wins. */
static const char *group_name(const Names *nm, uint32_t g) {
  uint32_t i;
  for (i = 0; i < nm->count; i++) {
    const unsigned char *e = nm->table + (size_t)i * nm->entsize;
    if ((uint32_t)(e[0] << 8 | e[1]) == g) return (const char *)e + 2;
  }
  return NULL;
}

/*
 * re:captures(s [, init]) -> table | nil
 * [0] is the whole match, [1..n] the groups, and every named group is also
 * stored under its name.
 */
static int l_captures(lua_State *L) {
  const Re *r = check_re(L, 1);
  size_t len, pos;
  const char *s = luaL_checklstring(L, 2, &len);
  const PCRE2_SIZE *ov = NULL; /* set by a successful match */
  Names nm;
  uint32_t i;
  int t;

  if (!start_offset(L, 3, len, &pos) || !exec(L, r, s, len, pos, 0, &ov)) {
    lua_pushnil(L);
    return 1;
  }
  names_init(r, &nm);
  lua_createtable(L, (int)r->ngroups, 1);
  t = lua_gettop(L);
  for (i = 0; i <= r->ngroups; i++) {
    push_group(L, s, ov, i);
    lua_rawseti(L, t, (int)i);
  }
  for (i = 1; i <= r->ngroups; i++) {
    const char *name = group_name(&nm, i);
    if (name == NULL) continue;
    /* (with dupnames, a group that took part wins over one that did not) */
    lua_pushstring(L, name);
    lua_rawget(L, t);
    if (lua_isnil(L, -1) || ov[2 * i] != PCRE2_UNSET) {
      lua_pushstring(L, name);
      push_group(L, s, ov, i);
      lua_rawset(L, t);
    }
    lua_pop(L, 1);
  }
  return 1;
}

/* ------------------------------------------------------------------ */
/* global search: gmatch, gsub                                         */
/* ------------------------------------------------------------------ */

/*
 * Offset right behind the character starting at the 0-based offset `pos`
 * (len + 1 if pos is at or behind the end, which ends a global match loop).
 * UTF-8 aware unless the regexp works on bytes. The subject is valid UTF-8
 * here (the matcher has checked it).
 */
static size_t next_char(const Re *r, const char *s, size_t pos, size_t len) {
  unsigned char b;
  size_t n;
  if (pos >= len) return len + 1;
  if (r->bytes) return pos + 1;
  b = (unsigned char)s[pos];
  n = b < 0xC0 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
  /* never run past the end for truncated sequences */
  return pos + n > len ? len : pos + n;
}

/*
 * The next match of a global search at or behind the 0-based offset `pos`;
 * the offsets are in *ov as for exec. Returns 0 if there is none.
 *
 * This is Perl's rule: right after an empty match at `pos` (`retry`), a
 * non-empty match at the very same position is tried first; if there is
 * none, the search goes on one character further. So a non-empty match may
 * be directly followed by an empty one ("a*" on "baaac" matches "", "aaa",
 * "" and "").
 *
 * `checked`: the subject has been validated before (as UTF-8), do not
 * spend time on it again.
 */
static int scan(lua_State *L, const Re *r, const char *s, size_t len,
                size_t pos, int retry, int checked, const PCRE2_SIZE **ov) {
  while (pos <= len) {
    uint32_t mflags = checked ? PCRE2_NO_UTF_CHECK : 0;
    if (retry) mflags |= PCRE2_ANCHORED | PCRE2_NOTEMPTY_ATSTART;
    if (exec(L, r, s, len, pos, mflags, ov)) return 1;
    if (!retry) return 0;
    pos = next_char(r, s, pos, len);
    retry = 0;
  }
  return 0;
}

/*
 * The state of a gmatch loop. It is a callable userdata (__call is the
 * shared function gmatch_iter) rather than a closure: LuaJIT specializes a
 * compiled trace on the function object it calls, so a new closure per
 * gmatch call would make every loop record a new trace (hundreds of them)
 * instead of reusing one, which is several times slower than not compiling
 * at all. A callable object works in `for ... in` and when called by hand
 * (`local it = re:gmatch(s); it()`) alike.
 *
 * The regexp and the subject are kept alive by the environment table of
 * the userdata ({regexp, subject}), the pointers here are borrowed.
 */
#define GMATCH_MT "lust.pcre2.gmatch"

typedef struct {
  const Re *r;
  const char *s;
  size_t len;
  size_t pos;  /* 0-based offset to go on from; > len when done */
  int retry;   /* the last match was empty, see scan */
  int checked; /* the subject has been validated as UTF-8 */
} Gmatch;

/* __call of a gmatch loop (the arguments of the generic `for` are ignored) */
static int gmatch_iter(lua_State *L) {
  Gmatch *g = (Gmatch *)luaL_checkudata(L, 1, GMATCH_MT);
  const PCRE2_SIZE *ov = NULL; /* set by a successful match */

  if (g->pos > g->len) return 0;
  if (!scan(L, g->r, g->s, g->len, g->pos, g->retry, g->checked, &ov)) {
    g->pos = g->len + 1; /* done */
    return 0;
  }
  g->checked = 1;
  g->pos = ov[1];
  g->retry = ov[0] == ov[1];
  return push_captures(L, g->r, g->s, ov);
}

/* re:gmatch(s): iterate over all non-overlapping matches, yielding what
 * match returns. */
static int l_gmatch(lua_State *L) {
  Gmatch *g;
  check_re(L, 1);
  luaL_checklstring(L, 2, NULL);
  lua_settop(L, 2);
  g = (Gmatch *)lua_newuserdata(L, sizeof *g);
  g->r = (const Re *)lua_touserdata(L, 1);
  g->s = lua_tolstring(L, 2, &g->len);
  g->pos = 0;
  g->retry = g->checked = 0;
  luaL_getmetatable(L, GMATCH_MT);
  lua_setmetatable(L, 3);
  lua_createtable(L, 2, 0);
  lua_pushvalue(L, 1);
  lua_rawseti(L, -2, 1);
  lua_pushvalue(L, 2);
  lua_rawseti(L, -2, 2);
  lua_setfenv(L, 3);
  return 1;
}

/*
 * Check a replacement string of gsub against the group count, so that a
 * mistake is reported even if nothing matches.
 */
static void check_replacement(lua_State *L, const char *repl, size_t rlen, uint32_t n) {
  size_t i = 0;
  while (i < rlen) {
    if (repl[i] == '\\') {
      char d = i + 1 < rlen ? repl[i + 1] : '\0';
      if (d >= '0' && d <= '9') {
        if ((uint32_t)(d - '0') > n)
          luaL_error(L, "bad replacement: group \\%d does not exist "
                        "(the regexp has %d)", d - '0', (int)n);
      } else if (d != '\\') {
        luaL_error(L, "bad replacement: \\ must be followed by a digit or \\");
      }
      i += 2;
    } else {
      i++;
    }
  }
}

/* Append a (checked) replacement string with its \0 .. \9 expanded. */
static void add_replacement(luaL_Buffer *b, const char *repl, size_t rlen,
                            const char *s, const PCRE2_SIZE *ov) {
  size_t i = 0;
  while (i < rlen) {
    if (repl[i] == '\\') {
      char d = repl[i + 1];
      if (d == '\\') {
        luaL_addchar(b, '\\');
      } else {
        uint32_t g = (uint32_t)(d - '0');
        if (ov[2 * g] != PCRE2_UNSET)
          luaL_addlstring(b, s + ov[2 * g], ov[2 * g + 1] - ov[2 * g]);
      }
      i += 2;
    } else {
      luaL_addchar(b, repl[i++]);
    }
  }
}

/*
 * re:gsub(s, repl [, max]) -> new string, number of replacements
 * `repl` is
 *   - a string: "\0" is the whole match, "\1" .. "\9" the groups and "\\"
 *     a backslash,
 *   - a function called with the captures (the whole match if there are
 *     none), returning the replacement; false/nil keeps the match,
 *   - a table indexed by the first capture (or the whole match); a missing
 *     (false/nil) value keeps the match.
 * At most `max` matches are replaced (default: all).
 *
 * Stack: 1 regexp, 2 subject, 3 repl, 4 max.
 */
static int l_gsub(lua_State *L) {
  const Re *r = check_re(L, 1);
  size_t len, rlen = 0, pos = 0;
  const char *s = luaL_checklstring(L, 2, &len);
  int rtype = lua_type(L, 3), has_max = !lua_isnoneornil(L, 4);
  int retry = 0, checked = 0;
  const char *repl = NULL;
  lua_Number max = 0, count = 0;
  const PCRE2_SIZE *ov = NULL; /* set by a successful match */
  luaL_Buffer b;

  if (rtype == LUA_TNUMBER || rtype == LUA_TSTRING) {
    repl = lua_tolstring(L, 3, &rlen);
    check_replacement(L, repl, rlen, r->ngroups);
  } else if (rtype != LUA_TFUNCTION && rtype != LUA_TTABLE) {
    luaL_typerror(L, 3, "string/function/table");
  }
  if (has_max) max = luaL_checknumber(L, 4);

  luaL_buffinit(L, &b);
  while (pos <= len && (!has_max || count < max)) {
    size_t a, e; /* the match, copied out of ov: callbacks may reuse it */
    if (!scan(L, r, s, len, pos, retry, checked, &ov)) break;
    checked = 1;
    a = ov[0];
    e = ov[1];

    /* unmatched text before the match */
    if (a > pos) luaL_addlstring(&b, s + pos, a - pos);

    if (repl != NULL) {
      add_replacement(&b, repl, rlen, s, ov);
    } else {
      if (rtype == LUA_TFUNCTION) {
        int n;
        lua_pushvalue(L, 3);
        n = push_captures(L, r, s, ov);
        /* From here on, ov may be overwritten by the callback. */
        lua_call(L, n, 1);
      } else {
        push_group(L, s, ov, r->ngroups == 0 ? 0 : 1);
        lua_gettable(L, 3);
      }
      if (!lua_toboolean(L, -1)) {
        lua_pop(L, 1);
        luaL_addlstring(&b, s + a, e - a);
      } else if (lua_isstring(L, -1)) { /* (numbers are converted) */
        luaL_addvalue(&b);
      } else {
        luaL_error(L, "invalid replacement value (a %s)", luaL_typename(L, -1));
      }
    }
    count++;
    pos = e;
    retry = a == e;
  }

  if (pos <= len) luaL_addlstring(&b, s + pos, len - pos);
  luaL_pushresult(&b);
  lua_pushnumber(L, count);
  return 2;
}

/* ------------------------------------------------------------------ */
/* introspection                                                       */
/* ------------------------------------------------------------------ */

/* re:groups(): number of capturing groups (not counting the whole match) */
static int l_groups(lua_State *L) {
  lua_pushinteger(L, (lua_Integer)check_re(L, 1)->ngroups);
  return 1;
}

/* re:groupindex(): table name -> index of the named capturing groups */
static int l_groupindex(lua_State *L) {
  const Re *r = check_re(L, 1);
  Names nm;
  uint32_t i;
  names_init(r, &nm);
  lua_newtable(L);
  for (i = 1; i <= r->ngroups; i++) {
    const char *name = group_name(&nm, i);
    if (name == NULL) continue;
    lua_pushstring(L, name);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) { /* the first group of a name wins */
      lua_pushstring(L, name);
      lua_pushinteger(L, (lua_Integer)i);
      lua_rawset(L, -4);
    }
    lua_pop(L, 1);
  }
  return 1;
}

/* re:pattern(): the pattern the regexp was compiled from */
static int l_pattern(lua_State *L) {
  const Re *r = check_re(L, 1);
  lua_pushlstring(L, r->src, r->srclen);
  return 1;
}

static const luaL_Reg methods[] = {
    {"test", l_test},
    {"find", l_find},
    {"match", l_match},
    {"fullmatch", l_fullmatch},
    {"captures", l_captures},
    {"gsub", l_gsub},
    {"gmatch", l_gmatch},
    {"groups", l_groups},
    {"groupindex", l_groupindex},
    {"pattern", l_pattern},
    {NULL, NULL},
};

static const luaL_Reg funcs[] = {
    {"new", l_new},
    {"quote", l_quote},
    {NULL, NULL},
};

LUST_API int luaopen_lust_pcre2(lua_State *L) {
  char version[64];

  /* metatable: methods through __index, release through __gc */
  luaL_newmetatable(L, RE_MT);
  lua_newtable(L);
  luaL_register(L, NULL, methods);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, l_gc);
  lua_setfield(L, -2, "__gc");
  lua_pop(L, 1);
  luaL_newmetatable(L, GMATCH_MT);
  lua_pushcfunction(L, gmatch_iter);
  lua_setfield(L, -2, "__call");
  lua_pop(L, 1);

  lua_newtable(L);
  luaL_register(L, NULL, funcs);
  /* version of the linked PCRE2, e.g. "10.49 2026-09-28" */
  version[0] = '\0';
  pcre2_config(PCRE2_CONFIG_VERSION, version);
  lua_pushstring(L, version);
  lua_setfield(L, -2, "version");
  return 1;
}
