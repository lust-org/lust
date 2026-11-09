# AGENTS.md

C libraries for LuaJIT. Each builds as a `.so` for stock luajit and as a
static archive linked into the `lust` binary (LuaJIT + all libraries).

## Constraints

- Never add, edit or remove files inside `vendor/*/**`. They are managed
  by `vendor.py` / `vendor/vendor.toml`. Build definitions for vendored
  code (e.g. `xxhash_dep`, `luajit_src`) go in `vendor/meson.build`.
- External dependencies are either dynamic (`dependency()`) or static
  from `vendor/`. Nothing is downloaded at build time.
- Build system is meson only, `meson_version: '>=1.3.0'` (Ubuntu 24.04).
  Do not use newer-only features (e.g. `build_subdir` needs 1.10).
- C11 for our code. Only upstream `luajit.c` is built as `gnu11`.
- `.so` modules must not link libluajit: only the LuaJIT headers are used,
  symbols resolve from the host interpreter. Keep `b_lundef=false`.
- Export only `luaopen_lust_<name>` (`LUST_API`); everything else hidden.
- Requiring one library must not load any other (`tests/test_lazy.lua`).
- Stock luajit finds `require "lust.<x>"` at `<cpath>/lust/<x>.so`, so
  module output names/dirs must keep that layout.
- Do not modify the vendored LuaJIT tree. `tools/build-luajit.py` mirrors
  it into the build dir and builds there.
- Install locations must work on Arch and Ubuntu: use meson's `libdir`
  (differs per distro), keep `cmoddir` overridable.
- `./dev.py` stays a thin shim over meson (`build`, `install`, `test`).

## Workflow

- `./dev.py test` must pass (each lib: shared + static + lazy tests).
- Adding a library: see "Adding a library" in `README.md`.
- Commit each finished change separately; amend when asked to change a
  previous commit.
