# The `lust` interpreter: LuaJIT + all enabled libraries, statically linked.

# 1. LuaJIT's own build (libluajit.a, luajit.h, vmdef.lua) is defined in
#    vendor/meson.build as `luajit_build`.
#    The rest of the jit.* Lua modules are static files in the LuaJIT tree.
install_subdir(
  luajit_src / 'src' / 'jit',
  install_dir: datadir,
  exclude_files: ['.gitignore'],
  install_tag: 'lust',
)

# 2. Generated list of embedded libraries (an X-macro, see src/lust.c).
fs = import('fs')
embedded = []
foreach name, info : lib_infos
  if info['embed']
    embedded += 'X(@0@)'.format(name)
  endif
endforeach
# Where `lust` finds the libraries that are not embedded.
lust_cmoddir = fs.is_absolute(cmoddir) ? cmoddir : get_option('prefix') / cmoddir
conf = configuration_data()
conf.set('LUST_DATADIR', datadir)
conf.set('LUST_CMODDIR', lust_cmoddir)
conf.set('LUST_EMBEDDED_LIBS', ' '.join(embedded))
lust_libs_h = configure_file(
  input: 'lust_libs.h.in',
  output: 'lust_libs.h',
  configuration: conf,
)

# 3. LuaJIT's stock main(), with luaL_openlibs redirected to lust_openlibs().
#    Compiled in its own target so the define doesn't leak into lust.c.
luajit_main = static_library(
  'luajit_main',
  luajit_src / 'src' / 'luajit.c',
  luajit_build[2], # generated luajit.h, found via the build dir include
  c_args: ['-DluaL_openlibs=lust_openlibs'],
  dependencies: lust_dep,
  # Upstream code: it needs POSIX (sigaction), and we don't police its warnings.
  override_options: ['warning_level=0', 'c_std=gnu11'],
  build_by_default: false,
)

lust_exe = executable(
  'lust',
  'lust.c',
  lust_libs_h,
  objects: luajit_main.extract_all_objects(recursive: false),
  link_with: luajit_build[0],
  dependencies: [
    lust_dep,
    static_lib_deps,
    cc.find_library('m'),
    cc.find_library('dl'),
  ],
  # LuaJIT's FFI and third-party C modules loaded at runtime need the host
  # to export the Lua C API (-Wl,-E).
  export_dynamic: true,
  install: true,
  install_tag: 'lust',
)
