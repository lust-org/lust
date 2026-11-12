# LuSt
A LuaJIT runtime bundled with a slim, but proper StdLib - you can also use only
the libraries from your existing `LuaJIT` or `PUC Lua 5.1` runtime.

| library           | what                                                                    | dependency                              |
| -----------       | -------------------                                                     | --------------------------------------- |
| `lust.hash.xxh`   | xxHash                                                                  | `vendor/xxhash`, linked statically      |
| `lust.hash.md5`   | MD5, one-shot or incremental                                            | `vendor/mbedtls`, linked statically     |
| `lust.hash.sha1`  | SHA-1, one-shot or incremental                                          | `vendor/mbedtls`, linked statically     |
| `lust.hash.sha256`| SHA-256, one-shot or incremental                                        | `vendor/mbedtls`, linked statically     |
| `lust.pcre2`      | PCRE2 (Perl compatible) regular expressions                             | `vendor/pcre2`, linked statically       |
| `lust.json`       | JSON (2 to 4 times faster than lua-cjson, see `libs/json/vs-cjson.lua`) | `vendor/yyjson`, linked statically      |
| `lust.sys.fs`     | file system: read, write, list, walk, stat, paths, mkdir, temp files    | none (libc)                             |
| `lust.sys.proc`   | run programs without a shell, talk to them through pipes, poll, exec    | none (libc)                             |
| `lust.sys.env`    | set and unset environment variables                                     | none (libc)                             |
| `lust.sys.time`   | monotonic clock, sleeps                                                 | none (libc)                             |
| `lust.sys.random` | cryptographically secure random bytes                                   | none (libc)                             |
| `lust.sys.term`   | terminal detection and size                                             | none (libc)                             |
| `lust.sys.signal` | Lua callbacks for signals, signal numbers                               | none (libc)                             |
| `lust.meta`       | versions of lust, LuaJIT and the bundled dependencies                   | none (their headers)                    |

## Layout

```
dev.py              build / test / install shim around meson
docs/               conventions and how-tos for working on lust
examples/           runnable examples, one per library (or namespace)
include/lust.h      header shared by all libraries
libs/<name>/        one directory per library: <name>.c, test.lua, meson.build
libs/<name>/bench/  its benchmarks + their baseline.txt (instruction counts)
libs/<ns>/<name>/   the same for library <ns>.<name> (e.g. libs/sys/proc/: lust.sys.proc)
libs/ffi_shim.c     loads the embedded Lua part of the `ffi` libraries
libs/lust/          defines the build targets of all libraries
meson.build         project, ties everything together
meson_options.txt
src/                the `lust` binary
tests/              test harness + cross library tests
tools/              helper used by the meson build
vendor/luajit/      LuaJIT (managed by vendor.py, never edit)
vendor/mbedtls/     Mbed TLS + TF-PSA-Crypto (managed by vendor.py, never edit)
vendor/meson.build  build definitions of the vendored dependencies
vendor/pcre2/       PCRE2 (managed by vendor.py, never edit)
vendor/vendor.toml  upstream repos + revisions
vendor/xxhash/      xxHash (managed by vendor.py, never edit)
```

## Documentation

- [Overview, building and installing](docs/README.md)
- [Adding a library](docs/adding-a-library.md)
- [Benchmark regressions](docs/benchmark-regressions.md)
- [Error handling](docs/error-handling.md): when libraries raise and when they
  return `nil, err`
