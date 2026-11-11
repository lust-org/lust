# lust

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

## Layout

```
dev.py              build / test / install shim around meson
meson.build         project, ties everything together
meson_options.txt
include/lust.h      header shared by all libraries
libs/<name>/        one directory per library: <name>.c, test.lua, meson.build
libs/<ns>/<name>/   the same for library <ns>.<name> (libs/sys/proc/: lust.sys.proc)
libs/<name>/bench/  its benchmarks + their baseline.txt (instruction counts)
libs/ffi_shim.c     loads the embedded Lua part of the `ffi` libraries
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

## Documentation

- [Overview, building and installing](docs/README.md)
- [Adding a library](docs/adding-a-library.md)
- [Benchmark regressions](docs/benchmark-regressions.md)
- [Error handling](docs/error-handling.md): when libraries raise and
  when they return `nil, err`
