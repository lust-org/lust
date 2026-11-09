# lust

A collection of C libraries for LuaJIT, with two ways to use each of them:

- **as a loadable module** (`lust/<name>.so`) for the stock `luajit`
  interpreter. Loading is a plain `dlopen`, and you only pay for the
  libraries you `require`.
- **statically linked into `lust`**, a standard LuaJIT interpreter with the
  libraries built in, so there is nothing to look up or `dlopen`. Startup
  does not grow with the number of libraries: they are found through one
  lookup table when required. Libraries with a dynamic dependency are not
  linked in (that would make every start map it): `lust` loads their `.so`
  from the install directory on demand, see "Adding a library".

```lua
local crc = require "lust.crc"
print(crc.crc32("123456789")) --> 3421780262
```

| library     | what                | dependency                              |
| ----------- | ------------------- | --------------------------------------- |
| `lust.crc`  | zlib checksums      | system zlib, linked dynamically (`.so` only, not in `lust`) |
| `lust.xxh`  | xxHash              | `vendor/xxhash`, linked statically      |
| `lust.xxhffi` | xxHash, same API  | as above, but bound through the FFI     |
| `lust.re2`  | RE2 regular expressions | `vendor/re2` + `vendor/abseil`, linked statically (`.so` only, not in `lust`) |
| `lust.pcre2` | PCRE2 (Perl compatible) regular expressions | `vendor/pcre2`, linked statically |

Libraries come in two kinds (`lib_kind` in their `meson.build`):

- `capi`: classic Lua C API module (`lust.crc`, `lust.xxh`, `lust.pcre2`).
  The default, and cheapest to `require`.
- `ffi`: [static FFI bindings](https://evo-lua.github.io/docs/background-information/luajit/static-ffi-bindings/).
  The C code is plain C that knows nothing about Lua and just exports
  functions. A Lua file binds them with `ffi.cdef`; it is compiled to
  bytecode at build time, embedded, and only loaded on `require`
  (`lust.xxhffi`). The FFI calls are visible to the JIT, which is the main
  point, but `require`ing one costs about 90k instructions more than a
  C API module, so use it only where the JIT pays off hugely (tiny, hot
  calls like hashing), not where the library itself dominates (regexp
  matching). A system library is best bound with `ffi.load()`: it is then
  `dlopen`ed on `require` only, so it can live inside `lust` for free.
  The embedded bytecode must match the LuaJIT that loads it (version and
  GC64), otherwise `require` fails with a clear error.

## Building

Requirements: `meson`, `ninja`, a C and a C++17 compiler (RE2), `make`,
`git`, `python3` and the dev files of the dynamic dependencies (currently
zlib).

```sh
# Arch Linux
sudo pacman -S --needed meson ninja gcc make git python zlib
# Ubuntu
sudo apt install meson ninja-build gcc g++ make git python3 zlib1g-dev

git clone <this repo> && cd lust
./dev.py build            # everything
./dev.py build lust       # only the `lust` binary  -> build/src/lust
./dev.py build libs       # all libraries as .so    -> build/libs/lust/*.so
./dev.py build crc        # a single library
./dev.py test
```

`dev.py` only shims `meson`; anything it does you can do by hand
(`meson setup build && meson compile -C build && meson test -C build`).
Meson options (`./dev.py build -D name=value`, see `meson_options.txt`):

- `libs=crc,xxh,xxhffi,re2,pcre2`: build only these libraries (default: all). This also
  decides which libraries get linked into `lust`.
- `lust=disabled`: don't build the `lust` binary (skips building LuaJIT).
- `cmoddir=...`: where the `.so` files go (default `<libdir>/lua/5.1`).
- `luajit_xcflags=...`: extra CFLAGS for the LuaJIT inside `lust`,
  e.g. `-DLUAJIT_ENABLE_LUA52COMPAT`.

Using the build tree without installing:

```sh
LUA_CPATH="$PWD/build/libs/?.so" luajit -e 'print(require("lust.xxh").xxh64("abc"))'
build/src/lust -e 'print(require("lust.xxh").xxh64("abc"))'
```

`examples/*.lua` show each library and run with the built `lust`:

```sh
build/src/lust examples/xxh.lua
build/src/lust examples/xxhffi.lua
build/src/lust examples/pcre2.lua
# crc and re2 are not linked into `lust`, so they are looked up in the
# build tree:
LUA_CPATH="$PWD/build/libs/?.so" build/src/lust examples/crc.lua
LUA_CPATH="$PWD/build/libs/?.so" build/src/lust examples/re2.lua
```

## Installing

Like `make install`; targets are `all` (default), `lust`, `libs` or a
library name. Needs root for system prefixes (meson will ask via
sudo/pkexec).

```sh
./dev.py install                        # to /usr/local
./dev.py install lust crc --prefix /usr
./dev.py install --prefix /usr --destdir pkg   # staging, for packaging
```

| what                         | where                                   |
| ---------------------------- | --------------------------------------- |
| `lust`                       | `<prefix>/bin/lust`                     |
| `lust/<name>.so`             | `<libdir>/lua/5.1/lust/<name>.so`       |
| `jit.*` Lua modules (`-jv`…) | `<prefix>/share/lust/jit/`              |

`<libdir>` is picked by meson for the distribution: `lib` on Arch,
`lib/x86_64-linux-gnu` on Ubuntu. `<libdir>/lua/5.1` is on the default
`package.cpath` of the distributions' `luajit` packages. If your luajit
doesn't search there, point it to the right place with `-D cmoddir=...` or
`LUA_CPATH`.

## Layout

```
dev.py              build / test / install shim around meson
meson.build         project, ties everything together
meson_options.txt
include/lust.h      header shared by all libraries
libs/<name>/        one directory per library: <name>.c, test.lua, meson.build
libs/<name>/bench/  its benchmarks + their baseline.txt (instruction counts)
libs/ffi_shim.c     loads the embedded bytecode of the `ffi` libraries
libs/lust/          defines the build targets of all libraries
src/                the `lust` binary (lust.c + build of LuaJIT)
examples/           runnable examples, one per library
tools/              helper used by the meson build
tools/absl-sources.py  checks the Abseil source list of vendor/meson.build
tests/              test harness + cross library tests
vendor/luajit/      LuaJIT (managed by vendor.py, never edit)
vendor/xxhash/      xxHash (managed by vendor.py, never edit)
vendor/re2/         RE2 (managed by vendor.py, never edit)
vendor/abseil/      Abseil, which RE2 needs (managed by vendor.py, never edit)
vendor/pcre2/       PCRE2 (managed by vendor.py, never edit)
vendor/meson.build  build definitions of the vendored dependencies
vendor/vendor.toml  upstream repos + revisions of the above
```

External dependencies are either **dynamic** (a normal `dependency()`
resolved through pkg-config; they stay shared libraries at runtime), or
**static**, from `vendor/`. Nothing is ever downloaded at build time.

## Adding a library

1. Create `libs/<name>/` with:
   - for a `capi` library: `<name>.c` exporting
     `LUST_API int luaopen_lust_<name>(lua_State *L)`
     (`#include "lust.h"`); for an `ffi` library: the plain C sources
     (functions tagged `LUST_API`) and `<name>.lua`, see `libs/xxhffi`,
   - `meson.build` setting `lib_sources` and `lib_deps` (and for `ffi`:
     `lib_kind = 'ffi'`, `lib_lua`); copy `libs/crc/meson.build` or
     `libs/xxhffi/meson.build`,
   - `test.lua` (see `libs/crc/test.lua`),
   - `doc-shortform.txt`: the API as succinct as possible (format: see
     `tools/gen-help.py`, example: `libs/xxh/doc-shortform.txt`). It is
     part of the usage text of `lust` (`lust --help`, section `LIBRARIES`; plain `-h` only points to it), and
     `tests/test_doc.lua` checks that it lists exactly what the module
     exports. The build fails without it,
   - `bench/*.lua`: benchmarks of what the library provides, and their
     `bench/baseline.txt` (see "Benchmark regressions"),
   - optionally `examples/<name>.lua`.
   - a library with a dynamic dependency: write it as `ffi` with
     `ffi.load()`, or set `lib_embed = false` (`.so` only, loaded by
     `lust` on demand). `tests/test_startup.py` guards `lust`'s startup.
     C++ libraries (`libs/re2`) need `lib_embed = false` too: the C++
     runtime runs static constructors at startup.
   - the Lua part of an `ffi` library must not contain table constructors
     with named fields (`{ a = 1 }`, `{ a = f() }`): the compiled bytecode
     would differ from build to build. Write `local t = {}; t.a = 1`
     (`tests/test_bytecode.py`).
2. Add `<name>` to `all_libs` in `libs/meson.build`.
3. For a static dependency, add it to `vendor/vendor.toml`, vendor it
   with `vendor.py` and declare a `<dep>_dep` for it in
   `vendor/meson.build` (see `xxhash_dep`). Never add files inside
   `vendor/<dep>/`. For a dependency that has no meson build (RE2/Abseil,
   PCRE2) the source lists are kept in `vendor/meson.build`; after
   upgrading RE2/Abseil run `tools/absl-sources.py --check`, after
   upgrading PCRE2 compare the list with `vendor/pcre2/CMakeLists.txt`.

The `.so`, the static archive for `lust`, install rules and both flavours
of the tests are derived automatically.

## Benchmark regressions

Every library has benchmarks in `libs/<name>/bench/*.lua` (`_*.lua` are
helpers): a fixed amount of work each (a few ms), asserting its result.
`./dev.py test` runs each one in both flavours (vendored luajit + `.so`,
and `lust`) and fails if its user space instructions (`perf stat -e
instructions:u`, median of 3) differ more than 3% from
`libs/<name>/bench/baseline.txt`. Instructions are free of timing noise
(repeated runs differ well below 1%, whatever the CPU or its core type),
so this works on a loaded machine, and all runs go in parallel, each
process pinned to a CPU of its own. Without a usable `perf` only the
results are checked; non release or sanitizer builds don't compare.

The counts depend on the toolchain and system libraries. After an
intended change (faster counts too, so the baseline stays tight), for a
new library, or on another system, record them anew and commit the diff
(counts within 1% of the recorded ones are kept, so noise doesn't churn
the baselines):

```sh
./dev.py test --suite bench --test-args=--update
```
