project(
  'lust',
  'c',
  version: '0.1.0',
  meson_version: '>=1.3.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'buildtype=release',
    # Loadable modules resolve the Lua C API from the host interpreter at
    # load time, so they must be allowed to have undefined symbols.
    'b_lundef=false',
  ],
)

py = find_program('python3')
cc = meson.get_compiler('c')
lust_feature = get_option('lust')
# Where the Lua part of the runtime (jit.* modules) is installed.
datadir = get_option('prefix') / get_option('datadir') / 'lust'

# Vendored dependencies first: they provide luajit_src, luajit_inc, ...
subdir('vendor')

# Dependency carrying everything a library needs to compile: the LuaJIT
# headers and our own include/lust.h.
lust_dep = declare_dependency(
  include_directories: [luajit_inc, include_directories('include')],
)

subdir('libs')
if lust_feature.allowed()
  subdir('src')
endif
subdir('tests')
