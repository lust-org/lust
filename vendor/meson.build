# Vendored dependencies. The build definitions live here (not inside the
# vendor/<dep>/ directories) so those can be replaced wholesale by
# vendor.py without losing anything. Each dependency is exposed as a
# declare_dependency().

fs = import('fs')

# LuaJIT: only its sources and headers are vendored. The libraries are
# compiled against the headers alone (no libluajit gets linked into the
# .so modules, they use the symbols of whichever LuaJIT loads them);
# src/meson.build builds the VM itself from luajit_src for `lust`.
luajit_src = meson.project_source_root() / 'vendor' / 'luajit'
if not fs.exists(luajit_src / 'src' / 'lua.h')
  error(
    'vendor/luajit has no LuaJIT sources (see vendor/vendor.toml, ' +
    'vendor.py)',
  )
endif
luajit_inc = include_directories('luajit/src', is_system: true)

# xxHash: static archive, PIC so it can be absorbed into the shared
# modules. Only built when something links against it.
xxhash_lib = static_library(
  'xxhash',
  'xxhash/xxhash.c',
  include_directories: include_directories('xxhash', is_system: true),
  override_options: ['warning_level=0'],
  pic: true,
  build_by_default: false,
)

xxhash_dep = declare_dependency(
  link_with: xxhash_lib,
  include_directories: include_directories('xxhash', is_system: true),
)
