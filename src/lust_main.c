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

#include "luajit.c"
