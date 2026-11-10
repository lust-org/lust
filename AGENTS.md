# AGENTS.md

C libraries for LuaJIT. Each builds as a `.so` for stock luajit and as a
static archive linked into the `lust` binary (LuaJIT + all libraries).

## Docs

`docs/` has one document per topic, named after it. Read one only
when its topic comes up; new project-wide conventions go there too.

## Constraints

- Never add, edit or remove files inside `vendor/*/**`. They are managed
  by `vendor.py` / `vendor/vendor.toml`. Build definitions for vendored
  code (e.g. `xxhash_dep`, `luajit_src`) go in `vendor/meson.build`.
- External dependencies are either dynamic (`dependency()`) or static
  from `vendor/`. Nothing is downloaded at build time.
- Build system is meson only, `meson_version: '>=1.3.0'` (Ubuntu 24.04).
  Do not use newer-only features (e.g. `build_subdir` needs 1.10).
- C11 for our code. Only upstream `luajit.c` is built as `gnu11`. The one
  exception is C++17 for code that wraps C++ dependencies (an `extern "C"`
  shim) and for vendored C++ code (RE2/Abseil, currently vendored but not
  used by any library). A C++ library links the C++ runtime statically
  (`cpp_rt_dep`) and is
  `lib_embed = false`: libstdc++'s static constructors (iostream, locale,
  exception pool) cost ~110k instructions per start.
- Prefer the Lua C API (`capi`, the default) over `ffi`: requiring an
  `ffi` library costs ~90k instructions more (FFI, `cdef`, bytecode,
  symbol lookups). Use `ffi` only where the JIT should pay off hugely:
  tiny, hot, allocation free calls (hashes, checksums), not work
  dominated by the library itself (regexps). LuaJIT specializes traces
  on the function object: never return a fresh C closure per call that
  ends up in a hot loop (a `gmatch` iterator), return a callable
  userdata instead (see `libs/pcre2`).
- `.so` modules must not link libluajit: only the LuaJIT headers are used,
  symbols resolve from the host interpreter. Keep `b_lundef=false`.
- Export only `luaopen_lust_<name>` (`LUST_API`); everything else hidden.
  `ffi` libraries (`lib_kind = 'ffi'`, Lua part compiled to bytecode by
  the vendored LuaJIT and embedded by `libs/ffi_shim.c`) additionally
  export the C functions their Lua part binds, and must keep them linked
  (they are only reached via dlsym).
- **Startup cost of `lust` must not grow with the number of libraries**
  (there will be many; every start pays for everything linked in, used or
  not). Therefore:
  - A library with a *dynamic* dependency must never put it in `lust`'s
    `NEEDED` list (the loader maps it on every start, ~60k instructions
    for libz alone). Either write it as an `ffi` library and
    `ffi.load()` the system library in its Lua part (lazy `dlopen` on
    `require`, no build-time dependency), or set `lib_embed = false` in
    its `meson.build`: it is then built as a `.so` only and `lust` loads
    it from its install `cmoddir`. Static deps from `vendor/` are fine.
  - Nothing may happen per library at startup: `src/lust.c` has one
    searcher over a static table, no `package.preload` entries, no init.
  - `tests/test_startup.py` enforces this (same `NEEDED` set as plain
    luajit, small instruction budget). Don't raise the budget to make a
    library fit; fix the library.
- Requiring one library must not load any other (`tests/test_lazy.lua`).
- Any Lua API call that creates an object (a string, table, userdata;
  `luaL_Buffer`, `luaL_error`) may run `__gc` finalizers, i.e. any Lua
  code, this library included; LuaJIT drops their errors, so reentrancy
  can't be refused. Across such a call, hold no unguarded state shared
  with other calls and no pointers into memory Lua code can change.
  Which calls, the patterns (`include/lust_scratch.h`) and the test
  helper (`T.during_gc`): `docs/gc-reentrancy.md`.
- Stock luajit finds `require "lust.<x>"` at `<cpath>/lust/<x>.so`, so
  module output names/dirs must keep that layout.
- The Lua part of a library (`lib_lua`: every `ffi` library, optionally
  a `capi` one) must not use table constructors with named fields:
  LuaJIT dumps them in a per process randomized order, so the embedded
  bytecode would not be reproducible (`tests/test_bytecode.py`).
- Do not modify the vendored LuaJIT tree. `tools/build-luajit.py` mirrors
  it into the build dir and builds there.
- Install locations must work on Arch and Ubuntu: use meson's `libdir`
  (differs per distro), keep `cmoddir` overridable.
- Builds are deterministic: a clean rebuild yields byte-identical `lust`,
  `.so` files, `libluajit.a` and `vmdef.lua`, regardless of build dir or
  checkout path. Do not embed timestamps, absolute paths, hostnames or
  other ambient state (`__DATE__`, `__TIME__`, `-g` paths, ...). The only
  stamp is LuaJIT's version, from the last commit touching
  `vendor/luajit`. Check by building twice (`cmp`) after build changes.
- `./dev.py` stays a thin shim over meson (`build`, `install`, `test`).

## Workflow

- `./dev.py test` must pass (each lib: shared + static + lazy tests, and
  its `bench/` instruction counts within 3% of `bench/baseline.txt`).
  Don't re-record a baseline to hide a regression: only for intended
  changes, committing the diff with them (see
  `docs/benchmark-regressions.md`). Every library needs `bench/*.lua`
  exercising its API.
- Errors: misuse and out of memory raise, bad input returns `nil, err`
  (unless `nil` is a normal result); see `docs/error-handling.md`.
- Commit each finished change separately; amend when asked to change a
  previous commit.
