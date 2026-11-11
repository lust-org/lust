/*
 * lust.sys.proc: running programs without a shell.
 *
 *   local proc = require "lust.sys.proc"
 *   local status, out, err = proc.run { "git", "status", "--short" }
 *   proc.run({ "sort" }, { input = "b\na\n" })        --> 0, "a\nb\n", ""
 *   proc.run({ "make" }, { stdout = "inherit", cwd = "src" })
 *   proc.run { "no-such-program" } --> nil, "no-such-program: ...", "ENOENT"
 *
 *   local c = proc.spawn({ "rofi", "-dmenu" }, { stdin = "pipe", stdout = "pipe" })
 *   c.stdin:write("a\n", "b\n")
 *   local status, choice = c:wait()
 *   for line in proc.spawn({ "git", "ls-files" }, { stdout = "pipe" }).stdout:lines() do ... end
 *
 *   proc.exec { "vim", file }      -- (replaces this process)
 *
 * The arguments go to the program as they are (os.execute and io.popen
 * pass a command line to sh: quoting is up to the caller, and getting it
 * wrong runs something else). The program is searched in PATH unless it
 * contains a "/".
 *
 * posix_spawn starts it (glibc: clone(CLONE_VM|CLONE_VFORK), no copy of
 * the address space, the parent learns whether the exec failed). The
 * child gets no descriptors but 0, 1, 2 (files io.open opened are not
 * inherited).
 *
 * spawn returns while the child runs: a Child, with a Stream for each of
 * its stdin, stdout, stderr that is a "pipe". wait closes its stdin, then
 * one poll loop feeds opts.input and reads stdout and stderr to their end
 * (writing all input first could deadlock against a child that blocks
 * writing output nobody reads yet), and returns what was left unread of
 * them. run is spawn + wait, with stdout and stderr piped by default.
 * poll waits for output or the end of children, with a timeout: the only
 * one there is.
 *
 * Dropping a Child neither kills the child nor waits for it: it runs on
 * (session = true: also when our terminal goes away). Once it exits, it is
 * reaped by a later spawn or run (see orphans), so it leaves no zombie.
 *
 * exec replaces this process with the program instead (execvpe): what a
 * wrapper script does last. The same rule holds: the program gets no
 * descriptors but 0, 1, 2, see exec_prepare.
 *
 * Failing to start the program (not found, not executable, no such cwd,
 * out of descriptors) returns nil, "<program>: <reason>", "<errno name>"
 * (like lust.sys.fs). Misuse raises, see docs/error-handling.md.
 */
#define _GNU_SOURCE /* pipe2, ppoll, execvpe, strerrorname_np, sigabbrev_np, posix_spawn_file_actions_*_np (glibc) */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "lust.h"
#include "lust_errno.h"
#include "lust_signame.h"

/* (the same number on every architecture: Linux 5.1 and 5.3) */
#ifndef SYS_pidfd_send_signal
#define SYS_pidfd_send_signal 424
#endif
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif

extern char **environ;

#define STREAM_MT "lust.sys.proc.stream"
#define CHILD_MT "lust.sys.proc.child"

/* Reading grows a buffer to at least this much free space (a pipe holds
 * 64 KiB, so reads are at most that large anyway). */
#define BUF_INIT 16384

/* Results up to this size are copied out of a stream on the C stack (see
 * push_data). */
#define SMALL 4096

/* The child's descriptors 0, 1, 2. */
enum { IN, OUT, ERR, NFD };

/* What one of them is (opts.stdin, stdout, stderr). */
enum { STD_INHERIT, STD_NULL, STD_PIPE, STD_FILE };

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void close_fd(int *fd) {
  if (*fd >= 0) close(*fd);
  *fd = -1;
}

/* A string without zero bytes (the kernel would cut it short there). */
static int is_c_string(lua_State *L, int idx) {
  size_t len;
  const char *s;
  if (lua_type(L, idx) != LUA_TSTRING) return 0;
  s = lua_tolstring(L, idx, &len);
  return strlen(s) == len;
}

/*
 * Writing to a pipe whose reader is gone raises SIGPIPE, which would kill
 * us: it is blocked meanwhile, and one we caused is taken back before
 * unblocking (unless one was pending already). So the write just fails
 * with EPIPE, and nothing changes for the rest of the program.
 */
typedef struct {
  sigset_t set, old;
  int was_pending;
} PipeGuard;

static void pipe_guard_begin(PipeGuard *g) {
  sigset_t pend;
  sigemptyset(&g->set);
  sigaddset(&g->set, SIGPIPE);
  sigpending(&pend);
  g->was_pending = sigismember(&pend, SIGPIPE);
  sigprocmask(SIG_BLOCK, &g->set, &g->old);
}

/* epipe: a write failed with EPIPE (i.e. raised a SIGPIPE). */
static void pipe_guard_end(PipeGuard *g, int epipe) {
  if (epipe && !g->was_pending) {
    struct timespec zero = {0, 0};
    while (sigtimedwait(&g->set, NULL, &zero) < 0 && errno == EINTR) {}
  }
  sigprocmask(SIG_SETMASK, &g->old, NULL);
}

/* ------------------------------------------------------------------ */
/* Stream                                                              */
/* ------------------------------------------------------------------ */

/*
 * Our end of a pipe to a child: its stdin (written) or its stdout or
 * stderr (read, through a buffer of our own, so that lines can be split
 * and poll knows about data already read).
 *
 * The buffer is state of the object, which a finalizer run by pushing a
 * result may use, too (docs/gc-reentrancy.md). Results are copied out of
 * it and consumed before such a push, see push_data: gen tells whether
 * the stream changed meanwhile.
 */
typedef struct {
  int fd;        /* -1: none (closed, or at its end) */
  int writable;  /* a child's stdin */
  int closed;    /* closed by close() or wait: using it is a bug */
  int eof;       /* the child closed its end: reads return nil */
  int hidden;    /* run's: no Lua code can reach it (see push_data, push_stream) */
  unsigned gen;  /* changes whenever data is consumed, or it is closed */
  char *buf;     /* read: the data at buf + off, len bytes (malloc'd) */
  size_t off, len, cap;
} Stream;

static void stream_init(Stream *s, int writable) {
  memset(s, 0, sizeof *s);
  s->fd = -1;
  s->writable = writable;
}

/* Closes the descriptor and frees the buffer. */
static void stream_free(Stream *s) {
  close_fd(&s->fd);
  free(s->buf);
  s->buf = NULL;
  s->off = s->len = s->cap = 0;
  s->gen++;
}

static void consume(Stream *s, size_t n) {
  s->off += n;
  s->len -= n;
  if (s->len == 0) s->off = 0;
  s->gen++;
}

/*
 * Reads once (up to max bytes) from s->fd behind the buffered data.
 * Returns what it got (> 0), 0 at the end (s->eof is set, the descriptor
 * closed), -1 if out of memory, -2 for an error (errno). A pseudo
 * terminal reports EIO once the other side is closed: that is its end.
 */
static ssize_t stream_fill(Stream *s, size_t max) {
  ssize_t got;
  if (s->off > 0 && s->cap - s->off - s->len < BUF_INIT / 4) {
    memmove(s->buf, s->buf + s->off, s->len);
    s->off = 0;
  }
  if (s->cap - s->off - s->len < BUF_INIT / 4) {
    size_t cap = s->cap ? s->cap * 2 : BUF_INIT;
    char *b = realloc(s->buf, cap);
    if (b == NULL) return -1;
    s->buf = b;
    s->cap = cap;
  }
  if (max > s->cap - s->off - s->len) max = s->cap - s->off - s->len;
  while ((got = read(s->fd, s->buf + s->off + s->len, max)) < 0 && errno == EINTR) {}
  if (got > 0) {
    s->len += (size_t)got;
    return got;
  }
  if (got == 0 || errno == EIO) {
    s->eof = 1;
    close_fd(&s->fd);
    return 0;
  }
  return -2;
}

/*
 * Pushes the first n bytes of s's data as a string, consuming them and
 * `skip` more (a line's "\n"). Returns 0; or 1 if the stream changed while
 * the copy was allocated (a finalizer used it): nothing is pushed then,
 * the caller looks at the stream anew.
 */
static int push_data(lua_State *L, Stream *s, size_t n, size_t skip) {
  unsigned gen = s->gen;
  char *copy;
  if (s->hidden) { /* (no finalizer can use it: no copy needed) */
    lua_pushlstring(L, s->buf + s->off, n);
    consume(s, n + skip);
    return 0;
  }
  if (n <= SMALL) { /* (no allocation before the copy is ours) */
    char tmp[SMALL];
    memcpy(tmp, s->buf + s->off, n);
    consume(s, n + skip);
    lua_pushlstring(L, tmp, n);
    return 0;
  }
  copy = (char *)lua_newuserdata(L, n);
  if (s->gen != gen) {
    lua_pop(L, 1);
    return 1;
  }
  memcpy(copy, s->buf + s->off, n);
  consume(s, n + skip);
  lua_pushlstring(L, copy, n);
  lua_remove(L, -2);
  return 0;
}

static Stream *check_stream(lua_State *L, int arg) {
  return (Stream *)luaL_checkudata(L, arg, STREAM_MT);
}

/* The stream at 1 for reading, or raises. */
static Stream *check_readable(lua_State *L) {
  Stream *s = check_stream(L, 1);
  if (s->closed) luaL_error(L, "stream is closed");
  if (s->writable) luaL_error(L, "stream is not readable (a child's stdin)");
  return s;
}

/* Raises for a failed stream_fill (-1 or -2). */
static int fill_error(lua_State *L, ssize_t r) {
  int c;
  const char *rest;
  if (r == -1) return luaL_error(L, "not enough memory");
  rest = lust_strerror(errno, &c);
  return luaL_error(L, "read: %c%s", c, rest);
}

/* Pushes the next line (without "\n"), nil at the end. */
static int read_line(lua_State *L, Stream *s) {
  size_t scanned = 0;
  for (;;) {
    const char *nl = s->len > scanned ? memchr(s->buf + s->off + scanned, '\n', s->len - scanned) : NULL;
    if (nl != NULL || s->eof) {
      size_t n = nl ? (size_t)(nl - (s->buf + s->off)) : s->len;
      if (nl == NULL && n == 0) {
        lua_pushnil(L);
        return 1;
      }
      if (push_data(L, s, n, nl != NULL) == 0) return 1;
      scanned = 0; /* (changed meanwhile: look again) */
      if (s->closed) luaL_error(L, "stream is closed");
      continue;
    }
    scanned = s->len;
    ssize_t r = stream_fill(s, (size_t)-1);
    if (r < 0) fill_error(L, r);
  }
}

/*
 * stream:read([fmt]) -> data | nil
 *
 * fmt "l" (default): a line, "a": everything up to the end ("" there),
 * a number n: what is there, up to n bytes, waiting only if nothing is
 * (unlike io's read(n), which waits for all n): for relaying output.
 */
static int l_stream_read(lua_State *L) {
  Stream *s = check_readable(L);
  lua_Number n = 0;
  int all = 0;
  if (lua_type(L, 2) == LUA_TNUMBER) {
    n = lua_tonumber(L, 2);
    if (!(n >= 1 && n <= 1e15 && n == (lua_Number)(long long)n))
      luaL_argerror(L, 2, "a number of bytes must be an integer >= 1");
  } else if (!lua_isnoneornil(L, 2)) {
    const char *f = luaL_checkstring(L, 2);
    if (strcmp(f, "a") == 0) all = 1;
    else if (strcmp(f, "l") != 0) luaL_argerror(L, 2, "format must be 'l', 'a' or a number of bytes");
  }
  lua_settop(L, 1);
  if (n == 0 && !all) return read_line(L, s);
  for (;;) {
    if (all ? s->eof : (s->len > 0 || s->eof)) {
      size_t k = all || (lua_Number)s->len < n ? s->len : (size_t)n;
      if (k == 0) {
        if (all) lua_pushliteral(L, "");
        else lua_pushnil(L);
        return 1;
      }
      if (push_data(L, s, k, 0) == 0) return 1;
      if (s->closed) luaL_error(L, "stream is closed");
      continue;
    }
    ssize_t r = stream_fill(s, all || n > 65536 ? 65536 : (size_t)n);
    if (r < 0) fill_error(L, r);
  }
}

/* The iterator of lines(): (stream, previous line) -> line | nil. One
 * function for all loops (an upvalue of lines): the JIT specializes its
 * traces on it. */
static int l_lines_iter(lua_State *L) {
  Stream *s = check_readable(L);
  lua_settop(L, 1);
  return read_line(L, s);
}

/* stream:lines() -> iterator, stream */
static int l_stream_lines(lua_State *L) {
  check_readable(L);
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_pushvalue(L, 1);
  return 2;
}

/*
 * stream:write(...) -> true | nil, err, "EPIPE"
 *
 * Writes all of its arguments (strings or numbers), waiting as long as
 * the child doesn't read. A child that exited or closed its stdin fails it
 * with EPIPE (no SIGPIPE, see PipeGuard).
 */
static int l_stream_write(lua_State *L) {
  Stream *s = check_stream(L, 1);
  int top = lua_gettop(L), e = 0;
  PipeGuard g;
  if (s->closed) luaL_error(L, "stream is closed");
  if (!s->writable) luaL_error(L, "stream is not writable (a child's output)");
  /* (all converted first: converting a number creates a string, which
   * may run a finalizer; pointers into strings on our stack stay valid) */
  for (int i = 2; i <= top; i++) luaL_checklstring(L, i, NULL);
  pipe_guard_begin(&g);
  for (int i = 2; i <= top && e == 0; i++) {
    size_t len;
    const char *p = lua_tolstring(L, i, &len);
    while (len > 0) {
      ssize_t put = write(s->fd, p, len);
      if (put > 0) {
        p += put;
        len -= (size_t)put;
      } else if (errno != EINTR) {
        e = errno;
        break;
      }
    }
  }
  pipe_guard_end(&g, e == EPIPE);
  if (e != 0) return lust_push_errno(L, "write", e);
  lua_pushboolean(L, 1);
  return 1;
}

/* stream:close(): a child's stdin sees its end, its output a reader gone.
 * Closing twice is fine. */
static int l_stream_close(lua_State *L) {
  Stream *s = check_stream(L, 1);
  stream_free(s);
  s->closed = 1;
  return 0;
}

static int l_stream_gc(lua_State *L) {
  stream_free((Stream *)lua_touserdata(L, 1));
  return 0;
}

/* Pushes a new Stream userdata. A hidden one (run's) gets no metatable:
 * no methods are called on it, and its Child frees it (child_release),
 * which saves run a finalizer call per stream. */
static Stream *push_stream(lua_State *L, int writable, int hidden) {
  Stream *s = (Stream *)lua_newuserdata(L, sizeof *s);
  stream_init(s, writable);
  s->hidden = hidden;
  if (!hidden) {
    luaL_getmetatable(L, STREAM_MT);
    lua_setmetatable(L, -2);
  }
  return s;
}

/* ------------------------------------------------------------------ */
/* Child                                                               */
/* ------------------------------------------------------------------ */

/*
 * A started child: a userdata, its Streams userdata in its environment
 * table (which anchors them, and spawn's opts.input).
 */
typedef struct {
  pid_t pid;        /* > 0: started */
  int pidfd;        /* for poll and kill; -1: none (run's, or no pidfd) */
  int session;      /* leads a session (and process group) of its own */
  int reaped;       /* waited for: its status is ws */
  int ws;
  int in_fd;        /* the pipe opts.input is fed into, -1: none (any more) */
  size_t in_off;    /* how much of the input is written */
  Stream *s[NFD];   /* the "pipe" ones, NULL if none */
} Child;

/*
 * Children whose Child was collected before they exited: reaped by later
 * starts (reap_orphans), so they leave no zombies behind. They are ours
 * until reaped, so their pids can't be reused meanwhile. Per process, like
 * the children; nothing in here runs a GC step.
 */
static pid_t *orphans;
static size_t n_orphans, cap_orphans;

static void orphan_add(pid_t pid) {
  if (n_orphans == cap_orphans) {
    size_t cap = cap_orphans ? cap_orphans * 2 : 16;
    pid_t *o = realloc(orphans, cap * sizeof *o);
    if (o == NULL) return; /* (a zombie until we exit, no harm otherwise) */
    orphans = o;
    cap_orphans = cap;
  }
  orphans[n_orphans++] = pid;
}

static void reap_orphans(void) {
  for (size_t i = 0; i < n_orphans;) {
    pid_t r = waitpid(orphans[i], NULL, WNOHANG);
    if (r == 0 || (r < 0 && errno == EINTR)) i++;
    else orphans[i] = orphans[--n_orphans]; /* (reaped, or not ours: ECHILD) */
  }
}

static void child_init(Child *c) {
  memset(c, 0, sizeof *c);
  c->pidfd = c->in_fd = -1;
}

/* What a collected Child leaves: its child runs on (reaped later). */
static void child_release(Child *c) {
  close_fd(&c->in_fd);
  /* (hidden Streams have no finalizer of their own; the environment
   * table keeps them alive until this ran) */
  for (int i = 0; i < NFD; i++) {
    if (c->s[i] != NULL && c->s[i]->hidden) stream_free(c->s[i]);
  }
  if (c->pid > 0 && !c->reaped) {
    pid_t r;
    while ((r = waitpid(c->pid, NULL, WNOHANG)) < 0 && errno == EINTR) {}
    if (r == 0) orphan_add(c->pid);
    c->reaped = 1;
  }
  close_fd(&c->pidfd);
}

static int l_child_gc(lua_State *L) {
  /* (spawn's Streams are userdata of their own, possibly collected
   * already: only the hidden ones are touched) */
  child_release((Child *)lua_touserdata(L, 1));
  return 0;
}

/* child.stdin, .stdout, .stderr from its environment, else the methods
 * (upvalue 1). */
static int l_child_index(lua_State *L) {
  lua_settop(L, 2);
  /* (opts.input is kept there, too, but is no field) */
  if (lua_type(L, 2) == LUA_TSTRING && strcmp(lua_tostring(L, 2), "input") != 0) {
    lua_getfenv(L, 1);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) return 1;
  }
  lua_pushvalue(L, 2);
  lua_rawget(L, lua_upvalueindex(1));
  return 1;
}

static Child *check_child(lua_State *L, int arg) {
  return (Child *)luaL_checkudata(L, arg, CHILD_MT);
}

/* ------------------------------------------------------------------ */
/* arguments                                                           */
/* ------------------------------------------------------------------ */

/* What the options are checked for: they differ a bit. */
enum { MODE_RUN, MODE_SPAWN, MODE_EXEC };

typedef struct {
  const char *cwd;  /* NULL: ours (the strings are in stack slots) */
  int env;          /* stack index of opts.env, 0: none */
  const char *input;
  size_t input_len;
  int has_input;
  int std[NFD];     /* STD_* */
  int session;
} Opts;

/*
 * Stack slots of run, spawn and exec: the arguments, then the values of
 * opts.cwd, input, env and the files of stdin, stdout, stderr (nil if not
 * set), so that they stay valid: a finalizer run by an allocation may
 * change opts, nothing changes our stack (docs/gc-reentrancy.md).
 */
enum { ARGV = 1, OPTS, SLOT_CWD, SLOT_INPUT, SLOT_ENV, SLOT_STD, NSLOTS = SLOT_STD + NFD - 1 };

/*
 * Fills argv (n + 1 entries) with the strings of the table at index ARGV,
 * NULL terminated, or raises if one isn't a string without zero bytes.
 * The pointers are valid only while the table holds the strings: nothing
 * between this and the spawn may run a GC step. Leaves argv[1] on the
 * stack (the program, anchored for error messages after the spawn).
 */
static void fill_argv(lua_State *L, char **argv, size_t n) {
  for (size_t i = n; i > 0; i--) { /* (backwards: argv[1] stays on top) */
    if (i < n) lua_pop(L, 1);
    lua_rawgeti(L, ARGV, (int)i);
    if (!is_c_string(L, -1))
      luaL_argerror(L, ARGV, lua_pushfstring(L, "argv[%d] must be a string without zero bytes", (int)i));
    argv[i - 1] = (char *)lua_tostring(L, -1);
  }
  argv[n] = NULL;
}

/* Checks argv (at ARGV), returns its length. */
static size_t check_argv(lua_State *L) {
  size_t n;
  luaL_checktype(L, ARGV, LUA_TTABLE);
  n = lua_objlen(L, ARGV);
  if (n == 0) luaL_argerror(L, ARGV, "argv is empty");
  return n;
}

static int bad_opt(lua_State *L, const char *msg, const char *key) {
  return luaL_argerror(L, OPTS, lua_pushfstring(L, msg, key));
}

/* Whether the value on top of the stack is a file of the io library
 * (LuaJIT's and PUC Lua's both start with the FILE *, NULL once closed). */
static int is_io_file(lua_State *L) {
  int yes;
  if (lua_type(L, -1) != LUA_TUSERDATA || !lua_getmetatable(L, -1)) return 0;
  luaL_getmetatable(L, LUA_FILEHANDLE);
  yes = lua_rawequal(L, -1, -2);
  lua_pop(L, 2);
  return yes;
}

/* Checks opts.stdin, stdout or stderr (i, the value on top of the stack);
 * a file goes to its stack slot. */
static int check_std(lua_State *L, const char *key, int i) {
  const char *s = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
  if (strcmp(s, "inherit") == 0) return STD_INHERIT;
  if (strcmp(s, "null") == 0) return STD_NULL;
  if (strcmp(s, "pipe") == 0) return STD_PIPE;
  if (is_io_file(L)) {
    if (*(FILE **)lua_touserdata(L, -1) == NULL) bad_opt(L, "%s is a closed file", key);
    lua_pushvalue(L, -1);
    lua_replace(L, SLOT_STD + i);
    return STD_FILE;
  }
  return bad_opt(L, "%s must be 'inherit', 'null', 'pipe' or a file", key);
}

/* Checks the options at index OPTS into o; the values of cwd, input, env
 * and files go to their stack slots, too. exec takes only cwd and env
 * (the others are about a child we talk to). */
static void check_opts(lua_State *L, Opts *o, int mode) {
  int has_stdin = 0;
  memset(o, 0, sizeof *o);
  if (mode == MODE_RUN) o->std[OUT] = o->std[ERR] = STD_PIPE;
  if (lua_isnoneornil(L, OPTS)) return;
  luaL_checktype(L, OPTS, LUA_TTABLE);
  lua_pushnil(L);
  while (lua_next(L, OPTS)) {
    const char *k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "?";
    int slot = 0;
    if (strcmp(k, "cwd") == 0) {
      if (!is_c_string(L, -1)) bad_opt(L, "%s must be a string without zero bytes", k);
      slot = SLOT_CWD;
    } else if (strcmp(k, "env") == 0) {
      if (!lua_istable(L, -1)) bad_opt(L, "%s must be a table", k);
      slot = SLOT_ENV;
    } else if (mode == MODE_EXEC) {
      bad_opt(L, "unknown option '%s'", k);
    } else if (strcmp(k, "input") == 0) {
      if (lua_type(L, -1) != LUA_TSTRING) bad_opt(L, "%s must be a string", k);
      slot = SLOT_INPUT;
    } else if (strcmp(k, "stdin") == 0) {
      o->std[IN] = check_std(L, k, IN);
      has_stdin = 1;
    } else if (strcmp(k, "stdout") == 0) {
      o->std[OUT] = check_std(L, k, OUT);
    } else if (strcmp(k, "stderr") == 0) {
      o->std[ERR] = check_std(L, k, ERR);
    } else if (strcmp(k, "session") == 0) {
      o->session = lua_toboolean(L, -1);
    } else {
      bad_opt(L, "unknown option '%s'", k);
    }
    if (slot != 0) {
      lua_pushvalue(L, -1);
      lua_replace(L, slot);
    }
    lua_pop(L, 1);
  }
  if (!lua_isnil(L, SLOT_CWD)) o->cwd = lua_tostring(L, SLOT_CWD);
  if (!lua_isnil(L, SLOT_INPUT)) {
    if (has_stdin) luaL_argerror(L, OPTS, "input and stdin exclude each other");
    o->input = lua_tolstring(L, SLOT_INPUT, &o->input_len);
    o->has_input = 1;
  }
  if (!lua_isnil(L, SLOT_ENV)) o->env = SLOT_ENV;
}

/* Does the table at index env name the variable var ("name=value")?
 * (Runs no GC step, see build_env.) */
static int named_in(lua_State *L, int env, const char *var, size_t namelen) {
  lua_pushnil(L);
  while (lua_next(L, env)) {
    size_t kl;
    const char *k = lua_tolstring(L, -2, &kl); /* (checked: a string) */
    lua_pop(L, 1);
    if (kl == namelen && memcmp(k, var, namelen) == 0) {
      lua_pop(L, 1);
      return 1;
    }
  }
  return 0;
}

/*
 * The environment for the child: ours, with the variables of opts.env
 * (at index env: name = value or false to remove) replacing ours. A NULL
 * terminated array in a userdata; the "name=value" strings it adds are
 * kept in a table, both left on the stack.
 *
 * The array points into environ: it must be the last allocation before
 * the spawn (a finalizer could change environ).
 */
static char **build_env(lua_State *L, int env) {
  size_t n_set = 0, i = 0;
  char **envp, **e;
  int strs;
  lua_pushnil(L);
  while (lua_next(L, env)) {
    if (!is_c_string(L, -2) || lua_objlen(L, -2) == 0 || strchr(lua_tostring(L, -2), '='))
      luaL_argerror(L, OPTS, "env names must be non-empty strings without '=' and zero bytes");
    if (!is_c_string(L, -1) && !(lua_type(L, -1) == LUA_TBOOLEAN && !lua_toboolean(L, -1)))
      luaL_argerror(L, OPTS, lua_pushfstring(L, "env.%s must be a string without zero bytes or false",
                                             lua_tostring(L, -2)));
    lua_pop(L, 1);
  }
  /* the "name=value" strings of the ones set */
  lua_newtable(L);
  strs = lua_gettop(L);
  lua_pushnil(L);
  while (lua_next(L, env)) {
    if (lua_type(L, -1) == LUA_TSTRING) {
      lua_pushfstring(L, "%s=%s", lua_tostring(L, -2), lua_tostring(L, -1));
      lua_rawseti(L, strs, (int)++n_set);
    }
    lua_pop(L, 1);
  }
  /* Room for ours and those: environ is counted again after allocating
   * it, since a finalizer run by that may have added variables (setenv). */
  for (;;) {
    size_t n = 0, now = 0;
    for (e = environ; *e; e++) n++;
    envp = lua_newuserdata(L, (n + n_set + 1) * sizeof *envp);
    for (e = environ; *e; e++) now++;
    if (now <= n) break;
    lua_pop(L, 1);
  }
  /* From here on, nothing runs a GC step (a finalizer could change
   * environ while we copy it). Ours, unless replaced: */
  for (e = environ; *e; e++) {
    const char *eq = strchr(*e, '=');
    if (eq == NULL) continue; /* (malformed: nothing to pass on) */
    if (!named_in(L, env, *e, (size_t)(eq - *e))) envp[i++] = *e;
  }
  /* the ones set (anchored by strs) */
  for (size_t k = 1; k <= n_set; k++) {
    lua_rawgeti(L, strs, (int)k);
    envp[i++] = (char *)lua_tostring(L, -1);
    lua_pop(L, 1);
  }
  envp[i] = NULL;
  return envp;
}

/* The failure of starting program (in cwd, unless NULL): returns nil,
 * "<program>[ (in <cwd>)]: <reason>", "<errno name>". */
static int push_start_error(lua_State *L, const char *program, const char *cwd, int e) {
  int c;
  const char *rest;
  if (cwd == NULL) return lust_push_errno(L, program, e);
  rest = lust_strerror(e, &c);
  lua_pushnil(L);
  lua_pushfstring(L, "%s (in %s): %c%s", program, cwd, c, rest);
  return lust_errno_result(L, e);
}

/* ------------------------------------------------------------------ */
/* starting the child                                                  */
/* ------------------------------------------------------------------ */

#ifndef __GLIBC__
/* Without glibc's posix_spawn_file_actions_addclosefrom_np (musl): has
 * the child close every descriptor above 2 it would inherit, i.e. the
 * open ones without FD_CLOEXEC (the others go with the exec anyway).
 * They are looked up in /proc/self/fd, without /proc all up to the
 * descriptor limit are checked. Returns 0 or an error number. */
static int add_close_inherited(posix_spawn_file_actions_t *fa) {
  int e = 0;
  DIR *d = opendir("/proc/self/fd");
  if (d != NULL) {
    struct dirent *de;
    while (e == 0 && (de = readdir(d)) != NULL) {
      int fd = atoi(de->d_name); /* (0 for "." and "..") */
      int fl = fd > 2 && fd != dirfd(d) ? fcntl(fd, F_GETFD) : -1;
      if (fl >= 0 && !(fl & FD_CLOEXEC)) e = posix_spawn_file_actions_addclose(fa, fd);
    }
    closedir(d);
    return e;
  }
  long max = sysconf(_SC_OPEN_MAX);
  for (int fd = 3; fd < max && e == 0; fd++) {
    int fl = fcntl(fd, F_GETFD);
    if (fl >= 0 && !(fl & FD_CLOEXEC)) e = posix_spawn_file_actions_addclose(fa, fd);
  }
  return e;
}
#endif

/*
 * Moves *fd above 2 (close-on-exec). Returns 0 or an errno (*fd stays
 * then). With our stdin, stdout or stderr closed, a new pipe can get one
 * of 0, 1, 2: the child's end would be replaced by the dup2 for another
 * (or not be closed in the child), ours handed on to it.
 */
static int lift(int *fd) {
  int moved;
  if (*fd > 2) return 0;
  if ((moved = fcntl(*fd, F_DUPFD_CLOEXEC, 3)) < 0) return errno;
  close(*fd);
  *fd = moved;
  return 0;
}

/*
 * The descriptor of the io file in slot (opts.stdin, stdout, stderr), its
 * buffered output written first, so it ends up before the child's. -1
 * (EBADF) if it was closed meanwhile (by a finalizer).
 */
static int file_fd(lua_State *L, int slot) {
  FILE *fp = *(FILE **)lua_touserdata(L, slot);
  if (fp == NULL) return -1;
  fflush(fp);
  return fileno(fp);
}

/*
 * Creates the pipes and starts the child into c (and the Streams c->s);
 * nothing in here runs a GC step. Returns 0, or the errno of what failed
 * (c and its Streams are left as they were).
 */
static int start(lua_State *L, Child *c, char **argv, char **envp, const Opts *o) {
  posix_spawn_file_actions_t fa;
  posix_spawnattr_t at;
  int child_end[NFD], ours[NFD], e = 0;
  pid_t pid;

  for (int i = 0; i < NFD; i++) child_end[i] = ours[i] = -1;
  for (int i = 0; i < NFD && e == 0; i++) {
    if (o->std[i] == STD_PIPE || (i == IN && o->has_input)) {
      int p[2];
      if (pipe2(p, O_CLOEXEC) != 0) {
        e = errno;
        break;
      }
      /* (our end: the writing one of stdin, the reading one else) */
      ours[i] = p[i == IN];
      child_end[i] = p[i != IN];
      if ((e = lift(&ours[i])) == 0) e = lift(&child_end[i]);
    } else if (o->std[i] == STD_FILE) {
      int fd = file_fd(L, SLOT_STD + i);
      if (fd < 0) e = EBADF;
      /* (a copy, closed below: not the file's own) */
      else if ((child_end[i] = fcntl(fd, F_DUPFD_CLOEXEC, 3)) < 0) e = errno;
    }
  }

  if (e == 0 && (e = posix_spawn_file_actions_init(&fa)) == 0) {
    for (int i = 0; i < NFD && e == 0; i++) {
      if (child_end[i] >= 0) /* (dup2 clears O_CLOEXEC on the copy) */
        e = posix_spawn_file_actions_adddup2(&fa, child_end[i], i);
      else if (o->std[i] == STD_NULL)
        e = posix_spawn_file_actions_addopen(&fa, i, "/dev/null", i == IN ? O_RDONLY : O_WRONLY, 0);
    }
    /* (everything above 2: the pipes and whatever else is open) */
#ifdef __GLIBC__
    if (e == 0) e = posix_spawn_file_actions_addclosefrom_np(&fa, 3);
#else
    if (e == 0) e = add_close_inherited(&fa);
#endif
    if (e == 0 && o->cwd) e = posix_spawn_file_actions_addchdir_np(&fa, o->cwd);
    if (e == 0 && o->session && (e = posix_spawnattr_init(&at)) == 0) {
      e = posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSID);
      if (e == 0) e = posix_spawnp(&pid, argv[0], &fa, &at, argv, envp ? envp : environ);
      posix_spawnattr_destroy(&at);
    } else if (e == 0) {
      e = posix_spawnp(&pid, argv[0], &fa, NULL, argv, envp ? envp : environ);
    }
    posix_spawn_file_actions_destroy(&fa);
  }

  for (int i = 0; i < NFD; i++) {
    close_fd(&child_end[i]);
    if (e != 0) close_fd(&ours[i]);
  }
  if (e != 0) return e;

  c->pid = pid;
  c->session = o->session;
  if (o->has_input) {
    c->in_fd = ours[IN];
    /* (written as the child reads, see drain) */
    fcntl(c->in_fd, F_SETFL, O_NONBLOCK);
    if (o->input_len == 0) close_fd(&c->in_fd);
  } else if (c->s[IN] != NULL) {
    c->s[IN]->fd = ours[IN];
  }
  for (int i = OUT; i <= ERR; i++) {
    if (c->s[i] != NULL) c->s[i]->fd = ours[i];
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* waiting for the child                                               */
/* ------------------------------------------------------------------ */

/*
 * Closes the child's stdin pipe, feeds the input (if any) and reads its
 * output pipes to their end, then reaps it. Nothing in here runs a GC
 * step. Returns 0, -1 if out of memory, or the errno of a failed waitpid.
 */
static int drain(Child *c, const char *input, size_t input_len) {
  struct pollfd pfd[NFD];
  int which[NFD]; /* IN, OUT or ERR of pfd[k] */
  int epipe = 0, guarded = c->in_fd >= 0;
  PipeGuard g;

  if (c->s[IN] != NULL) {
    stream_free(c->s[IN]);
    c->s[IN]->closed = 1;
  }
  if (guarded) pipe_guard_begin(&g);
  for (;;) {
    int n = 0;
    if (c->in_fd >= 0) {
      pfd[n].fd = c->in_fd;
      pfd[n].events = POLLOUT;
      which[n++] = IN;
    }
    for (int i = OUT; i <= ERR; i++) {
      if (c->s[i] == NULL || c->s[i]->fd < 0) continue;
      pfd[n].fd = c->s[i]->fd;
      pfd[n].events = POLLIN;
      which[n++] = i;
    }
    if (n == 0) break;
    if (poll(pfd, (nfds_t)n, -1) < 0) {
      if (errno == EINTR) continue;
      break; /* (can't happen with valid descriptors) */
    }
    for (int k = 0; k < n; k++) {
      int i = which[k];
      if (pfd[k].revents == 0) continue;
      if (i != IN) {
        ssize_t r = stream_fill(c->s[i], (size_t)-1);
        if (r == -1) {
          if (guarded) pipe_guard_end(&g, epipe);
          return -1;
        }
        if (r == -2) { /* (broken: take it as the end) */
          c->s[i]->eof = 1;
          close_fd(&c->s[i]->fd);
        }
      } else {
        ssize_t put = write(c->in_fd, input + c->in_off, input_len - c->in_off);
        if (put > 0) c->in_off += (size_t)put;
        else if (errno == EPIPE) epipe = 1;
        if (c->in_off == input_len || (put < 0 && errno != EAGAIN && errno != EINTR))
          close_fd(&c->in_fd); /* (all written, or the child doesn't read) */
      }
    }
  }
  if (guarded) pipe_guard_end(&g, epipe);

  while (!c->reaped) {
    if (waitpid(c->pid, &c->ws, 0) >= 0) {
      c->reaped = 1;
    } else if (errno != EINTR) {
      /* (ECHILD: someone else reaped it, can't happen unless SIGCHLD is
       * ignored) */
      c->reaped = 1;
      c->ws = 0;
      return errno;
    }
  }
  return 0;
}

/* Pushes how the child ended: its exit code, or the name of the signal
 * that killed it. */
static void push_status(lua_State *L, int ws) {
  if (WIFEXITED(ws)) {
    lua_pushinteger(L, WEXITSTATUS(ws));
  } else {
    int sig = WTERMSIG(ws);
    const char *name = lust_sigabbrev(sig);
    if (name != NULL) lua_pushfstring(L, "SIG%s", name);
    else lua_pushfstring(L, "SIGRTMIN+%d", sig - SIGRTMIN); /* (the only ones without) */
  }
}

/* Pushes the unread rest of output stream s ("" if none), nil without. */
static void push_rest(lua_State *L, Stream *s) {
  if (s == NULL) {
    lua_pushnil(L);
    return;
  }
  for (;;) {
    if (s->len == 0) {
      lua_pushliteral(L, "");
      return;
    }
    if (push_data(L, s, s->len, 0) == 0) return;
  }
}

/* drain, then pushes status, stdout, stderr (returns 3), or raises / the
 * waitpid error (program: for its message). */
static int finish(lua_State *L, Child *c, const char *input, size_t input_len, const char *program) {
  int e = drain(c, input, input_len);
  if (e == -1) return luaL_error(L, "not enough memory");
  if (e != 0) return lust_push_errno(L, program, e);
  push_status(L, c->ws);
  push_rest(L, c->s[OUT]);
  push_rest(L, c->s[ERR]);
  /* (at their end now: their buffers aren't needed any more) */
  for (int i = OUT; i <= ERR; i++) {
    if (c->s[i] != NULL) stream_free(c->s[i]);
  }
  return 3;
}

/* ------------------------------------------------------------------ */
/* run, spawn                                                          */
/* ------------------------------------------------------------------ */

/*
 * Starts the child of run or spawn (mode) from the arguments: a Child
 * userdata, on the stack at index NSLOTS + 2, whose environment table
 * holds its Streams (stdin, stdout, stderr) and spawn's opts.input (fed
 * by wait). Returns it, *program set to argv[1] (anchored on the stack);
 * or NULL, the start error pushed (3 values).
 */
static Child *start_child(lua_State *L, int mode, const char **program) {
  Opts o;
  char **argv, **envp = NULL;
  Child *c;
  int e, child_idx;
  size_t n;
  static const char *const names[NFD] = {"stdin", "stdout", "stderr"};
  lua_settop(L, NSLOTS); /* (the arguments, then the slots, all nil) */
  n = check_argv(L);
  check_opts(L, &o, mode);

  /* Everything that allocates first (build_env last) ... */
  argv = lua_newuserdata(L, (n + 1) * sizeof *argv);
  c = lua_newuserdata(L, sizeof *c);
  child_init(c);
  child_idx = lua_gettop(L);
  lua_createtable(L, 0, NFD + 1);
  for (int i = 0; i < NFD; i++) {
    if (o.std[i] != STD_PIPE) continue;
    /* (run's child, so its streams, never reaches Lua code) */
    c->s[i] = push_stream(L, i == IN, mode == MODE_RUN);
    lua_setfield(L, -2, names[i]);
  }
  if (o.has_input && mode == MODE_SPAWN) {
    lua_pushvalue(L, SLOT_INPUT);
    lua_setfield(L, -2, "input");
  }
  lua_setfenv(L, child_idx);
  luaL_getmetatable(L, CHILD_MT);
  lua_setmetatable(L, child_idx);
  if (o.env) envp = build_env(L, o.env);
  /* ... then argv, whose strings only the table holds: nothing may run a
   * GC step (and a finalizer change the table) until the spawn */
  fill_argv(L, argv, n);

  reap_orphans();
  if ((e = start(L, c, argv, envp, &o)) != 0) {
    push_start_error(L, argv[0], o.cwd, e);
    return NULL;
  }
  *program = argv[0]; /* (anchored on the stack, see fill_argv) */
  return c;
}

/* proc.run(argv [, opts]) -> status, stdout, stderr | nil, err, code */
static int l_run(lua_State *L) {
  const char *program, *input = NULL;
  size_t input_len = 0;
  Child *c = start_child(L, MODE_RUN, &program);
  if (c == NULL) return 3;
  if (!lua_isnil(L, SLOT_INPUT)) input = lua_tolstring(L, SLOT_INPUT, &input_len);
  return finish(L, c, input, input_len, program);
}

/* proc.spawn(argv [, opts]) -> child | nil, err, code */
static int l_spawn(lua_State *L) {
  const char *program;
  Child *c = start_child(L, MODE_SPAWN, &program);
  if (c == NULL) return 3;
  /* (fails without pidfds, before Linux 5.3: then poll can't wait for
   * the child, see l_poll; run's child needs none) */
  c->pidfd = (int)syscall(SYS_pidfd_open, c->pid, 0);
  if (c->pidfd < 0) c->pidfd = -1;
  lua_pushvalue(L, NSLOTS + 2);
  return 1;
}

/*
 * child:wait() -> status, stdout, stderr | nil, err, code
 *
 * Closes its stdin pipe (after feeding opts.input), reads its output
 * pipes to their end (their unread rest: stdout, stderr; nil if not
 * piped), waits for it to exit. Again: the same status, "" as output.
 */
static int l_wait(lua_State *L) {
  Child *c = check_child(L, 1);
  const char *input = NULL;
  size_t input_len = 0;
  lua_settop(L, 1);
  lua_getfenv(L, 1);
  lua_getfield(L, 2, "input"); /* (3: anchored on our stack) */
  if (lua_type(L, 3) == LUA_TSTRING) input = lua_tolstring(L, 3, &input_len);
  return finish(L, c, input, input_len, "wait");
}

/*
 * child:kill([sig]) -> true | nil, err, code
 *
 * sig: a name as signal.number takes it, default "SIGTERM". A child that
 * leads a session gets it with its whole process group. Others through
 * their pidfd, which still refers to them after they were reaped: a pid
 * could be another process's by then. Nothing left to signal: "ESRCH".
 */
static int l_kill(lua_State *L) {
  Child *c = check_child(L, 1);
  int sig = SIGTERM, r;
  if (!lua_isnoneornil(L, 2)) {
    sig = lust_signum(luaL_checkstring(L, 2));
    if (sig == 0) luaL_argerror(L, 2, lua_pushfstring(L, "unknown signal '%s'", lua_tostring(L, 2)));
  }
  if (c->session) {
    r = kill(-c->pid, sig); /* (a group's id isn't reused while it has members) */
  } else if (c->pidfd >= 0) {
    r = (int)syscall(SYS_pidfd_send_signal, c->pidfd, sig, NULL, 0);
  } else if (!c->reaped) {
    r = kill(c->pid, sig);
  } else {
    r = -1;
    errno = ESRCH;
  }
  if (r != 0) return lust_push_errno(L, "kill", errno);
  lua_pushboolean(L, 1);
  return 1;
}

/* ------------------------------------------------------------------ */
/* poll                                                                */
/* ------------------------------------------------------------------ */

/* Upvalues of poll: the metatables of Stream and Child. */
#define POLL_STREAM_MT lua_upvalueindex(1)
#define POLL_CHILD_MT lua_upvalueindex(2)

/*
 * proc.poll(list [, timeout]) -> { ready, ... }
 *
 * list: output Streams and Childs. Waits until one of them is ready (a
 * stream has data or is at its end: its read won't wait; a child
 * exited), at most timeout seconds (default: no limit). Returns the ready
 * ones in the order of list, {} on timeout.
 *
 * Allocates everything first: from the first look at list to the result,
 * nothing runs a GC step, so a finalizer can't change list in between.
 */
static int l_poll(lua_State *L) {
  struct pollfd *pfd;
  char *now;
  int n, ready = 0, k = 0, rc;
  lua_Number timeout = -1;
  struct timespec deadline = {0, 0};

  luaL_checktype(L, 1, LUA_TTABLE);
  if (!lua_isnoneornil(L, 2)) {
    timeout = luaL_checknumber(L, 2);
    if (!(timeout >= 0)) luaL_argerror(L, 2, "timeout must be a number >= 0");
    if (timeout >= 1e9) timeout = -1; /* (~30 years, math.huge: none) */
  }
  lua_settop(L, 1);
  n = (int)lua_objlen(L, 1);
  pfd = lua_newuserdata(L, (size_t)n * (sizeof *pfd + 1) + 1);
  now = (char *)(pfd + n); /* (ready without waiting) */
  lua_createtable(L, n, 0); /* 3: the result */

  for (int i = 1; i <= n; i++) {
    struct pollfd *p = &pfd[i - 1];
    p->fd = -1;
    p->events = POLLIN;
    p->revents = 0;
    now[i - 1] = 0;
    lua_rawgeti(L, 1, i);
    if (!lua_getmetatable(L, -1)) lua_pushnil(L);
    if (lua_rawequal(L, -1, POLL_STREAM_MT)) {
      Stream *s = lua_touserdata(L, -2);
      if (s->closed) luaL_argerror(L, 1, lua_pushfstring(L, "list[%d] is a closed stream", i));
      if (s->writable) luaL_argerror(L, 1, lua_pushfstring(L, "list[%d] is a child's stdin", i));
      if (s->len > 0 || s->eof) now[i - 1] = 1;
      else p->fd = s->fd;
    } else if (lua_rawequal(L, -1, POLL_CHILD_MT)) {
      Child *c = lua_touserdata(L, -2);
      /* (without a pidfd it can't be waited for: ready, wait tells) */
      if (c->reaped || c->pidfd < 0) now[i - 1] = 1;
      else p->fd = c->pidfd;
    } else {
      luaL_argerror(L, 1, lua_pushfstring(L, "list[%d] is no stream or child", i));
    }
    lua_pop(L, 2);
    ready |= now[i - 1];
  }

  if (ready) timeout = 0;
  if (timeout > 0) { /* (absolute: an interruption doesn't add to it) */
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += (time_t)timeout;
    deadline.tv_nsec += (long)((timeout - (lua_Number)(time_t)timeout) * 1e9);
    if (deadline.tv_nsec >= 1000000000L) {
      deadline.tv_sec++;
      deadline.tv_nsec -= 1000000000L;
    }
  }
  for (;;) {
    struct timespec left = {0, 0}, cur;
    if (timeout > 0) {
      clock_gettime(CLOCK_MONOTONIC, &cur);
      left.tv_sec = deadline.tv_sec - cur.tv_sec;
      left.tv_nsec = deadline.tv_nsec - cur.tv_nsec;
      if (left.tv_nsec < 0) {
        left.tv_sec--;
        left.tv_nsec += 1000000000L;
      }
      if (left.tv_sec < 0) left.tv_sec = left.tv_nsec = 0;
    }
    rc = ppoll(pfd, (nfds_t)n, timeout < 0 ? NULL : &left, NULL);
    if (rc >= 0 || errno != EINTR) break;
  }
  if (rc < 0) return luaL_error(L, "poll: %s", strerror(errno)); /* (ENOMEM) */

  for (int i = 1; i <= n; i++) {
    if (!now[i - 1] && pfd[i - 1].revents == 0) continue;
    lua_rawgeti(L, 1, i);
    lua_rawseti(L, 3, ++k);
  }
  return 1;
}

/* ------------------------------------------------------------------ */
/* exec                                                                */
/* ------------------------------------------------------------------ */

/*
 * What exec changes before the execvpe, to be undone if that fails (the
 * process goes on then, and must not notice).
 */
typedef struct {
  int *fds;     /* the descriptors whose FD_CLOEXEC we set (malloc'd) */
  size_t n;
  int cwd;      /* our working directory (O_PATH) if opts.cwd, else -1 */
} ExecUndo;

/* Appends fd to u->fds. Returns 0, or -1 if out of memory. */
static int undo_add(ExecUndo *u, size_t *cap, int fd) {
  if (u->n == *cap) {
    size_t c = *cap ? *cap * 2 : 16;
    int *f = realloc(u->fds, c * sizeof *f);
    if (f == NULL) return -1;
    u->fds = f;
    *cap = c;
  }
  u->fds[u->n++] = fd;
  return 0;
}

/*
 * Notes in u every descriptor above 2 the program would inherit (open,
 * without FD_CLOEXEC: e.g. the files io.open opened). They are looked up
 * in /proc/self/fd, without /proc all up to the descriptor limit are
 * checked. Returns 0, or -1 if out of memory.
 */
static int collect_inherited(ExecUndo *u) {
  size_t cap = 0;
  DIR *d = opendir("/proc/self/fd");
  if (d != NULL) {
    struct dirent *de;
    int status = 0;
    while (status == 0 && (de = readdir(d)) != NULL) {
      int fd = atoi(de->d_name); /* (0 for "." and "..") */
      int fl = fd > 2 && fd != dirfd(d) ? fcntl(fd, F_GETFD) : -1;
      if (fl >= 0 && !(fl & FD_CLOEXEC)) status = undo_add(u, &cap, fd);
    }
    closedir(d);
    return status;
  }
  long max = sysconf(_SC_OPEN_MAX);
  for (int fd = 3; fd < max; fd++) {
    int fl = fcntl(fd, F_GETFD);
    if (fl >= 0 && !(fl & FD_CLOEXEC) && undo_add(u, &cap, fd) != 0) return -1;
  }
  return 0;
}

/* Undoes what exec_prepare did (as far as it got) and frees u. */
static void exec_undo(ExecUndo *u) {
  for (size_t i = 0; i < u->n; i++) {
    int fl = fcntl(u->fds[i], F_GETFD);
    if (fl >= 0) fcntl(u->fds[i], F_SETFD, fl & ~FD_CLOEXEC);
  }
  free(u->fds);
  u->fds = NULL;
  u->n = 0;
  if (u->cwd >= 0) {
    /* (can't fail: the descriptor is a directory we were in) */
    if (fchdir(u->cwd) != 0) {}
    close(u->cwd);
    u->cwd = -1;
  }
}

/*
 * Gets the process ready for the exec: the descriptors above 2 it would
 * hand on get FD_CLOEXEC (like run, the program gets 0, 1, 2 only), the
 * working directory changes to cwd (if not NULL). Returns 0; or an errno
 * (u is undone then), ENOMEM if out of memory.
 *
 * Closing the descriptors instead would be final: if the exec fails, the
 * files Lua still holds would be gone.
 */
static int exec_prepare(ExecUndo *u, const char *cwd) {
  u->fds = NULL;
  u->n = 0;
  u->cwd = -1;
  if (collect_inherited(u) != 0) {
    exec_undo(u);
    return ENOMEM;
  }
  for (size_t i = 0; i < u->n; i++) fcntl(u->fds[i], F_SETFD, FD_CLOEXEC);
  if (cwd != NULL) {
    int e;
    u->cwd = open(".", O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (u->cwd < 0 || chdir(cwd) != 0) {
      e = errno;
      exec_undo(u);
      return e;
    }
  }
  return 0;
}

/*
 * proc.exec(argv [, opts]) -> (only on failure) nil, err, code
 *
 * Stack slots, argv and env as for run. Lua's output (io.write, print)
 * sits in C stdio buffers, which the exec would drop: all are flushed
 * first.
 */
static int l_exec(lua_State *L) {
  Opts o;
  ExecUndo u;
  char **argv, **envp = NULL;
  int e;
  size_t n;
  lua_settop(L, NSLOTS);
  n = check_argv(L);
  check_opts(L, &o, MODE_EXEC);

  /* Everything that allocates first, then argv (see run) */
  argv = lua_newuserdata(L, (n + 1) * sizeof *argv);
  if (o.env) envp = build_env(L, o.env);
  fill_argv(L, argv, n);

  e = exec_prepare(&u, o.cwd);
  if (e == ENOMEM) return luaL_error(L, "not enough memory");
  if (e == 0) {
    fflush(NULL);
    execvpe(argv[0], argv, envp ? envp : environ);
    e = errno;
    exec_undo(&u);
  }
  return push_start_error(L, argv[0], o.cwd, e);
}

/* ------------------------------------------------------------------ */
/* module                                                              */
/* ------------------------------------------------------------------ */

static const luaL_Reg funcs[] = {
    {"run", l_run},
    {"spawn", l_spawn},
    {"exec", l_exec},
    {NULL, NULL},
};

static const luaL_Reg stream_methods[] = {
    {"read", l_stream_read},
    {"write", l_stream_write},
    {"close", l_stream_close},
    {NULL, NULL},
};

static const luaL_Reg child_methods[] = {
    {"wait", l_wait},
    {"kill", l_kill},
    {NULL, NULL},
};

LUST_API int luaopen_lust_sys_proc(lua_State *L) {
  lua_settop(L, 0); /* (the module name) */
  lua_createtable(L, 0, 4); /* 1: the module */
  luaL_register(L, NULL, funcs);

  /* 2: Stream's metatable, its methods as __index (lines: with the
   * iterator as upvalue) */
  luaL_newmetatable(L, STREAM_MT);
  lua_createtable(L, 0, 4);
  luaL_register(L, NULL, stream_methods);
  lua_pushcfunction(L, l_lines_iter);
  lua_pushcclosure(L, l_stream_lines, 1);
  lua_setfield(L, -2, "lines");
  lua_setfield(L, 2, "__index");
  lua_pushcfunction(L, l_stream_gc);
  lua_setfield(L, 2, "__gc");

  /* 3: Child's metatable, __index for its fields and methods */
  luaL_newmetatable(L, CHILD_MT);
  lua_createtable(L, 0, 2);
  luaL_register(L, NULL, child_methods);
  lua_pushcclosure(L, l_child_index, 1);
  lua_setfield(L, 3, "__index");
  lua_pushcfunction(L, l_child_gc);
  lua_setfield(L, 3, "__gc");

  /* poll, with both metatables */
  lua_pushcclosure(L, l_poll, 2);
  lua_setfield(L, 1, "poll");
  return 1;
}
