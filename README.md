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
  from the install directory on demand, see
  [Adding a library](docs/adding-a-library.md).

```lua
local xxh = require "lust.xxh"
print(xxh.xxh64("abc")) --> 44bc2cf5ad770999
```

| library     | what                | dependency                              |
| ----------- | ------------------- | --------------------------------------- |
| `lust.xxh`  | xxHash              | `vendor/xxhash`, linked statically      |
| `lust.pcre2` | PCRE2 (Perl compatible) regular expressions | `vendor/pcre2`, linked statically |
| `lust.json` | JSON (2 to 4 times faster than lua-cjson, see `libs/json/vs-cjson.lua`) | `vendor/yyjson`, linked statically |
| `lust.sys.fs` | file system: list, walk, stat, readlink, mkdir, temporary files | none (libc) |
| `lust.sys.proc` | run programs without a shell, capture their output | none (libc) |
| `lust.sys.env` | set and unset environment variables | none (libc) |
| `lust.sys.time` | monotonic clock | none (libc) |
| `lust.sys.random` | cryptographically secure random bytes | none (libc) |
| `lust.sys.term` | terminal detection and size | none (libc) |
| `lust.sys.signal` | Lua callbacks for signals | none (libc) |
| `lust.meta` | versions of lust, LuaJIT and the bundled dependencies | none (their headers) |

A library can be in a namespace: `lust.sys.proc` is a library of its own
(`lust/sys/proc.so`), `sys` only groups it with the others, there is no
`lust.sys` to require.

Libraries come in two kinds (`lib_kind` in their `meson.build`):

- `capi`: classic Lua C API module (all libraries at the moment). The
  default, and cheapest to `require`. It may have a Lua
  part for what reads better in Lua, embedded as bytecode (`lust.sys.fs`'s
  `walkdir`).
- `ffi`: [static FFI bindings](https://evo-lua.github.io/docs/background-information/luajit/static-ffi-bindings/).
  The C code is plain C that knows nothing about Lua and just exports
  functions. A Lua file binds them with `ffi.cdef`; it is compiled to
  bytecode at build time, embedded, and only loaded on `require`
  (no library uses this at the moment). The FFI calls are visible to the JIT, which is the main
  point, but `require`ing one costs about 90k instructions more than a
  C API module, so use it only where the JIT pays off hugely (tiny, hot
  calls like hashing), not where the library itself dominates (regexp
  matching). A system library is best bound with `ffi.load()`: it is then
  `dlopen`ed on `require` only, so it can live inside `lust` for free.
  The embedded bytecode must match the LuaJIT that loads it (version and
  GC64), otherwise `require` fails with a clear error.

## Building

Requirements: `meson`, `ninja`, a C and a C++ compiler, `make`,
`git` and `python3` (plus the dev files of dynamic dependencies, of
which there are currently none).

```sh
# Arch Linux
sudo pacman -S --needed meson ninja gcc make git python
# Ubuntu
sudo apt install meson ninja-build gcc g++ make git python3

git clone <this repo> && cd lust
./dev.py build            # everything
./dev.py build lust       # only the `lust` binary  -> build/src/lust
./dev.py build libs       # all libraries as .so    -> build/libs/lust/*.so
./dev.py build xxh        # a single library
./dev.py test
```

`dev.py` only shims `meson`; anything it does you can do by hand
(`meson setup build && meson compile -C build && meson test -C build`).
Meson options (`./dev.py build -D name=value`, see `meson_options.txt`):

- `libs=xxh,pcre2,json,fs`: build only these libraries (default: all). This also
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
build/src/lust examples/pcre2.lua
build/src/lust examples/sys.lua    # all of lust.sys.*
```

## Installing

Like `make install`; targets are `all` (default), `lust`, `libs` or a
library name. Needs root for system prefixes (meson will ask via
sudo/pkexec).

```sh
./dev.py install                        # to /usr/local
./dev.py install lust xxh --prefix /usr
./dev.py install --prefix /usr --destdir pkg   # staging, for packaging
```

| what                         | where                                   |
| ---------------------------- | --------------------------------------- |
| `lust`                       | `<prefix>/bin/lust`                     |
| `lust/<name>.so`             | `<libdir>/lua/5.1/lust/<name>.so`       |
| `lust/<ns>/<name>.so`        | `<libdir>/lua/5.1/lust/<ns>/<name>.so`  |
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
libs/<ns>/<name>/   the same for library <ns>.<name> (libs/sys/proc/: lust.sys.proc)
libs/<name>/bench/  its benchmarks + their baseline.txt (instruction counts)
libs/ffi_shim.c     loads the embedded bytecode of the `ffi` libraries
libs/lust/          defines the build targets of all libraries
src/                the `lust` binary (lust.c + build of LuaJIT)
examples/           runnable examples, one per library (or namespace)
docs/               conventions and how-tos for working on lust
tools/              helper used by the meson build
tools/absl-sources.py  checks the Abseil source list of vendor/meson.build
tests/              test harness + cross library tests
vendor/luajit/      LuaJIT (managed by vendor.py, never edit)
vendor/xxhash/      xxHash (managed by vendor.py, never edit)
vendor/re2/         RE2, not used by any library at the moment (managed by vendor.py, never edit)
vendor/abseil/      Abseil, which RE2 needs (managed by vendor.py, never edit)
vendor/pcre2/       PCRE2 (managed by vendor.py, never edit)
vendor/meson.build  build definitions of the vendored dependencies
vendor/vendor.toml  upstream repos + revisions of the above
```

External dependencies are either **dynamic** (a normal `dependency()`
resolved through pkg-config; they stay shared libraries at runtime), or
**static**, from `vendor/`. Nothing is ever downloaded at build time.

## Documentation

- [Adding a library](docs/adding-a-library.md)
- [Benchmark regressions](docs/benchmark-regressions.md)
- [Error handling](docs/error-handling.md): when libraries raise and
  when they return `nil, err`
