# Library registry.
#
# To add a library <name>:
#   1. add it to `all_libs` below,
#   2. create libs/<name>/ with a test.lua and a meson.build that sets
#      `lib_sources` and `lib_deps`, plus `lib_kind` (default 'capi'):
#        - 'capi': <name>.c exports `luaopen_lust_<name>` tagged LUST_API
#          and uses the Lua C API (see libs/hash/xxh/meson.build). Preferred.
#        - 'ffi': static FFI bindings, costlier to `require`: only where
#          the JIT pays off hugely (see AGENTS.md). The C code is plain C
#          exporting (LUST_API) the functions to call; `lib_lua` is a Lua
#          file using the LuaJIT FFI to bind them. It is compiled to
#          bytecode and embedded (with its source, see include/lust_lua.h),
#          and only loaded on `require` (see libs/ffi_shim.c; no library
#          uses it at the moment).
#
#      A 'capi' library may have a Lua part, too (`lib_lua`, for code that
#      is better written in Lua): it is embedded the same way and the C
#      code gets it as LUST_LUA_BC_H to #include, which defines
#      luaJIT_BC_lust_<name>[] and luaJIT_BC_lust_<name>_SIZE, and
#      LUST_LUA_SRC_H with lust_lua_src_<name>[] and its _SIZE; the C code
#      decides when to load it with lust_load_lua (see libs/sys/fs/fs.c:
#      lazily, on first use).
#
#      Every library also MUST have a doc-shortform.txt: its API as
#      succinct as possible, which becomes part of `lust`'s help text
#      (format: see tools/gen-help.py). tests/test_doc.lua checks that it
#      lists exactly what the module exports.
#
#      Optional `lib_embed = false` (default true) keeps the library out of
#      `lust`: it is then only built as a .so, which `lust` loads on demand
#      from its install cmoddir. REQUIRED for a library that links a
#      dynamic dependency (see AGENTS.md); `ffi` libraries don't need it.
#
# From these a loadable module `lust/<name>.so` and a static archive (for
# the `lust` binary) are built. Each library only pulls in its own
# dependencies, so using one never costs anything for the others.
#
# A name may have dots: `sys.env` is `require "lust.sys.env"`, lives in
# libs/sys/env/, exports `luaopen_lust_sys_env` and is built as
# lust/sys/env.so (where the stock luajit looks for it). The parts before
# the last dot are only a namespace: there is no library `sys`, unless one
# is added like any other. Names have no `_`: `sys_env` and `sys.env`
# would both export `luaopen_lust_sys_env`.

all_libs = [
  'hash.xxh',
  'pcre2',
  'json',
  'sys.fs',
  'sys.time',
  'sys.random',
  'sys.env',
  'sys.term',
  'sys.signal',
  'sys.proc',
  'meta',
]

requested = get_option('libs')
enabled_libs = requested.length() > 0 ? requested : all_libs
foreach name : enabled_libs
  if name not in all_libs
    error(
      'Unknown library "@0@". Available: @1@'.format(name, ', '.join(all_libs)),
    )
  endif
endforeach
foreach name : all_libs
  if name.contains('_') or '' in name.split('.')
    error('Library name "@0@": no "_", no empty parts between dots'.format(name))
  endif
endforeach

cmoddir = get_option('cmoddir')
if cmoddir == ''
  cmoddir = get_option('libdir') / 'lua' / '5.1'
endif

# name -> {kind, sources, lua, deps} of every enabled library, see
# libs/lust/meson.build
lib_infos = {}
built_libs = [] # (enabled_libs without the ones left out)
foreach name : enabled_libs
  # Contract with libs/<name>/meson.build:
  lib_sources = [] # C sources of the library
  lib_deps = [] # its dependencies (dynamic system ones or vendored static)
  lib_kind = 'capi' # or 'ffi'
  lib_lua = [] # 'ffi': the Lua file binding the C functions, 'capi': optional Lua part
  lib_embed = true # link into `lust`
  dir = name.replace('.', '/') # (libs/sys/env/ for sys.env)
  subdir(dir)
  if lib_kind not in ['capi', 'ffi']
    error('@0@: lib_kind must be "capi" or "ffi"'.format(name))
  endif
  if lib_kind == 'ffi' and lib_lua.length() == 0
    error('@0@: lib_lua is required for "ffi" libraries'.format(name))
  endif
  if static and not lib_embed
    # (only loadable as .so, which a static `lust` can't)
    warning('@0@: left out, lib_embed = false and static = true'.format(name))
    continue
  endif
  lib_infos += {
    name: {
      'dir': dir,
      # C identifier: luaopen_lust_<id>, luaJIT_BC_lust_<id>, lust_lua_src_<id>
      'id': name.replace('.', '_'),
      'kind': lib_kind,
      'sources': lib_sources,
      'lua': lib_lua,
      'embed': lib_embed,
      'deps': lib_deps,
      # (missing file: meson fails at configure time)
      'doc': files(dir / 'doc-shortform.txt'),
      # (as a path for command lines: File.full_path() needs meson 1.4)
      'doc_path': meson.current_source_dir() / dir / 'doc-shortform.txt',
    },
  }
  built_libs += name
endforeach
enabled_libs = built_libs

# The targets are defined in libs/lust/ (rather than here) on purpose, see
# the comment in there.
shared_mods = []
static_lib_deps = [] # consumed by the `lust` executable
subdir('lust')
if shared_mods.length() > 0 # (none for static = true)
  alias_target('libs', shared_mods)
endif
