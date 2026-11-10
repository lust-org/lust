# Adding a library

1. Create `libs/<name>/` with:
   - for a `capi` library: `<name>.c` exporting
     `LUST_API int luaopen_lust_<name>(lua_State *L)`
     (`#include "lust.h"`); for an `ffi` library: the plain C sources
     (functions tagged `LUST_API`) and `<name>.lua`, see `libs/ffi_shim.c`,
   - `meson.build` setting `lib_sources` and `lib_deps` (and for `ffi`:
     `lib_kind = 'ffi'`, `lib_lua`); copy `libs/xxh/meson.build`
     (no `ffi` library exists at the moment, see `libs/ffi_shim.c`),
   - `test.lua` (see `libs/xxh/test.lua`),
   - `doc-shortform.txt`: the API as succinct as possible (format: see
     `tools/gen-help.py`, example: `libs/xxh/doc-shortform.txt`). It is
     part of the usage text of `lust` (`lust --help`, section `LIBRARIES`; plain `-h` lists only the options), and
     `tests/test_doc.lua` checks that it lists exactly what the module
     exports. The build fails without it,
   - `bench/*.lua`: benchmarks of what the library provides, and their
     `bench/baseline.txt` (see [Benchmark regressions](benchmark-regressions.md)),
   - optionally `examples/<name>.lua` (for a namespace: one
     `examples/<ns>.lua` for all of its libraries, e.g. `examples/sys.lua`).
   - a library with a dynamic dependency: write it as `ffi` with
     `ffi.load()`, or set `lib_embed = false` (`.so` only, loaded by
     `lust` on demand). `tests/test_startup.py` guards `lust`'s startup.
     C++ libraries need `lib_embed = false` too: the C++
     runtime runs static constructors at startup.
   - a `capi` library may have a Lua part too (`lib_lua`, for code that
     reads better in Lua): its bytecode is handed to the C code as
     `LUST_LUA_BC_H` to `#include`; load it lazily on first use like
     `libs/fs/fs.c` does, so `require` stays cheap and the C functions keep
     working on a stock luajit whose bytecode format differs,
   - the Lua part of a library must not contain table constructors
     with named fields (`{ a = 1 }`, `{ a = f() }`): the compiled bytecode
     would differ from build to build. Write `local t = {}; t.a = 1`
     (`tests/test_bytecode.py`).
   - errors follow the conventions in [Error handling](error-handling.md).
2. Add `<name>` to `all_libs` in `libs/meson.build`.

A library can be in a namespace: `sys.env` (`require "lust.sys.env"`)
lives in `libs/sys/env/`, exports `luaopen_lust_sys_env` and is built as
`lust/sys/env.so`. The namespace itself (`sys`) is no library. Names
contain no `_` (`sys_env` would clash with `sys.env`).
3. For a static dependency, add it to `vendor/vendor.toml`, vendor it
   with `vendor.py` and declare a `<dep>_dep` for it in
   `vendor/meson.build` (see `xxhash_dep`). Never add files inside
   `vendor/<dep>/`. For a dependency that has no meson build (RE2/Abseil,
   PCRE2) the source lists are kept in `vendor/meson.build`; after
   upgrading RE2/Abseil run `tools/absl-sources.py --check`, after
   upgrading PCRE2 compare the list with `vendor/pcre2/CMakeLists.txt`.

The `.so`, the static archive for `lust`, install rules and both flavours
of the tests are derived automatically.
