/*
 * lust.json: JSON through yyjson (vendor/yyjson, statically linked), a
 * Lua C API module.
 *
 *   local json = require "lust.json"
 *   json.decode('{"a":[1,2,{"b":null}]}')  --> {a = {1, 2, {b = json.null}}}
 *   json.encode({1, 2, 3})                 --> "[1,2,3]"
 *   json.encode({a = true}, {indent = 2})  --> pretty printed
 *
 * Mapping (compatible with lua-cjson, so json.null is cjson.null):
 *   - null        <-> json.null (a NULL light userdata; nil cannot be stored
 *                    in a table). nil encodes as null, too
 *   - true/false  <-> boolean
 *   - number      <-> number. Integers are written as integers up to 2^53,
 *                    other numbers as the shortest string that reads back
 *                    as the same double (0.1 stays 0.1)
 *   - string      <-> string (decoding validates UTF-8, encoding copies the
 *                    bytes: keep strings valid UTF-8)
 *   - array       <-> table 1..n, nothing else in it
 *   - object      <-> any other table. Number keys are written as strings,
 *                    other keys make encode return nil, err. The order
 *                    of the keys is Lua's: not stable between runs
 *   - {}          encodes as {} (an empty table is not known to be an array),
 *                    unless it has the metatable json.array_mt, and
 *                    json.empty_array encodes as []
 *
 * Both functions return `nil, err` when the JSON or the value is the
 * problem (decode never returns nil otherwise: null is json.null):
 * malformed JSON, NaN/Infinity (unless `nan_inf`), unsupported types
 * (function, userdata, thread), nesting deeper than `max_depth` (which also
 * catches cycles). Misuse (wrong argument types, unknown options) and out
 * of memory raise errors instead.
 *
 * Why this is fast: yyjson parses the text (validating, ~GB/s) into a
 * compact, contiguous tree, which is then turned into Lua tables with their
 * final size known up front (no rehashing). Encoding does not build a tree
 * at all: values are written straight into one buffer (that is reused by
 * the next call), strings are scanned eight bytes at a time and only the
 * rare doubles that are no integers go through yyjson's number writer.
 *
 * Why the Lua C API and not the FFI: converting between Lua values and JSON
 * is dominated by allocating tables and strings, which a JIT cannot make
 * faster, and requiring an FFI library costs far more than a C API one.
 */
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lust.h"
#include "yyjson.h"

/* Registry name of the metatable of the state (see State). */
#define STATE_MT "lust.json.state"

/* Default for the `max_depth` option, like lua-cjson. */
#define DEFAULT_MAX_DEPTH 1000

/* The buffer of an encode is dropped again if it grew beyond this. */
#define KEEP_BUF_MAX (1u << 20)

/*
 * Per Lua state (the userdata is an upvalue of decode and encode), it owns
 * the resources an operation holds while it may raise a Lua error (out of
 * memory ...): nothing leaks, whatever happens. They are released after the operation, by the next call at the
 * latest, or by __gc.
 */
typedef struct {
  yyjson_doc *doc; /* the document being turned into Lua values */
  char *buf;       /* the output of encode ... */
  size_t len;      /* ... how much of it is used ... */
  size_t cap;      /* ... and how much is allocated */
} State;

/* Address is the value of json.empty_array. */
static char empty_array_tag;

static int bad_opt(lua_State *L, const char *key) {
  return luaL_argerror(L, 2, lua_pushfstring(L, "unknown option '%s'", key));
}

/* A numeric option: an integer in [0, INT_MAX]. */
static int int_opt(lua_State *L, const char *key) {
  double d;
  if (lua_type(L, -1) != LUA_TNUMBER)
    luaL_argerror(L, 2, lua_pushfstring(L, "%s must be a number", key));
  d = lua_tonumber(L, -1);
  if (!(d >= 0 && d <= INT_MAX && d == (double)(int)d))
    luaL_argerror(L, 2, lua_pushfstring(L, "%s must be an integer in [0, %d]", key, INT_MAX));
  return (int)d;
}

/* ------------------------------------------------------------------ */
/* decode                                                              */
/* ------------------------------------------------------------------ */

/*
 * Pushes the Lua value of a JSON value. `depth` is how many more levels
 * of arrays/objects may be entered; if that is not enough (or the Lua
 * stack is), it returns 0 and leaves an unspecified number of values on
 * the stack (the caller resets it). Containers are laid out contiguously
 * (a container is followed by its children, an object by key, value, key,
 * value ...), so walking is just pointer chasing.
 */
static int push_value(lua_State *L, const yyjson_val *v, int depth) {
  switch (unsafe_yyjson_get_type(v)) {
  case YYJSON_TYPE_STR:
    lua_pushlstring(L, unsafe_yyjson_get_str(v), unsafe_yyjson_get_len(v));
    return 1;

  case YYJSON_TYPE_NUM:
    switch (unsafe_yyjson_get_subtype(v)) {
    case YYJSON_SUBTYPE_UINT:
      lua_pushnumber(L, (lua_Number)unsafe_yyjson_get_uint(v));
      break;
    case YYJSON_SUBTYPE_SINT:
      lua_pushnumber(L, (lua_Number)unsafe_yyjson_get_sint(v));
      break;
    default:
      lua_pushnumber(L, unsafe_yyjson_get_real(v));
    }
    return 1;

  case YYJSON_TYPE_BOOL:
    lua_pushboolean(L, unsafe_yyjson_get_bool(v));
    return 1;

  case YYJSON_TYPE_ARR: {
    size_t n = unsafe_yyjson_get_len(v), i;
    const yyjson_val *c = v + 1;
    /* The element needs a slot, too (and the table one, the key one) */
    if (depth <= 0 || !lua_checkstack(L, 4)) return 0;
    lua_createtable(L, n < INT_MAX ? (int)n : INT_MAX, 0);
    for (i = 1; i <= n; i++) {
      if (!push_value(L, c, depth - 1)) return 0;
      lua_rawseti(L, -2, (int)i);
      c = unsafe_yyjson_get_next(c);
    }
    return 1;
  }

  case YYJSON_TYPE_OBJ: {
    size_t n = unsafe_yyjson_get_len(v), i;
    const yyjson_val *k = v + 1;
    if (depth <= 0 || !lua_checkstack(L, 4)) return 0;
    lua_createtable(L, 0, n < INT_MAX ? (int)n : INT_MAX);
    for (i = 0; i < n; i++) {
      const yyjson_val *val = k + 1;
      lua_pushlstring(L, unsafe_yyjson_get_str(k), unsafe_yyjson_get_len(k));
      if (!push_value(L, val, depth - 1)) return 0;
      /* a duplicate key: the last one wins, like lua-cjson */
      lua_rawset(L, -3);
      k = unsafe_yyjson_get_next(val);
    }
    return 1;
  }

  default: /* YYJSON_TYPE_NULL (RAW is never produced) */
    lua_pushlightuserdata(L, NULL);
    return 1;
  }
}

/* json.decode(str [, opts]) -> value | nil, err */
static int l_decode(lua_State *L) {
  State *st = (State *)lua_touserdata(L, lua_upvalueindex(1));
  size_t len;
  const char *s = luaL_checklstring(L, 1, &len);
  yyjson_read_flag flg = YYJSON_READ_NOFLAG;
  int max_depth = DEFAULT_MAX_DEPTH;
  yyjson_read_err err;
  yyjson_doc *doc;
  int top, ok;

  if (!lua_isnoneornil(L, 2)) {
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 2)) {
      const char *k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "?";
      if (strcmp(k, "max_depth") == 0) {
        max_depth = int_opt(L, k);
      } else {
        yyjson_read_flag f;
        if (strcmp(k, "nan_inf") == 0) f = YYJSON_READ_ALLOW_INF_AND_NAN;
        else if (strcmp(k, "comments") == 0) f = YYJSON_READ_ALLOW_COMMENTS;
        else if (strcmp(k, "trailing_commas") == 0) f = YYJSON_READ_ALLOW_TRAILING_COMMAS;
        else if (strcmp(k, "invalid_unicode") == 0) f = YYJSON_READ_ALLOW_INVALID_UNICODE;
        else return bad_opt(L, k);
        if (lua_toboolean(L, -1)) flg |= f;
      }
      lua_pop(L, 1);
    }
  }

  /* What an earlier call left behind when it raised an error. */
  if (st->doc != NULL) {
    yyjson_doc_free(st->doc);
    st->doc = NULL;
  }

  /* The text is copied (the reader needs a zero padded, writable copy
   * anyway), and `s` is not modified. */
  doc = yyjson_read_opts((char *)(uintptr_t)s, len, flg, NULL, &err);
  if (doc == NULL) {
    /* err.pos is a 0-based byte offset; report the 1-based position like
     * string.find, so that s:sub(pos) starts at the offending byte (#s + 1
     * when the data ended too early). */
    lua_pushnil(L);
    lua_pushfstring(L, "%s (at position %d)", err.msg, (int)err.pos + 1);
    return 2;
  }

  st->doc = doc;
  top = lua_gettop(L);
  ok = push_value(L, yyjson_doc_get_root(doc), max_depth);
  st->doc = NULL;
  yyjson_doc_free(doc);
  if (!ok) {
    lua_settop(L, top);
    lua_pushnil(L);
    lua_pushstring(L, "nesting too deep");
    return 2;
  }
  return 1;
}

/* ------------------------------------------------------------------ */
/* encode                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
  lua_State *L;
  State *st;
  int indent;     /* spaces per level, 0: compact */
  int nan_inf;    /* write NaN and Infinity instead of raising an error */
  int max_depth;
  int array_mt;   /* stack index of json.array_mt */
  /* Why the value cannot be encoded (a format with at most one %s, and
   * its argument): set by `fail`, reported by l_encode. */
  const char *err, *what;
} Enc;

/* The encoder's way to give up: record why, the callers pass on the 0. */
static int fail(Enc *e, const char *err, const char *what) {
  e->err = err;
  e->what = what;
  return 0;
}

/* Makes room for n more bytes (the rare, slow part of `reserve`). */
static __attribute__((noinline)) char *grow(Enc *e, size_t n) {
  State *st = e->st;
  size_t cap = st->cap ? st->cap : 4096;
  char *p;
  while (cap - st->len < n) {
    if (cap > SIZE_MAX / 2) luaL_error(e->L, "out of memory");
    cap *= 2;
  }
  p = (char *)realloc(st->buf, cap);
  if (p == NULL) luaL_error(e->L, "out of memory");
  st->buf = p;
  st->cap = cap;
  return p + st->len;
}

/* Room for n more bytes; returns where to write them. The caller advances
 * st->len by what it has written. */
static inline char *reserve(Enc *e, size_t n) {
  State *st = e->st;
  if (__builtin_expect(st->cap - st->len < n, 0)) return grow(e, n);
  return st->buf + st->len;
}

static void put(Enc *e, const char *s, size_t n) {
  memcpy(reserve(e, n), s, n);
  e->st->len += n;
}

static void put_char(Enc *e, char c) {
  *reserve(e, 1) = c;
  e->st->len++;
}

/* A line break and the indentation of `level` (pretty printing only). */
static void put_newline(Enc *e, int level) {
  size_t n = (size_t)level * (size_t)e->indent;
  char *p = reserve(e, n + 1);
  *p++ = '\n';
  memset(p, ' ', n);
  e->st->len += n + 1;
}

/* Bytes that have to be escaped in a JSON string: < 0x20, '"' and '\\'. */
static const unsigned char NEEDS_ESCAPE[256] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* '"' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, /* '\\' */
};

/* Does one of the 8 bytes need escaping? (exact, no false positives) */
static inline int word_has_escape(uint64_t w) {
  const uint64_t ones = 0x0101010101010101ull, highs = 0x8080808080808080ull;
  uint64_t quote = w ^ (ones * '"'), bslash = w ^ (ones * '\\');
  return ((((w - ones * 0x20) & ~w) | ((quote - ones) & ~quote) |
           ((bslash - ones) & ~bslash)) & highs) != 0;
}

/*
 * The rare part of put_string: from the first byte that needs escaping on.
 * The worst case is six bytes per byte, reserved once.
 */
static __attribute__((noinline)) void put_string_escaped(Enc *e, const char *s, const char *end) {
  static const char HEX[] = "0123456789abcdef";
  char *base, *p;

  base = p = reserve(e, (size_t)(end - s) * 6 + 1);
  while (s < end) {
    unsigned char c = (unsigned char)*s;
    if (!NEEDS_ESCAPE[c]) {
      /* (a plain run, skipping eight bytes at a time) */
      const char *run = s;
      while ((size_t)(end - s) >= 8) {
        uint64_t w;
        memcpy(&w, s, 8);
        if (word_has_escape(w)) break;
        s += 8;
      }
      while (s < end && !NEEDS_ESCAPE[(unsigned char)*s]) s++;
      memcpy(p, run, (size_t)(s - run));
      p += s - run;
      continue;
    }
    s++;
    *p++ = '\\';
    switch (c) {
    case '"': *p++ = '"'; break;
    case '\\': *p++ = '\\'; break;
    case '\n': *p++ = 'n'; break;
    case '\r': *p++ = 'r'; break;
    case '\t': *p++ = 't'; break;
    case '\b': *p++ = 'b'; break;
    case '\f': *p++ = 'f'; break;
    default:
      *p++ = 'u'; *p++ = '0'; *p++ = '0';
      *p++ = HEX[c >> 4];
      *p++ = HEX[c & 15];
    }
  }
  *p++ = '"';
  e->st->len += (size_t)(p - base);
}

/* A string as a JSON string. Most strings need no escaping at all: they
 * are copied while scanning, eight bytes at a time. */
static void put_string(Enc *e, const char *s, size_t len) {
  const char *end = s + len;
  /* the quotes, and the room for the last word store to overshoot */
  char *base = reserve(e, len + 10), *p = base;

  *p++ = '"';
  while ((size_t)(end - s) >= 8) {
    uint64_t w;
    memcpy(&w, s, 8);
    if (word_has_escape(w)) break;
    memcpy(p, &w, 8);
    s += 8;
    p += 8;
  }
  while (s < end && !NEEDS_ESCAPE[(unsigned char)*s]) *p++ = *s++;
  if (s == end) {
    *p++ = '"';
    e->st->len += (size_t)(p - base);
    return;
  }
  e->st->len += (size_t)(p - base);
  put_string_escaped(e, s, end);
}

/* Writes an integer (|v| < 2^53) at p, returns the end. */
static char *write_int(char *p, int64_t v) {
  static const char DIGITS[] =
      "0001020304050607080910111213141516171819"
      "2021222324252627282930313233343536373839"
      "4041424344454647484950515253545556575859"
      "6061626364656667686970717273747576777879"
      "8081828384858687888990919293949596979899";
  char tmp[24];
  char *t = tmp + sizeof tmp;
  uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
  size_t n;

  while (u >= 100) {
    unsigned d = (unsigned)(u % 100) * 2;
    u /= 100;
    *--t = DIGITS[d + 1];
    *--t = DIGITS[d];
  }
  if (u >= 10) {
    *--t = DIGITS[u * 2 + 1];
    *--t = DIGITS[u * 2];
  } else {
    *--t = (char)('0' + u);
  }
  if (v < 0) *p++ = '-';
  n = (size_t)(tmp + sizeof tmp - t);
  memcpy(p, t, n);
  return p + n;
}

/* A number as a JSON number, 40 bytes are enough for all. NULL: NaN or
 * Infinity that must not be written. */
static char *write_number(Enc *e, char *p, double d) {
  if (d >= -9007199254740992.0 && d <= 9007199254740992.0 && (double)(int64_t)d == d)
    return write_int(p, (int64_t)d);
  if (isnan(d) || isinf(d)) {
    const char *lit = isnan(d) ? "NaN" : d > 0 ? "Infinity" : "-Infinity";
    size_t n = strlen(lit);
    if (!e->nan_inf) return NULL;
    memcpy(p, lit, n);
    return p + n;
  } else {
    /* yyjson's shortest round trip double to string conversion */
    yyjson_val v;
    v.tag = YYJSON_TYPE_NUM | YYJSON_SUBTYPE_REAL;
    v.uni.f64 = d;
    return yyjson_write_number(&v, p);
  }
}

static int encode_value(Enc *e, int level);

/*
 * Is the table at index t (n = #t > 0) a JSON array, i.e. are its keys
 * exactly 1..n? One pass over the keys tells: n distinct integers in
 * [1, n] are exactly 1..n. (No shortcut like "nothing follows key n":
 * the traversal order of keys in the hash part is not defined, only
 * that the array part comes first.)
 */
static int is_array(lua_State *L, int t, size_t n) {
  size_t count = 0;

  lua_pushnil(L);
  while (lua_next(L, t)) {
    double d;
    lua_pop(L, 1); /* the value, the key stays for lua_next */
    if (lua_type(L, -1) != LUA_TNUMBER) {
      lua_pop(L, 1);
      return 0;
    }
    d = lua_tonumber(L, -1);
    if (!(d >= 1 && d <= (double)n && d == (double)(size_t)d)) {
      lua_pop(L, 1);
      return 0;
    }
    count++;
  }
  return count == n;
}

/* The table on top of the stack. Returns 0 if it cannot be encoded. */
static int encode_table(Enc *e, int level) {
  lua_State *L = e->L;
  int t = lua_gettop(L);
  size_t n = lua_objlen(L, t);

  if (level >= e->max_depth || !lua_checkstack(L, 5)) return fail(e, "nesting too deep", "");

  if (n > 0 && is_array(L, t, n)) {
    size_t i;
    put_char(e, '[');
    for (i = 1; i <= n; i++) {
      if (i > 1) put_char(e, ',');
      if (e->indent) put_newline(e, level + 1);
      lua_rawgeti(L, t, (int)i);
      if (!encode_value(e, level + 1)) return 0;
      lua_pop(L, 1);
    }
    if (e->indent) put_newline(e, level);
    put_char(e, ']');
    return 1;
  }

  lua_pushnil(L);
  if (!lua_next(L, t)) {
    /* An empty table: an object, unless it is marked as array */
    int arr = lua_getmetatable(L, t) && lua_rawequal(L, -1, e->array_mt);
    if (arr) lua_pop(L, 1);
    put(e, arr ? "[]" : "{}", 2);
    return 1;
  }

  put_char(e, '{');
  do {
    /* key at -2, value at -1 */
    if (e->indent) put_newline(e, level + 1);
    if (lua_type(L, -2) == LUA_TSTRING) {
      size_t klen;
      const char *k = lua_tolstring(L, -2, &klen);
      put_string(e, k, klen);
    } else if (lua_type(L, -2) == LUA_TNUMBER) {
      /* (not lua_tolstring: that would convert the key in place) */
      char *p = reserve(e, 44);
      char *q = p;
      *q++ = '"';
      q = write_number(e, q, lua_tonumber(L, -2));
      if (q == NULL) return fail(e, "cannot serialise number: must not be NaN or Infinity", "");
      *q++ = '"';
      e->st->len += (size_t)(q - p);
    } else {
      return fail(e, "cannot serialise table key of type %s", luaL_typename(L, -2));
    }
    put_char(e, ':');
    if (e->indent) put_char(e, ' ');
    if (!encode_value(e, level + 1)) return 0;
    lua_pop(L, 1); /* the value */
    if (!lua_next(L, t)) break;
    put_char(e, ',');
  } while (1);
  if (e->indent) put_newline(e, level);
  put_char(e, '}');
  return 1;
}

/* The value on top of the stack. Returns 0 if it cannot be encoded. */
static int encode_value(Enc *e, int level) {
  lua_State *L = e->L;
  switch (lua_type(L, -1)) {
  case LUA_TSTRING: {
    size_t len;
    const char *s = lua_tolstring(L, -1, &len);
    put_string(e, s, len);
    return 1;
  }
  case LUA_TNUMBER: {
    char *p = reserve(e, 40);
    char *q = write_number(e, p, lua_tonumber(L, -1));
    if (q == NULL) return fail(e, "cannot serialise number: must not be NaN or Infinity", "");
    e->st->len += (size_t)(q - p);
    return 1;
  }
  case LUA_TBOOLEAN:
    if (lua_toboolean(L, -1)) put(e, "true", 4);
    else put(e, "false", 5);
    return 1;
  case LUA_TTABLE:
    return encode_table(e, level);
  case LUA_TNIL:
    put(e, "null", 4);
    return 1;
  case LUA_TLIGHTUSERDATA: {
    void *p = lua_touserdata(L, -1);
    if (p == NULL) {
      put(e, "null", 4);
      return 1;
    }
    if (p == &empty_array_tag) {
      put(e, "[]", 2);
      return 1;
    }
    return fail(e, "cannot serialise %s", "userdata");
  }
  default:
    return fail(e, "cannot serialise %s", luaL_typename(L, -1));
  }
}

/* Don't hold on to the memory of an exceptionally large document. */
static void trim_buf(State *st) {
  if (st->cap > KEEP_BUF_MAX) {
    free(st->buf);
    st->buf = NULL;
    st->cap = 0;
  }
}

/* json.encode(value [, opts]) -> str | nil, err */
static int l_encode(lua_State *L) {
  State *st = (State *)lua_touserdata(L, lua_upvalueindex(1));
  Enc e;

  e.L = L;
  e.st = st;
  e.indent = 0;
  e.nan_inf = 0;
  e.max_depth = DEFAULT_MAX_DEPTH;
  e.err = e.what = "";
  luaL_checkany(L, 1);

  if (!lua_isnoneornil(L, 2)) {
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 2)) {
      const char *k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "?";
      if (strcmp(k, "indent") == 0) e.indent = int_opt(L, k);
      else if (strcmp(k, "max_depth") == 0) e.max_depth = int_opt(L, k);
      else if (strcmp(k, "nan_inf") == 0) e.nan_inf = lua_toboolean(L, -1);
      else return bad_opt(L, k);
      lua_pop(L, 1);
    }
  }

  /* Arguments out of the way: json.array_mt (index 1), then the value to
   * encode, which has to be on top of the stack for encode_value */
  lua_settop(L, 1);
  lua_pushvalue(L, lua_upvalueindex(2));
  lua_insert(L, 1);
  e.array_mt = 1;

  st->len = 0;
  if (!encode_value(&e, 0)) {
    trim_buf(st);
    lua_settop(L, 0);
    lua_pushnil(L);
    lua_pushfstring(L, e.err, e.what);
    return 2;
  }
  lua_pushlstring(L, st->buf, st->len);
  trim_buf(st);
  return 1;
}

/* ------------------------------------------------------------------ */
/* module                                                              */
/* ------------------------------------------------------------------ */

static int l_state_gc(lua_State *L) {
  State *st = (State *)luaL_checkudata(L, 1, STATE_MT);
  if (st->doc != NULL) yyjson_doc_free(st->doc);
  free(st->buf);
  memset(st, 0, sizeof *st);
  return 0;
}

LUST_API int luaopen_lust_json(lua_State *L) {
  State *st;

  lua_settop(L, 0); /* (the module name) */
  lua_newtable(L);  /* the module, index 1 */

  /* The state, shared (as an upvalue) by decode and encode */
  st = (State *)lua_newuserdata(L, sizeof *st); /* 2 */
  memset(st, 0, sizeof *st);
  if (luaL_newmetatable(L, STATE_MT)) {
    lua_pushcfunction(L, l_state_gc);
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);

  lua_newtable(L); /* array_mt, 3 */
  lua_pushvalue(L, -1);
  lua_setfield(L, 1, "array_mt");

  lua_pushvalue(L, 2);
  lua_pushcclosure(L, l_decode, 1);
  lua_setfield(L, 1, "decode");

  lua_pushvalue(L, 2);
  lua_pushvalue(L, 3);
  lua_pushcclosure(L, l_encode, 2);
  lua_setfield(L, 1, "encode");

  lua_pushlightuserdata(L, NULL);
  lua_setfield(L, 1, "null");
  lua_pushlightuserdata(L, &empty_array_tag);
  lua_setfield(L, 1, "empty_array");
  lua_pushstring(L, YYJSON_VERSION_STRING);
  lua_setfield(L, 1, "version");

  lua_settop(L, 1);
  return 1;
}
