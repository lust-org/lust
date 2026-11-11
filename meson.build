project(
  'lust',
  'c',
  version: '0.0.1',
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
# A fully static `lust` (see meson_options.txt): only it gets built.
static = get_option('static')
if static and not lust_feature.allowed()
  error('static=true builds only `lust`, it needs lust=enabled')
endif
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
