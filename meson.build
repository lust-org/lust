project(
  'lust',
  'c',
  version: '0.1.0',
  meson_version: '>=1.3.0', # build_subdir
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'buildtype=release',
    # Loadable modules resolve the Lua C API from the host interpreter at
    # load time, so they must be allowed to have undefined symbols.
    'b_lundef=false',
  ],
)

fs = import('fs')

# All libraries are compiled against the vendored LuaJIT headers. Only the
# headers are needed for the .so modules: no libluajit gets linked into
# them, they use the symbols of whichever LuaJIT loads them.
luajit_src = meson.project_source_root() / 'vendor' / 'luajit'
if not fs.exists(luajit_src / 'src' / 'lua.h')
  error(
    'vendor/luajit is empty. Fetch it with: ' +
    'git submodule update --init vendor/luajit',
  )
endif

# Dependency carrying everything a library needs to compile: the LuaJIT
# headers and our own include/lust.h.
lust_dep = declare_dependency(
  include_directories: [
    include_directories('vendor/luajit/src', is_system: true),
    include_directories('include'),
  ],
)

lust_feature = get_option('lust')

subdir('vendor')
subdir('libs')
if lust_feature.allowed()
  subdir('src')
endif
subdir('tests')
