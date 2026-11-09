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

Libraries come in two kinds (`lib_kind` in their `meson.build`):

- `capi`: classic Lua C API module (`lust.crc`, `lust.xxh`).
- `ffi`: [static FFI bindings](https://evo-lua.github.io/docs/background-information/luajit/static-ffi-bindings/).
  The C code is plain C that knows nothing about Lua and just exports
  functions. A Lua file binds them with `ffi.cdef`; it is compiled to
  bytecode at build time, embedded, and only loaded on `require`
  (`lust.xxhffi`). The FFI calls are visible to the JIT, which is the main
  point. A system library is best bound with `ffi.load()`: it is then
  `dlopen`ed on `require` only, so it can live inside `lust` for free.
  The embedded bytecode must match the LuaJIT that loads it (version and
  GC64), otherwise `require` fails with a clear error.

## Building

Requirements: `meson`, `ninja`, a C compiler, `make`, `git`, `python3`
and the dev files of the dynamic dependencies (currently zlib).

```sh
# Arch Linux
sudo pacman -S --needed meson ninja gcc make git python zlib
# Ubuntu
sudo apt install meson ninja-build gcc make git python3 zlib1g-dev

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

- `libs=crc,xxh,xxhffi`: build only these libraries (default: all). This also
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
libs/ffi_shim.c     loads the embedded bytecode of the `ffi` libraries
libs/lust/          defines the build targets of all libraries
src/                the `lust` binary (lust.c + build of LuaJIT)
tools/              helper used by the meson build
tests/              test harness + cross library tests
bench/              startup / steady state benchmark (`./dev.py bench`)
vendor/luajit/      LuaJIT (managed by vendor.py, never edit)
vendor/xxhash/      xxHash (managed by vendor.py, never edit)
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
   - `test.lua` (see `libs/crc/test.lua`).
   - a library with a dynamic dependency: write it as `ffi` with
     `ffi.load()`, or set `lib_embed = false` (`.so` only, loaded by
     `lust` on demand). `tests/test_startup.py` guards `lust`'s startup.
2. Add `<name>` to `all_libs` in `libs/meson.build`.
3. For a static dependency, add it to `vendor/vendor.toml`, vendor it
   with `vendor.py` and declare a `<dep>_dep` for it in
   `vendor/meson.build` (see `xxhash_dep`). Never add files inside
   `vendor/<dep>/`.

The `.so`, the static archive for `lust`, install rules and both flavours
of the tests are derived automatically.

## Benchmarking

`./dev.py bench` builds everything and compares `lust.xxh` (C API) with
`lust.xxhffi` (FFI), each as a `.so` in a plain luajit and linked into
`lust`, pinned to CPU 0 (`--cpu N` to change; `--libs a,b` to compare other
libraries with the same API; `--only startup|throughput`):

- **startup**: the interpreter is spawned hundreds of times with
  `-e 'require "lust.<lib>"'`, interleaved across variants, and the cost
  of the require is the difference to an empty chunk. User space
  instruction counts (via `perf`, if usable) are noise free.
- **steady state**: `bench/throughput.lua` hashes 8 B to 64 KiB inputs;
  several processes per variant, because JIT trace layout varies by run.

For stable numbers use the `performance` cpufreq governor
(`sudo cpupower frequency-set -g performance`), disable turbo/boost, close
other load, or isolate the CPU (`isolcpus=0` on the kernel command line).
The plain luajit used is the vendored build (`build/vendor/luajit`), so
that both sides run the same VM binary; `--luajit` selects another one.
