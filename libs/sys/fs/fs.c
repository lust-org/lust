/*
 * lust.sys.fs: file system basics the Lua standard library lacks.
 *
 *   local fs = require "lust.sys.fs"
 *   fs.list("src")                    --> { "a.c", "b.c", ... }
 *   fs.list("src", { types = true })  --> { "a.c", ... }, { "file", ... }
 *   fs.stat("a.c")                    --> { type = "file", size = 12, ... }
 *   fs.readlink("cc")                 --> "/usr/bin/gcc"
 *   fs.mkdir("out/a/b", { parents = true })
 *   fs.tempfile { suffix = ".txt" }   --> "/tmp/lust-Xa9f2Q.txt"
 *   fs.tempdir()                      --> "/tmp/lust-p0QmZx"
 *   for dir, name, type in fs.walkdir("src") do ... end
 *
 * Failures of the operation itself (missing files, permissions, ...)
 * return nil, "<path>: <reason>", "<ERRNO name>" (e.g. "ENOENT"), since
 * callers often branch on the reason. Misuse raises, see
 * docs/error-handling.md.
 *
 * Everything is a thin layer over one or two syscalls. Strings that are
 * the same for every call (type names, the keys of a stat table) are
 * created once, at require, and kept as upvalues: pushing them is a copy,
 * not a hash + string table lookup.
 *
 * walkdir is written in Lua (fs.lua, embedded as bytecode) on top of
 * list(path, { types = true }), whose types come with the entries for free,
 * so walking costs no stat per entry. The bytecode is only loaded on the
 * first walkdir, so `require` does not pay for it, and everything else
 * keeps working on a stock luajit whose bytecode format differs.
 */
/* strerrorname_np, mkostemps, st_mtim, d_type ... are not plain C11 */
#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lust.h"
#include "lust_errno.h"
#include "lust_scratch.h"

#ifndef LUST_LUA_BC_H
#error "LUST_LUA_BC_H must be defined (by libs/lust/meson.build)"
#endif
/* luaJIT_BC_lust_sys_fs[] and luaJIT_BC_lust_sys_fs_SIZE: the bytecode of fs.lua */
#include LUST_LUA_BC_H

#define STATE_MT "lust.sys.fs.state"

/* Prefix of the names tempfile/tempdir create, unless opts.prefix */
#define DEFAULT_PREFIX "lust-"

/* Types of directory entries, the index of their name (see type_names) */
enum { TY_FILE, TY_DIR, TY_LINK, TY_FIFO, TY_SOCK, TY_CHAR, TY_BLOCK, TY_OTHER, TY_N };
static const char *const type_names[TY_N] = {
    "file", "directory", "link", "fifo", "socket", "char", "block", "other",
};

/* Fields of a stat table, the index of their key (see stat_keys) */
enum { K_TYPE, K_SIZE, K_MODE, K_MTIME, K_ATIME, K_CTIME, K_UID, K_GID, K_NLINK, K_INO, K_DEV, K_N };
static const char *const stat_keys[K_N] = {
    "type", "size", "mode", "mtime", "atime", "ctime", "uid", "gid", "nlink", "ino", "dev",
};

/*
 * Upvalues: list has the State (1) and the type names (2 ...), stat the
 * type names (1 ...) and its keys (behind them).
 */
#define LIST_TYPE_UV(t) lua_upvalueindex(2 + (t))
#define STAT_TYPE_UV(t) lua_upvalueindex(1 + (t))
#define STAT_KEY_UV(k) lua_upvalueindex(1 + TY_N + (k))

/* Size of the buffer list reads directory entries into (as glibc's
 * opendir, which allocates one per call; ours is kept). */
#define DIRBUF_SIZE 32768

/*
 * The scratch of list (include/lust_scratch.h), it owns what a list holds
 * while pushing names may raise a Lua error (out of memory) or run a
 * finalizer that lists, too: the directory's descriptor, closed after the
 * listing or by __gc. And the buffer for the entries, allocated on the
 * first list, kept until __gc.
 */
typedef struct {
  LustScratch scratch;
  int fd;     /* -1: none */
  char *buf;  /* DIRBUF_SIZE bytes, NULL until needed */
} State;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* A path argument: a string without zero bytes (the kernel would silently
 * truncate it there). */
static const char *check_path(lua_State *L, int arg) {
  size_t len;
  const char *p = luaL_checklstring(L, arg, &len);
  if (strlen(p) != len) luaL_argerror(L, arg, "path contains a zero byte");
  return p;
}

static int bad_opt(lua_State *L, int arg, const char *key) {
  return luaL_argerror(L, arg, lua_pushfstring(L, "unknown option '%s'", key));
}

/*
 * Iterates the options table at `arg` (nil or none: no options): calls
 * opt(L, arg, key, ud) with the value on top of the stack for every key.
 * opt raises for unknown keys and bad values.
 */
typedef void (*OptFn)(lua_State *L, int arg, const char *key, void *ud);
static void parse_opts(lua_State *L, int arg, OptFn opt, void *ud) {
  if (lua_isnoneornil(L, arg)) return;
  luaL_checktype(L, arg, LUA_TTABLE);
  lua_pushnil(L);
  while (lua_next(L, arg)) {
    const char *k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "?";
    opt(L, arg, k, ud);
    lua_pop(L, 1); /* the value, the key stays for lua_next */
  }
}

/* A string option (the value on top of the stack). */
static const char *str_opt(lua_State *L, int arg, const char *key) {
  size_t len;
  const char *s;
  if (lua_type(L, -1) != LUA_TSTRING)
    luaL_argerror(L, arg, lua_pushfstring(L, "%s must be a string", key));
  s = lua_tolstring(L, -1, &len);
  if (strlen(s) != len)
    luaL_argerror(L, arg, lua_pushfstring(L, "%s contains a zero byte", key));
  return s;
}

/* An integer option in [min, max] (the value on top of the stack). */
static lua_Number int_opt(lua_State *L, int arg, const char *key, int min, int max) {
  lua_Number n;
  if (lua_type(L, -1) != LUA_TNUMBER)
    luaL_argerror(L, arg, lua_pushfstring(L, "%s must be a number", key));
  n = lua_tonumber(L, -1);
  if (!(n >= min && n <= max && n == (lua_Number)(int)n))
    luaL_argerror(L, arg, lua_pushfstring(L, "%s must be an integer in [%d, %d]", key, min, max));
  return n;
}

static int type_of_mode(mode_t m) {
  if (S_ISREG(m)) return TY_FILE;
  if (S_ISDIR(m)) return TY_DIR;
  if (S_ISLNK(m)) return TY_LINK;
  if (S_ISFIFO(m)) return TY_FIFO;
  if (S_ISSOCK(m)) return TY_SOCK;
  if (S_ISCHR(m)) return TY_CHAR;
  if (S_ISBLK(m)) return TY_BLOCK;
  return TY_OTHER;
}

/* ------------------------------------------------------------------ */
/* list                                                                */
/* ------------------------------------------------------------------ */

static void list_opt(lua_State *L, int arg, const char *key, void *ud) {
  if (strcmp(key, "types") == 0) *(int *)ud = lua_toboolean(L, -1);
  else bad_opt(L, arg, key);
}

/*
 * The type of a directory entry: from the kernel, which most file systems
 * fill in, else from an lstat. -1 if the entry vanished in between.
 */
static int type_of_dirent(int dirfd, const struct dirent64 *de) {
  struct stat sb;
  switch (de->d_type) {
  case DT_REG: return TY_FILE;
  case DT_DIR: return TY_DIR;
  case DT_LNK: return TY_LINK;
  case DT_FIFO: return TY_FIFO;
  case DT_SOCK: return TY_SOCK;
  case DT_CHR: return TY_CHAR;
  case DT_BLK: return TY_BLOCK;
  case DT_UNKNOWN:
    if (fstatat(dirfd, de->d_name, &sb, AT_SYMLINK_NOFOLLOW) == 0)
      return type_of_mode(sb.st_mode);
    return errno == ENOENT ? -1 : TY_OTHER;
  default: return TY_OTHER;
  }
}

static int is_dot_or_dotdot(const char *name) {
  return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

static void state_close(State *st) {
  if (st->fd >= 0) close(st->fd);
  st->fd = -1;
}

static int l_state_gc(lua_State *L) {
  State *st = (State *)luaL_checkudata(L, 1, STATE_MT);
  state_close(st);
  free(st->buf);
  st->buf = NULL;
  return 0;
}

/* Pushes a new State (with its __gc). */
static void *push_state(lua_State *L) {
  State *st = (State *)lua_newuserdata(L, sizeof *st);
  st->scratch.busy = 0;
  st->fd = -1;
  st->buf = NULL;
  if (luaL_newmetatable(L, STATE_MT)) {
    lua_pushcfunction(L, l_state_gc);
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
  return st;
}

/*
 * fs.list(path [, opts]) -> { name, ... } [, { type, ... }] | nil, err, code
 *
 * Reads the entries with getdents64 rather than readdir: readdir locks a
 * mutex per entry, and opendir allocates a buffer per call. The number of
 * entries of the first batch (all of them, for most directories) presizes
 * the tables.
 */
static int l_list(lua_State *L) {
  const char *path = check_path(L, 1);
  int types = 0, names_idx = 0, types_idx = 0, n = 0, e = 0;
  State *st;

  parse_opts(L, 2, list_opt, &types);
  st = (State *)lust_scratch_acquire(L, lua_upvalueindex(1), push_state);
  if (st->buf == NULL && (st->buf = (char *)malloc(DIRBUF_SIZE)) == NULL) {
    lust_scratch_release(&st->scratch);
    return luaL_error(L, "not enough memory");
  }
  st->fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (st->fd < 0) {
    lust_scratch_release(&st->scratch);
    return lust_push_errno(L, path, errno);
  }

  for (;;) {
    ssize_t len = getdents64(st->fd, st->buf, DIRBUF_SIZE), off;
    if (len <= 0) {
      if (len < 0) e = errno;
      break;
    }
    if (names_idx == 0) { /* the first batch: create the tables */
      int count = 0;
      for (off = 0; off < len; off += ((struct dirent64 *)(st->buf + off))->d_reclen)
        count++;
      lua_createtable(L, count, 0);
      names_idx = lua_gettop(L);
      if (types) {
        lua_createtable(L, count, 0);
        types_idx = names_idx + 1;
      }
    }
    for (off = 0; off < len;) {
      const struct dirent64 *de = (const struct dirent64 *)(st->buf + off);
      off += de->d_reclen;
      if (is_dot_or_dotdot(de->d_name)) continue;
      if (types) {
        int t = type_of_dirent(st->fd, de);
        if (t < 0) continue; /* (removed while listing) */
        lua_pushvalue(L, LIST_TYPE_UV(t));
        lua_rawseti(L, types_idx, n + 1);
      }
      lua_pushstring(L, de->d_name);
      lua_rawseti(L, names_idx, ++n);
    }
  }
  state_close(st);
  lust_scratch_release(&st->scratch);
  if (e != 0) return lust_push_errno(L, path, e);
  if (names_idx == 0) { /* (no entries at all: a removed directory) */
    lua_newtable(L);
    if (types) lua_newtable(L);
  }
  return types ? 2 : 1;
}

/* ------------------------------------------------------------------ */
/* stat                                                                */
/* ------------------------------------------------------------------ */

static void stat_opt(lua_State *L, int arg, const char *key, void *ud) {
  if (strcmp(key, "follow") == 0) *(int *)ud = lua_toboolean(L, -1);
  else bad_opt(L, arg, key);
}

/* A struct timespec as (fractional) seconds. */
static lua_Number seconds(struct timespec ts) {
  return (lua_Number)ts.tv_sec + (lua_Number)ts.tv_nsec / 1e9;
}

/* Sets field k of the table on top of the stack to the number v. */
static void set_num(lua_State *L, int k, lua_Number v) {
  lua_pushvalue(L, STAT_KEY_UV(k));
  lua_pushnumber(L, v);
  lua_rawset(L, -3);
}

/*
 * fs.stat(path [, opts [, t]]) -> table | nil, err, code
 *
 * With t, its fields are set and t is returned: a loop reusing one table
 * allocates nothing (after the first call has created the fields), which
 * halves the cost of a call (the new table and collecting it are the
 * larger part). t is checked before the stat, so misuse raises also for
 * a missing file.
 */
static int l_stat(lua_State *L) {
  const char *path = check_path(L, 1);
  int follow = 1, fill = !lua_isnoneornil(L, 3);
  struct stat sb;

  if (fill) luaL_checktype(L, 3, LUA_TTABLE);
  parse_opts(L, 2, stat_opt, &follow);
  if ((follow ? stat(path, &sb) : lstat(path, &sb)) != 0)
    return lust_push_errno(L, path, errno);

  if (fill) lua_settop(L, 3); /* (t on top) */
  else lua_createtable(L, 0, K_N);
  lua_pushvalue(L, STAT_KEY_UV(K_TYPE));
  lua_pushvalue(L, STAT_TYPE_UV(type_of_mode(sb.st_mode)));
  lua_rawset(L, -3);
  set_num(L, K_SIZE, (lua_Number)sb.st_size);
  set_num(L, K_MODE, (lua_Number)(sb.st_mode & 07777));
  set_num(L, K_MTIME, seconds(sb.st_mtim));
  set_num(L, K_ATIME, seconds(sb.st_atim));
  set_num(L, K_CTIME, seconds(sb.st_ctim));
  set_num(L, K_UID, (lua_Number)sb.st_uid);
  set_num(L, K_GID, (lua_Number)sb.st_gid);
  set_num(L, K_NLINK, (lua_Number)sb.st_nlink);
  /* (exact below 2^53, which is plenty for real file systems) */
  set_num(L, K_INO, (lua_Number)sb.st_ino);
  set_num(L, K_DEV, (lua_Number)sb.st_dev);
  return 1;
}

/* ------------------------------------------------------------------ */
/* readlink                                                            */
/* ------------------------------------------------------------------ */

/* fs.readlink(path) -> target | nil, err, code */
static int l_readlink(lua_State *L) {
  const char *path = check_path(L, 1);
  char sbuf[PATH_MAX]; /* Linux: always large enough, one syscall */
  char *buf = sbuf;
  size_t cap = sizeof sbuf;
  ssize_t n;

  /* readlink truncates silently: a full buffer may be a cut off target,
   * so grow (GC owned, we may raise) until it is not full */
  while ((n = readlink(path, buf, cap)) >= 0 && (size_t)n == cap) {
    cap *= 2;
    lua_settop(L, 1);
    buf = (char *)lua_newuserdata(L, cap);
  }
  if (n < 0) return lust_push_errno(L, path, errno);
  lua_pushlstring(L, buf, (size_t)n);
  return 1;
}

/* ------------------------------------------------------------------ */
/* mkdir                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
  mode_t mode;
  int parents;
} MkdirOpts;

static void mkdir_opt(lua_State *L, int arg, const char *key, void *ud) {
  MkdirOpts *o = (MkdirOpts *)ud;
  if (strcmp(key, "parents") == 0) o->parents = lua_toboolean(L, -1);
  else if (strcmp(key, "mode") == 0) o->mode = (mode_t)int_opt(L, arg, key, 0, 07777);
  else bad_opt(L, arg, key);
}

/* Whether path exists and is a directory (following symlinks). */
static int is_dir(const char *path) {
  struct stat sb;
  return stat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
}

/*
 * mkdir -p: creates every missing directory of p, the last one with
 * `mode`, the others with 0777 (both minus the umask), like coreutils.
 * Existing directories are fine. Returns 0, or an errno with p cut off
 * behind the component that failed (for the error message).
 */
static int mkdir_parents(char *p, mode_t mode) {
  char *s = p;
  while (*s == '/') s++; /* (the root always exists) */
  for (;;) {
    char *next, c;
    int last;
    while (*s != '\0' && *s != '/') s++; /* end of this component */
    next = s;
    while (*next == '/') next++;
    last = *next == '\0'; /* (trailing slashes belong to the last one) */
    c = *s;
    *s = '\0';
    /* Some file systems report EACCES/EROFS ... before EEXIST, so any
     * failure is fine as long as the directory is there. */
    if (mkdir(p, last ? mode : 0777) != 0) {
      int e = errno;
      if (!is_dir(p)) return e;
    }
    *s = c;
    if (last) return 0;
    s = next;
  }
}

/* fs.mkdir(path [, opts]) -> true | nil, err, code */
static int l_mkdir(lua_State *L) {
  size_t len;
  const char *path = check_path(L, 1);
  MkdirOpts o = {0777, 0};
  char *buf;
  int e;

  parse_opts(L, 2, mkdir_opt, &o);
  /* The common cases cost one syscall: the parent exists ... */
  if (mkdir(path, o.mode) == 0) goto ok;
  e = errno;
  if (!o.parents) return lust_push_errno(L, path, e);
  /* ... or the directory does (else ENOENT: a parent is missing) */
  if (e != ENOENT) {
    if (is_dir(path)) goto ok;
    return lust_push_errno(L, path, e);
  }

  /* A copy to cut into prefixes, owned by the GC (we may raise) */
  lua_tolstring(L, 1, &len);
  buf = (char *)lua_newuserdata(L, len + 1);
  memcpy(buf, path, len + 1);
  e = mkdir_parents(buf, o.mode);
  if (e != 0) return lust_push_errno(L, buf, e);
ok:
  lua_pushboolean(L, 1);
  return 1;
}

/* ------------------------------------------------------------------ */
/* tempfile, tempdir                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
  const char *dir, *prefix, *suffix;
  int allow_suffix; /* (mkdtemp has no suffix) */
  size_t xs;        /* offset of the XXXXXX in the template */
  size_t sl;        /* length of the suffix */
} TempOpts;

static void temp_opt(lua_State *L, int arg, const char *key, void *ud) {
  TempOpts *o = (TempOpts *)ud;
  if (strcmp(key, "dir") == 0) {
    o->dir = str_opt(L, arg, key);
  } else if (strcmp(key, "prefix") == 0 || (o->allow_suffix && strcmp(key, "suffix") == 0)) {
    const char *s = str_opt(L, arg, key);
    /* (the name would end up in another directory) */
    if (strchr(s, '/') != NULL)
      luaL_argerror(L, arg, lua_pushfstring(L, "%s must not contain '/'", key));
    if (key[0] == 'p') o->prefix = s;
    else o->suffix = s;
  } else {
    bad_opt(L, arg, key);
  }
}

/* Appends n bytes of s to the template t of PATH_MAX bytes at offset at,
 * as far as they fit; returns the offset behind them (PATH_MAX if they
 * didn't fit). */
static size_t temp_put(char *t, size_t at, const char *s, size_t n) {
  if (n > PATH_MAX - at) n = PATH_MAX - at;
  memcpy(t + at, s, n);
  return at + n;
}

/*
 * Parses the options at argument 1 and builds the template
 * "<dir>/<prefix>XXXXXX<suffix>" in t. dir defaults to $TMPDIR, or /tmp
 * if that is unset or empty. Returns 0 if it is longer than a path may be
 * (t then holds its beginning, ending in "...").
 *
 * The option strings are only anchored by the opts table, which a
 * finalizer may change: they are copied to t with no GC step between
 * parsing and copying, hence t on the C stack rather than allocated
 * (docs/gc-reentrancy.md).
 */
static int temp_template(lua_State *L, TempOpts *o, char t[PATH_MAX]) {
  size_t dl, at;

  o->dir = o->suffix = NULL;
  o->prefix = DEFAULT_PREFIX;
  parse_opts(L, 1, temp_opt, o);
  if (o->dir == NULL) {
    o->dir = getenv("TMPDIR");
    if (o->dir == NULL || o->dir[0] == '\0') o->dir = "/tmp";
  }
  if (o->suffix == NULL) o->suffix = "";
  o->sl = strlen(o->suffix);

  dl = strlen(o->dir);
  at = temp_put(t, 0, o->dir, dl);
  if (dl > 0 && o->dir[dl - 1] != '/') at = temp_put(t, at, "/", 1);
  at = temp_put(t, at, o->prefix, strlen(o->prefix));
  o->xs = at;
  at = temp_put(t, at, "XXXXXX", 6);
  at = temp_put(t, at, o->suffix, o->sl + 1); /* (with the NUL) */
  if (at < PATH_MAX || t[PATH_MAX - 1] == '\0') return 1;
  memcpy(t + PATH_MAX - 4, "...", 4);
  return 0;
}

/* The error of a failed mkostemps/mkdtemp, which may have changed the
 * XXXXXX of the template already. */
static int temp_error(lua_State *L, char *t, const TempOpts *o, int e) {
  memcpy(t + o->xs, "XXXXXX", 6);
  return lust_push_errno(L, t, e);
}

/* fs.tempfile([opts]) -> path | nil, err, code */
static int l_tempfile(lua_State *L) {
  TempOpts o;
  char t[PATH_MAX];
  int fd;

  o.allow_suffix = 1;
  if (!temp_template(L, &o, t)) return lust_push_errno(L, t, ENAMETOOLONG);
  /* Creates the file with O_EXCL and mode 0600; the name is kept, the
   * descriptor not (a C module cannot hand it to Lua as a file) */
  fd = mkostemps(t, (int)o.sl, O_CLOEXEC);
  if (fd < 0) return temp_error(L, t, &o, errno);
  close(fd);
  lua_pushstring(L, t);
  return 1;
}

/* fs.tempdir([opts]) -> path | nil, err, code */
static int l_tempdir(lua_State *L) {
  TempOpts o;
  char t[PATH_MAX];

  o.allow_suffix = 0;
  if (!temp_template(L, &o, t)) return lust_push_errno(L, t, ENAMETOOLONG);
  if (mkdtemp(t) == NULL) return temp_error(L, t, &o, errno); /* (mode 0700) */
  lua_pushstring(L, t);
  return 1;
}

/* ------------------------------------------------------------------ */
/* walkdir                                                             */
/* ------------------------------------------------------------------ */

static void walk_opt(lua_State *L, int arg, const char *key, void *ud) {
  if (strcmp(key, "max_depth") == 0) {
    *(lua_Number *)ud = int_opt(L, arg, key, 1, INT_MAX);
  } else if (strcmp(key, "prune") == 0 || strcmp(key, "on_error") == 0) {
    if (lua_type(L, -1) != LUA_TFUNCTION)
      luaL_argerror(L, arg, lua_pushfstring(L, "%s must be a function", key));
  } else {
    bad_opt(L, arg, key);
  }
}

/*
 * fs.walkdir(root [, opts]) -> iter, state | nil, err, code
 *
 * (iter, state: for a generic for, iter is the same function for all walks)
 *
 * Checks the arguments and calls the walkdir of fs.lua with plain values:
 * walkdir(root, max_depth, prune, on_error). Upvalues: that function (nil
 * until the first call loads it) and list, which fs.lua is built on.
 */
static int l_walkdir(lua_State *L) {
  lua_Number max_depth = INT_MAX;

  check_path(L, 1);
  parse_opts(L, 2, walk_opt, &max_depth);
  lua_settop(L, 2);

  if (lua_isnil(L, lua_upvalueindex(1))) {
    if (luaL_loadbuffer(L, (const char *)luaJIT_BC_lust_sys_fs,
                        luaJIT_BC_lust_sys_fs_SIZE, "=lust.sys.fs") != 0)
      return luaL_error(L,
                        "lust.sys.fs: cannot load the embedded bytecode (%s); it "
                        "was built for a different LuaJIT than the running one",
                        lua_tostring(L, -1));
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_call(L, 1, 1);
    lua_replace(L, lua_upvalueindex(1));
  }

  lua_pushvalue(L, lua_upvalueindex(1)); /* 3 */
  lua_pushvalue(L, 1);
  lua_pushnumber(L, max_depth);
  if (lua_istable(L, 2)) {
    lua_pushliteral(L, "prune");
    lua_rawget(L, 2);
    lua_pushliteral(L, "on_error");
    lua_rawget(L, 2);
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
  }
  lua_call(L, 4, LUA_MULTRET);
  return lua_gettop(L) - 2;
}

/* ------------------------------------------------------------------ */
/* module                                                              */
/* ------------------------------------------------------------------ */

static const luaL_Reg funcs[] = {
    {"readlink", l_readlink},
    {"mkdir", l_mkdir},
    {"tempfile", l_tempfile},
    {"tempdir", l_tempdir},
    {NULL, NULL},
};

/* Stack layout while opening the module */
#define MOD_IDX 1
#define TYPES_IDX 2 /* TY_N type names, then the K_N stat keys */

LUST_API int luaopen_lust_sys_fs(lua_State *L) {
  int i;

  lua_settop(L, 0); /* (the module name) */
  lua_createtable(L, 0, 7);
  luaL_register(L, NULL, funcs);
  for (i = 0; i < TY_N; i++) lua_pushstring(L, type_names[i]);
  for (i = 0; i < K_N; i++) lua_pushstring(L, stat_keys[i]);

  /* stat: the type names and keys as upvalues */
  for (i = 0; i < TY_N + K_N; i++) lua_pushvalue(L, TYPES_IDX + i);
  lua_pushcclosure(L, l_stat, TY_N + K_N);
  lua_setfield(L, MOD_IDX, "stat");

  /* list: its State and the type names as upvalues */
  push_state(L);
  for (i = 0; i < TY_N; i++) lua_pushvalue(L, TYPES_IDX + i);
  lua_pushcclosure(L, l_list, 1 + TY_N);
  lua_pushvalue(L, -1);
  lua_setfield(L, MOD_IDX, "list");

  /* walkdir: the Lua function (loaded on first use) and list */
  lua_pushnil(L);
  lua_insert(L, -2);
  lua_pushcclosure(L, l_walkdir, 2);
  lua_setfield(L, MOD_IDX, "walkdir");

  lua_settop(L, MOD_IDX);
  return 1;
}
