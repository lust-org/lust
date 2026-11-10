/*
 * lust.sys.term: what a command line program needs to know about its
 * terminal.
 *
 *   local term = require "lust.sys.term"
 *   if term.isatty(1) then ... colors ... end
 *   local cols, rows = term.size(1)  --> 80, 24 | nil, err, "ENOTTY"
 *
 * Both take a file descriptor: 0, 1, 2 for stdin, stdout, stderr.
 */
#define _GNU_SOURCE /* strerrorname_np (lust_errno.h) */

#include <errno.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "lust.h"
#include "lust_errno.h"

/* A file descriptor argument: an integer >= 0. */
static int check_fd(lua_State *L, int arg) {
  lua_Number n = luaL_checknumber(L, arg);
  if (!(n >= 0 && n <= INT_MAX && n == (lua_Number)(int)n))
    luaL_argerror(L, arg, "must be a file descriptor (an integer >= 0)");
  return (int)n;
}

static int l_isatty(lua_State *L) {
  lua_pushboolean(L, isatty(check_fd(L, 1)));
  return 1;
}

/* The size the terminal reports; 0 for what it doesn't know (a pty whose
 * size was never set). */
static int l_size(lua_State *L) {
  int fd = check_fd(L, 1);
  struct winsize ws;
  if (ioctl(fd, TIOCGWINSZ, &ws) != 0) {
    int e = errno, c;
    const char *rest = lust_strerror(e, &c);
    lua_pushfstring(L, "fd %d: %c%s", fd, c, rest);
    return lust_errno_result(L, e);
  }
  lua_pushinteger(L, ws.ws_col);
  lua_pushinteger(L, ws.ws_row);
  return 2;
}

static const luaL_Reg funcs[] = {
    {"isatty", l_isatty},
    {"size", l_size},
    {NULL, NULL},
};

LUST_API int luaopen_lust_sys_term(lua_State *L) {
  lua_createtable(L, 0, sizeof funcs / sizeof funcs[0] - 1);
  luaL_register(L, NULL, funcs);
  return 1;
}
