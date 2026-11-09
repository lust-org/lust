# Library registry.
#
# To add a library <name>:
#   1. add it to `all_libs` below,
#   2. create libs/<name>/ with a test.lua and a meson.build that sets
#      `lib_sources` and `lib_deps`, plus `lib_kind` (default 'capi'):
#        - 'capi': <name>.c exports `luaopen_lust_<name>` tagged LUST_API
#          and uses the Lua C API (see libs/crc/meson.build).
#        - 'ffi': static FFI bindings. The C code is plain C exporting
#          (LUST_API) the functions to call; `lib_lua` is a Lua file using
#          the LuaJIT FFI to bind them. It is compiled to bytecode and
#          embedded, and only loaded on `require` (see libs/xxhffi/).
#
#      Optional `lib_embed = false` (default true) keeps the library out of
#      `lust`: it is then only built as a .so, which `lust` loads on demand
#      from its install cmoddir. REQUIRED for a library that links a
#      dynamic dependency (see AGENTS.md); `ffi` libraries don't need it.
#
# From these a loadable module `lust/<name>.so` and a static archive (for
# the `lust` binary) are built. Each library only pulls in its own
# dependencies, so using one never costs anything for the others.

all_libs = ['crc', 'xxh', 'xxhffi']

requested = get_option('libs')
enabled_libs = requested.length() > 0 ? requested : all_libs
foreach name : enabled_libs
  if name not in all_libs
    error(
      'Unknown library "@0@". Available: @1@'.format(name, ', '.join(all_libs)),
    )
  endif
endforeach

cmoddir = get_option('cmoddir')
if cmoddir == ''
  cmoddir = get_option('libdir') / 'lua' / '5.1'
endif

# name -> {kind, sources, lua, deps} of every enabled library, see
# libs/lust/meson.build
lib_infos = {}
foreach name : enabled_libs
  # Contract with libs/<name>/meson.build:
  lib_sources = [] # C sources of the library
  lib_deps = [] # its dependencies (dynamic system ones or vendored static)
  lib_kind = 'capi' # or 'ffi'
  lib_lua = [] # 'ffi' only: the Lua file binding the C functions
  lib_embed = true # link into `lust`
  subdir(name)
  if lib_kind not in ['capi', 'ffi']
    error('@0@: lib_kind must be "capi" or "ffi"'.format(name))
  endif
  if (lib_kind == 'ffi') != (lib_lua.length() > 0)
    error('@0@: lib_lua is required for (and only for) "ffi" libraries'.format(name))
  endif
  lib_infos += {
    name: {
      'kind': lib_kind,
      'sources': lib_sources,
      'lua': lib_lua,
      'embed': lib_embed,
      'deps': lib_deps,
    },
  }
endforeach

# The targets are defined in libs/lust/ (rather than here) on purpose, see
# the comment in there.
shared_mods = []
static_lib_deps = [] # consumed by the `lust` executable
subdir('lust')
alias_target('libs', shared_mods)
