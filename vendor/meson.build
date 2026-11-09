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

# LuaJIT's own (make based) build -> libluajit.a, the generated
# jit/vmdef.lua, the generated luajit.h (version macros) and the luajit
# interpreter itself (used at build time, e.g. to compile FFI libraries
# to bytecode).
# Always "stale": make decides whether there is work to do, and the script
# leaves the outputs untouched if nothing changed (no needless relinking).
luajit_build = custom_target(
  'luajit',
  output: ['libluajit.a', 'vmdef.lua', 'luajit.h', 'luajit'],
  command: [
    py,
    files('../tools/build-luajit.py'),
    '--src', luajit_src,
    '--work', meson.current_build_dir() / 'luajit-tree',
    '--cc', ' '.join(cc.cmd_array()),
    '--xcflags', get_option('luajit_xcflags'),
    '--out-lib', '@OUTPUT0@',
    '--out-vmdef', '@OUTPUT1@',
    '--out-header', '@OUTPUT2@',
    '--out-bin', '@OUTPUT3@',
  ],
  build_always_stale: true,
  # Only the vmdef.lua is installed, and only along with `lust`.
  install: lust_feature.allowed(),
  install_dir: [false, datadir / 'jit', false, false],
  install_tag: 'lust',
)
luajit_exe = luajit_build[3]

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
