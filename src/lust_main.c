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
 * options, from its static print_usage(), which cannot be replaced from the
 * outside. The definition and the one call share a name, but differ in
 * their argument list, which this function-like macro tells apart:
 *   definition  static void print_usage(void)  ->  luajit_print_usage ()
 *   call        print_usage();                 ->  lust_print_usage ();
 * So the call goes to lust_print_usage() (below), which prints lust's own
 * option list (with `-h|--help`) and, for `--help`, the documentation of
 * the libraries. The original stays defined but unused. If a future LuaJIT
 * changes the shape of either, or its option list, the build breaks or the
 * test of the usage text fails (tests/test_usage.py compares the options
 * against the vendored luajit). */
#include <stdio.h>
#include <string.h>

#include "lust_help.h"

static int lust_full_help; /* `lust --help`: also document the libraries */

static void lust_print_usage(void);
#define LUST_USAGE_void __attribute__((unused)) luajit_print_usage
#define LUST_USAGE_ lust_print_usage
#define print_usage(arg) LUST_USAGE_##arg()

/* luajit.c's main(), under another name: ours wraps it. */
#define main luajit_main
/*
 * The interactive banner is followed by the "JIT: ON SSE3 ..." line, printed
 * by the static print_jit_status(lua_State *L) of luajit.c, which is both
 * defined and called (as print_jit_status(L)) in that file. To drop the line
 * without editing it, print_jit_status is made a function-like macro that
 * looks at its argument: the definition's argument is `lua_State *L` and is
 * left alone (expands back to the original, which the preprocessor does not
 * expand recursively), the calls' argument is exactly `L` and expands to
 * nothing. The test pastes the argument onto LUST_PROBE_, which is only a
 * defined macro for the call form `L`.
 */
#define LUST_CAT_(a, b) a##b
#define LUST_CAT(a, b) LUST_CAT_(a, b)
#define LUST_PICK(a, b, ...) b
#define LUST_PICK_(...) LUST_PICK(__VA_ARGS__)
#define LUST_PROBE_L ~, CALL
#define LUST_JS_CALL(x) ((void)0)
#define LUST_JS_DECL(x) print_jit_status(x)
#define print_jit_status(x)                                                   \
  LUST_CAT(LUST_JS_, LUST_PICK_(LUST_PROBE_##x, DECL))(x)

#include "luajit.c"
#undef main

/* The options of luajit.c (in the same words, plus `-h|--help`), then for
 * `--help` the libraries: the doc-shortform.txt of each of them, collected
 * at build time by tools/gen-help.py. `progname` is luajit.c's (argv[0]). */
static void lust_print_usage(void) {
  fputs("usage: ", stderr);
  fputs(progname, stderr);
  fputs(" [options]... [script [args]...].\n"
        "\n"
        "OPTIONS\n"
        "  -e chunk  Execute string 'chunk'.\n"
        "  -l name   Require library 'name'.\n"
        "  -b ...    Save or list bytecode.\n"
        "  -j cmd    Perform LuaJIT control command.\n"
        "  -O[opt]   Control LuaJIT optimizations.\n"
        "  -i        Enter interactive mode after executing 'script'.\n"
        "  -v        Show version information.\n"
        "  -E        Ignore environment variables.\n"
        "  -h|--help Show this help text\n"
        "  --        Stop handling options.\n"
        "  -         Execute stdin and stop handling options.\n",
        stderr);
  if (lust_full_help)
    fputs("\nLIBRARIES\n" LUST_HELP_LIBRARIES, stderr);
  fflush(stderr);
}

/* `-h` and `--help` as the first argument are recognized here (to luajit.c
 * they are just invalid options, which print the usage); asking for help is
 * not an error, so they exit successfully. Only `--help` adds the
 * libraries. */
int main(int argc, char **argv) {
  int help = argc > 1 && (strcmp(argv[1], "-h") == 0 ||
                          strcmp(argv[1], "--help") == 0);
  lust_full_help = help && strcmp(argv[1], "--help") == 0;
  int status = luajit_main(argc, argv);
  return help ? 0 : status;
}
