/*
 * lust.sys.proc: running programs without a shell.
 *
 *   local proc = require "lust.sys.proc"
 *   local status, out, err = proc.run { "git", "status", "--short" }
 *   proc.run({ "sort" }, { input = "b\na\n" })        --> 0, "a\nb\n", ""
 *   proc.run({ "make" }, { stdout = "inherit", cwd = "src" })
 *   proc.run { "no-such-program" } --> nil, "no-such-program: ...", "ENOENT"
 *
 * The arguments go to the program as they are (os.execute and io.popen
 * pass a command line to sh: quoting is up to the caller, and getting it
 * wrong runs something else). The program is searched in PATH unless it
 * contains a "/".
 *
 * posix_spawn starts it (glibc: clone(CLONE_VM|CLONE_VFORK), no copy of
 * the address space, the parent learns whether the exec failed). The
 * child gets no descriptors but 0, 1, 2 (files io.open opened are not
 * inherited). Then one poll loop feeds the input and reads stdout and
 * stderr as they come: writing all input first could deadlock against a
 * child that blocks writing output nobody reads yet.
 *
 * Failing to start the program (not found, not executable, no such cwd,
 * out of descriptors) returns nil, "<program>: <reason>", "<errno name>"
 * (like lust.fs). Misuse raises, see docs/error-handling.md.
 */
#define _GNU_SOURCE /* pipe2, strerrorname_np, sigabbrev_np, posix_spawn_file_actions_*_np */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "lust.h"
#include "lust_errno.h"

extern char **environ;

/* The metatable of Run (upvalue of run). */
#define RUN_MT_UV lua_upvalueindex(1)

/* What happens to the child's stdout or stderr (opts.stdout/stderr). */
enum { OUT_CAPTURE, OUT_INHERIT, OUT_NULL };

/* Capturing starts with a buffer this large and doubles it as needed
 * (a pipe holds 64 KiB, so reads are at most that large anyway). */
#define BUF_INIT 16384

/* The child's descriptors 0, 1, 2 (index into Run's arrays). */
enum { IN, OUT, ERR, NFD };

/*
 * One run, a userdata: owns what a run holds while it may raise (out of
 * memory), freed by run_cleanup at its end, or by __gc. A run that is
 * aborted that way kills and reaps its child.
 */
typedef struct {
  pid_t pid;       /* > 0: started, not reaped yet */
  int fd[NFD];     /* our ends of the pipes, -1 if none */
  int child[NFD];  /* the child's ends, until it is started; -1 if none */
  char *buf[NFD];  /* captured output (OUT, ERR) */
  size_t len[NFD], cap[NFD];
} Run;

typedef struct {
  const char *cwd;  /* NULL: ours */
  int env;          /* stack index of opts.env, 0: none */
  const char *input;
  size_t input_len;
  int has_input;
  int out[NFD];     /* OUT_* of OUT and ERR */
} Opts;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void close_fd(int *fd) {
  if (*fd >= 0) close(*fd);
  *fd = -1;
}

/* Frees everything; kills and reaps a child that is still there. */
static void run_cleanup(Run *r) {
  for (int i = 0; i < NFD; i++) {
    close_fd(&r->fd[i]);
    close_fd(&r->child[i]);
    free(r->buf[i]);
    r->buf[i] = NULL;
  }
  if (r->pid > 0) {
    kill(r->pid, SIGKILL);
    while (waitpid(r->pid, NULL, 0) < 0 && errno == EINTR) {}
    r->pid = 0;
  }
}

static int l_run_gc(lua_State *L) {
  run_cleanup(lua_touserdata(L, 1));
  return 0;
}

/* A string without zero bytes (the kernel would cut it short there). */
static int is_c_string(lua_State *L, int idx) {
  size_t len;
  const char *s;
  if (lua_type(L, idx) != LUA_TSTRING) return 0;
  s = lua_tolstring(L, idx, &len);
  return strlen(s) == len;
}

/* ------------------------------------------------------------------ */
/* arguments                                                           */
/* ------------------------------------------------------------------ */

/* argv as a NULL terminated array (in a userdata; the strings stay
 * referenced by the table). */
static char **check_argv(lua_State *L, int arg) {
  size_t n;
  char **argv;
  luaL_checktype(L, arg, LUA_TTABLE);
  n = lua_objlen(L, arg);
  if (n == 0) luaL_argerror(L, arg, "argv is empty");
  argv = lua_newuserdata(L, (n + 1) * sizeof *argv);
  for (size_t i = 0; i < n; i++) {
    lua_rawgeti(L, arg, (int)i + 1);
    if (!is_c_string(L, -1))
      luaL_argerror(L, arg, lua_pushfstring(L, "argv[%d] must be a string without zero bytes", (int)i + 1));
    argv[i] = (char *)lua_tostring(L, -1);
    lua_pop(L, 1);
  }
  argv[n] = NULL;
  return argv;
}

static int bad_opt(lua_State *L, int arg, const char *msg, const char *key) {
  return luaL_argerror(L, arg, lua_pushfstring(L, msg, key));
}

static int check_out(lua_State *L, int arg, const char *key) {
  const char *s = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
  if (strcmp(s, "capture") == 0) return OUT_CAPTURE;
  if (strcmp(s, "inherit") == 0) return OUT_INHERIT;
  if (strcmp(s, "null") == 0) return OUT_NULL;
  return bad_opt(L, arg, "%s must be 'capture', 'inherit' or 'null'", key);
}

static void check_opts(lua_State *L, int arg, Opts *o) {
  memset(o, 0, sizeof *o);
  if (lua_isnoneornil(L, arg)) return;
  luaL_checktype(L, arg, LUA_TTABLE);
  lua_pushnil(L);
  while (lua_next(L, arg)) {
    const char *k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "?";
    if (strcmp(k, "cwd") == 0) {
      if (!is_c_string(L, -1)) bad_opt(L, arg, "%s must be a string without zero bytes", k);
      o->cwd = lua_tostring(L, -1);
    } else if (strcmp(k, "env") == 0) {
      if (!lua_istable(L, -1)) bad_opt(L, arg, "%s must be a table", k);
      o->env = 1; /* (the index is taken below, from opts itself) */
    } else if (strcmp(k, "input") == 0) {
      if (lua_type(L, -1) != LUA_TSTRING) bad_opt(L, arg, "%s must be a string", k);
      o->input = lua_tolstring(L, -1, &o->input_len);
      o->has_input = 1;
    } else if (strcmp(k, "stdout") == 0) {
      o->out[OUT] = check_out(L, arg, k);
    } else if (strcmp(k, "stderr") == 0) {
      o->out[ERR] = check_out(L, arg, k);
    } else {
      bad_opt(L, arg, "unknown option '%s'", k);
    }
    lua_pop(L, 1);
  }
  /* (cwd and input stay referenced by opts) */
  if (o->env) {
    lua_getfield(L, arg, "env");
    o->env = lua_gettop(L);
  }
}

/*
 * The environment for the child: ours, with the variables of opts.env
 * (at index env: name = value or false to remove) replacing ours. A NULL
 * terminated array in a userdata; the "name=value" strings it adds are
 * kept in a table, both left on the stack.
 */
static char **build_env(lua_State *L, int arg, int env) {
  size_t n = 0, i = 0;
  char **envp;
  int strs;
  for (char **e = environ; *e; e++) n++;
  /* (an upper bound: ours plus all of opts.env) */
  lua_pushnil(L);
  while (lua_next(L, env)) {
    if (!is_c_string(L, -2) || lua_objlen(L, -2) == 0 || strchr(lua_tostring(L, -2), '='))
      luaL_argerror(L, arg, "env names must be non-empty strings without '=' and zero bytes");
    if (!is_c_string(L, -1) && !(lua_type(L, -1) == LUA_TBOOLEAN && !lua_toboolean(L, -1)))
      luaL_argerror(L, arg, lua_pushfstring(L, "env.%s must be a string without zero bytes or false",
                                            lua_tostring(L, -2)));
    lua_pop(L, 1);
    n++;
  }
  envp = lua_newuserdata(L, (n + 1) * sizeof *envp);
  lua_newtable(L);
  strs = lua_gettop(L);
  /* ours, unless replaced */
  for (char **e = environ; *e; e++) {
    const char *eq = strchr(*e, '=');
    if (eq == NULL) continue; /* (malformed: nothing to pass on) */
    lua_pushlstring(L, *e, (size_t)(eq - *e));
    lua_rawget(L, env);
    if (lua_isnil(L, -1)) envp[i++] = *e;
    lua_pop(L, 1);
  }
  /* the ones set */
  lua_pushnil(L);
  while (lua_next(L, env)) {
    if (lua_type(L, -1) == LUA_TSTRING) {
      lua_pushfstring(L, "%s=%s", lua_tostring(L, -2), lua_tostring(L, -1));
      envp[i++] = (char *)lua_tostring(L, -1);
      lua_rawseti(L, strs, (int)i);
    }
    lua_pop(L, 1);
  }
  envp[i] = NULL;
  return envp;
}

/* ------------------------------------------------------------------ */
/* starting the child                                                  */
/* ------------------------------------------------------------------ */

/*
 * Creates the pipes and starts the child. Returns 0, or the errno of what
 * failed (r is cleaned up by the caller).
 */
static int start(Run *r, char **argv, char **envp, const Opts *o) {
  posix_spawn_file_actions_t fa;
  int e;
  if (o->has_input) {
    int p[2];
    if (pipe2(p, O_CLOEXEC) != 0) return errno;
    r->child[IN] = p[0];
    r->fd[IN] = p[1];
    /* (written as the child reads, see io_loop) */
    if (fcntl(r->fd[IN], F_SETFL, O_NONBLOCK) != 0) return errno;
  }
  for (int i = OUT; i <= ERR; i++) {
    int p[2];
    if (o->out[i] != OUT_CAPTURE) continue;
    if (pipe2(p, O_CLOEXEC) != 0) return errno;
    r->fd[i] = p[0];
    r->child[i] = p[1];
  }

  /* What the child does before the exec (each step only if all before
   * succeeded, e: the first error). */
  if ((e = posix_spawn_file_actions_init(&fa)) != 0) return e;
  for (int i = IN; i <= ERR && e == 0; i++) {
    if (r->child[i] >= 0) /* (dup2 clears O_CLOEXEC on the copy) */
      e = posix_spawn_file_actions_adddup2(&fa, r->child[i], i);
    else if (i != IN && o->out[i] == OUT_NULL)
      e = posix_spawn_file_actions_addopen(&fa, i, "/dev/null", O_WRONLY, 0);
  }
  /* (everything above 2: the pipes and whatever else is open) */
  if (e == 0) e = posix_spawn_file_actions_addclosefrom_np(&fa, 3);
  if (e == 0 && o->cwd) e = posix_spawn_file_actions_addchdir_np(&fa, o->cwd);
  if (e == 0) e = posix_spawnp(&r->pid, argv[0], &fa, NULL, argv, envp ? envp : environ);
  posix_spawn_file_actions_destroy(&fa);
  if (e != 0) {
    r->pid = 0;
    return e;
  }
  for (int i = IN; i <= ERR; i++) close_fd(&r->child[i]);
  return 0;
}

/* ------------------------------------------------------------------ */
/* talking to the child                                                */
/* ------------------------------------------------------------------ */

/* Reads what is available from r->fd[i] into its buffer; closes it at
 * the end. Returns 0, or -1 if out of memory. */
static int read_some(Run *r, int i) {
  ssize_t got;
  if (r->cap[i] - r->len[i] < BUF_INIT / 4) {
    size_t cap = r->cap[i] ? r->cap[i] * 2 : BUF_INIT;
    char *b = realloc(r->buf[i], cap);
    if (b == NULL) return -1;
    r->buf[i] = b;
    r->cap[i] = cap;
  }
  got = read(r->fd[i], r->buf[i] + r->len[i], r->cap[i] - r->len[i]);
  if (got > 0) r->len[i] += (size_t)got;
  else if (got == 0 || errno != EINTR) close_fd(&r->fd[i]); /* (end, or broken) */
  return 0;
}

/*
 * Feeds the input and reads the output until the child closed both
 * (normally: exited). Returns 0, or -1 if out of memory.
 *
 * Writing to a child that exited (or closed stdin) raises SIGPIPE, which
 * would kill us: it is blocked meanwhile, and one we caused is taken back
 * before unblocking (unless one was pending already).
 */
static int io_loop(Run *r, const Opts *o) {
  struct pollfd pfd[NFD];
  int which[NFD]; /* IN, OUT or ERR of pfd[k] */
  size_t off = 0;
  int status = 0, epipe = 0, was_pending = 0;
  sigset_t pipe_set, old;
  if (r->fd[IN] >= 0) {
    sigset_t pend;
    sigemptyset(&pipe_set);
    sigaddset(&pipe_set, SIGPIPE);
    sigpending(&pend);
    was_pending = sigismember(&pend, SIGPIPE);
    sigprocmask(SIG_BLOCK, &pipe_set, &old);
    if (o->input_len == 0) close_fd(&r->fd[IN]);
  }
  for (;;) {
    int n = 0;
    for (int i = IN; i <= ERR; i++) {
      if (r->fd[i] < 0) continue;
      pfd[n].fd = r->fd[i];
      pfd[n].events = i == IN ? POLLOUT : POLLIN;
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
        if (read_some(r, i) != 0) {
          status = -1;
          goto out;
        }
      } else {
        ssize_t put = write(r->fd[IN], o->input + off, o->input_len - off);
        if (put > 0) off += (size_t)put;
        else if (errno == EPIPE) epipe = 1;
        if (off == o->input_len || (put < 0 && errno != EAGAIN && errno != EINTR))
          close_fd(&r->fd[IN]); /* (all written, or the child doesn't read) */
      }
    }
  }
out:
  if (o->has_input) {
    if (epipe && !was_pending) {
      struct timespec zero = {0, 0};
      while (sigtimedwait(&pipe_set, NULL, &zero) < 0 && errno == EINTR) {}
    }
    sigprocmask(SIG_SETMASK, &old, NULL);
  }
  return status;
}

/* ------------------------------------------------------------------ */
/* run                                                                 */
/* ------------------------------------------------------------------ */

/* Pushes how the child ended: its exit code, or the name of the signal
 * that killed it. */
static void push_status(lua_State *L, int ws) {
  if (WIFEXITED(ws)) {
    lua_pushinteger(L, WEXITSTATUS(ws));
  } else {
    int sig = WTERMSIG(ws);
    const char *name = sigabbrev_np(sig);
    if (name != NULL) lua_pushfstring(L, "SIG%s", name);
    else lua_pushfstring(L, "SIGRTMIN+%d", sig - SIGRTMIN); /* (the only ones without) */
  }
}

static int l_run(lua_State *L) {
  Opts o;
  char **argv, **envp = NULL;
  Run *r;
  int e, ws;
  lua_settop(L, 2); /* (so what is pushed below isn't taken for opts) */
  argv = check_argv(L, 1);
  check_opts(L, 2, &o);
  if (o.env) envp = build_env(L, 2, o.env);

  r = lua_newuserdata(L, sizeof *r);
  memset(r, 0, sizeof *r);
  for (int i = 0; i < NFD; i++) r->fd[i] = r->child[i] = -1;
  lua_pushvalue(L, RUN_MT_UV);
  lua_setmetatable(L, -2);

  if ((e = start(r, argv, envp, &o)) != 0) {
    run_cleanup(r);
    if (o.cwd) {
      int c;
      const char *rest = lust_strerror(e, &c);
      lua_pushfstring(L, "%s (in %s): %c%s", argv[0], o.cwd, c, rest);
      return lust_errno_result(L, e);
    }
    return lust_push_errno(L, argv[0], e);
  }
  if (io_loop(r, &o) != 0) {
    run_cleanup(r);
    return luaL_error(L, "not enough memory");
  }
  while (waitpid(r->pid, &ws, 0) < 0) {
    /* (EINTR: a signal, ECHILD: someone else reaped it, can't happen
     * unless SIGCHLD is ignored) */
    if (errno != EINTR) {
      r->pid = 0;
      return lust_push_errno(L, argv[0], errno);
    }
  }
  r->pid = 0;

  push_status(L, ws);
  for (int i = OUT; i <= ERR; i++) {
    if (o.out[i] == OUT_CAPTURE) lua_pushlstring(L, r->buf[i] ? r->buf[i] : "", r->len[i]);
    else lua_pushnil(L);
  }
  run_cleanup(r);
  return 3;
}

static const luaL_Reg funcs[] = {
    {"run", l_run},
    {NULL, NULL},
};

LUST_API int luaopen_lust_sys_proc(lua_State *L) {
  lua_createtable(L, 0, sizeof funcs / sizeof funcs[0] - 1);
  /* the metatable of Run, an upvalue of every function */
  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, l_run_gc);
  lua_setfield(L, -2, "__gc");
  luaL_openlib(L, NULL, funcs, 1);
  return 1;
}
