/*
 * main() of the `lust` interpreter: the stock, unmodified upstream luajit.c
 * with the version banner replaced.
 *
 * luajit.c prints LUAJIT_VERSION in its banner (and for -v). The macro comes
 * from luajit.h, which luajit.c includes behind an include guard, so
 * including it first and redefining the macro here is enough; the vendored
 * tree is not touched. LUAJIT_VERSION is only used for that banner (the
 * linker enforced version check uses LUAJIT_VERSION_SYM, and jit.version
 * still reports the LuaJIT version).
 *
 * LUST_VERSION is the lust version, defined once in the top level
 * meson.build (project version) and passed in by src/meson.build.
 */
/* luajit.c defines this before any Lua header: luaconf.h keys the
 * interpreter-only settings (prompts, ...) on it. */
#define luajit_c

#include "luajit.h"

#ifndef LUST_VERSION
#error "LUST_VERSION must be defined by the build system"
#endif

/* print_version() in luajit.c does
 *   fputs(LUAJIT_VERSION " -- " LUAJIT_COPYRIGHT ". " LUAJIT_URL "\n", stdout)
 * The format cannot be changed through the macros alone (the " -- " and ". "
 * separators are literals). So LUAJIT_VERSION holds the complete lust banner
 * followed by an explicit NUL: fputs() stops there and the rest of the
 * (concatenated) upstream format string is never printed. The LuaJIT version
 * is still available as jit.version.
 */
#define LUST_URL "https://github.com/lust-org/lust"
#undef LUAJIT_VERSION
#define LUAJIT_VERSION "LuSt " LUST_VERSION " -- " LUST_URL "\n\0"

/* The usage text. luajit.c prints it (to stderr) when it is given invalid
 * options (`-h` among them), from its static print_usage(), which cannot
 * be replaced from the outside. It is also not possible to simply wrap it,
 * as the definition and the one call share a name. They only differ in
 * their argument list though, which this function-like macro tells apart:
 *   definition  static void print_usage(void)  ->  luajit_print_usage ()
 *   call        print_usage();                 ->  lust_print_usage ();
 * So the original keeps printing the options, and lust_print_usage()
 * (below) adds to it: a pointer to `--help` or, for `--help` itself, the
 * documentation of the libraries. If a future LuaJIT changes the shape of
 * either, the build breaks or the test of the usage text fails
 * (tests/test_usage.py). */
#include <stdio.h>
#include <string.h>

#include "lust_help.h"

static int lust_full_help; /* `lust --help`: also document the libraries */

static void lust_print_usage(void);
#define LUST_USAGE_void luajit_print_usage
#define LUST_USAGE_ lust_print_usage
#define print_usage(arg) LUST_USAGE_##arg()

/* luajit.c's main(), under another name: ours wraps it. */
#define main luajit_main
#include "luajit.c"
#undef main

/* The options of luajit.c, then either a pointer to `--help` or the
 * libraries: the doc-shortform.txt of each of them, collected at build time
 * by tools/gen-help.py. */
static void lust_print_usage(void) {
  luajit_print_usage();
  if (lust_full_help)
    fputs("\nLIBRARIES\n" LUST_HELP_LIBRARIES, stderr);
  else
    fputs("\nRun with --help to list the libraries and their API.\n", stderr);
  fflush(stderr);
}

/* `--help` as the first argument is recognized here (to luajit.c it is just
 * another invalid option, which prints the usage); asking for help is not
 * an error, so it exits successfully. */
int main(int argc, char **argv) {
  lust_full_help = argc > 1 && strcmp(argv[1], "--help") == 0;
  int status = luajit_main(argc, argv);
  return lust_full_help ? 0 : status;
}
