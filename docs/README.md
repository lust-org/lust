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
  [Adding a library](adding-a-library.md).

```lua
local xxh = require "lust.xxh"
print(xxh.xxh64("abc")) --> 44bc2cf5ad770999
```

A library can be in a namespace: `lust.sys.proc` is a library of its own
(`lust/sys/proc.so`), `sys` only groups it with the others, there is no
`lust.sys` to require.

Libraries come in two kinds (`lib_kind` in their `meson.build`):

- `capi`: classic Lua C API module (all libraries at the moment). The
  default, and cheapest to `require`. It may have a Lua
  part for what reads better in Lua, embedded as bytecode and as source
  (`lust.sys.fs`'s `walkdir`).
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
  The embedded bytecode loads only into a LuaJIT with the same bytecode
  format (version and GC64), others compile the embedded source instead.

## Dependencies

External dependencies are either **dynamic** (a normal `dependency()`
resolved through pkg-config; they stay shared libraries at runtime), or
**static**, from `vendor/`. Nothing is ever downloaded at build time.

## Building

Requirements: `meson`, `ninja`, a C and a C++ compiler, `make`,
`git` and `python3` (plus the dev files of dynamic dependencies, of
which there are currently none). The tests also use a stock `luajit`
and PUC `lua5.1` from PATH where installed (Arch: `luajit lua51`,
Ubuntu: `luajit lua5.1`).

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
- `static=true`: link `lust` fully statically, see below.

### A static `lust` with musl

```sh
sudo pacman -S --needed musl     # Arch
sudo apt install musl-tools      # Ubuntu
./dev.py build --musl-static     # -> build-musl/src/lust
./dev.py test --musl-static
```

This compiles everything with `musl-gcc` and links `lust` fully
statically (`static=true`), in its own build directory (`build-musl`,
`-B` picks another). The binary needs nothing from the system it runs on,
but as there is no dynamic loader:

- only `lust` is built: no `.so` modules, and libraries with
  `lib_embed = false` are left out,
- `lust` can't load C modules (`require` of a `.so`), and the LuaJIT
  FFI can't resolve C symbols (`ffi.C.<name>`, `ffi.load`).

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
