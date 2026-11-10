# Defines the build targets of all libraries.
#
# `require "lust.<name>"` makes the stock interpreter look for
# <cpath>/lust/<name>.so. Defining the modules from within this `lust/`
# directory mirrors that layout in the build tree (build/libs/lust/*.so),
# so the build directory can be used directly through
# LUA_CPATH=build/libs/?.so (that is what the tests do).
#
# For a library in a namespace (sys.env: lust/sys/env.so) the namespace
# directory is the name_prefix of the module (`sys/`): meson places the
# output there (that works from 1.3.0 on, a subdir() per namespace would
# have to repeat all of this).

fs = import('fs')

foreach name, info : lib_infos
  deps = [lust_dep] + info['deps']
  sources = info['sources']
  c_args = []
  id = info['id']
  base = fs.name(info['dir']) # env (for sys/env)
  parent = fs.parent(info['dir']) # sys (or '.')
  prefix = parent == '.' ? '' : parent + '/'

  if info['lua'].length() > 0
    # The Lua part is compiled to (stripped, hence reproducible) bytecode by
    # the LuaJIT built from vendor/luajit and embedded: for 'ffi' by
    # ffi_shim.c, which provides the luaopen function (see there for the
    # details), for 'capi' by the library's own C code.
    bytecode = custom_target(
      id + '_bc',
      input: info['lua'],
      output: id + '_bc.h',
      command: [luajit_exe, '-b', '-n', 'lust_' + id, '@INPUT@', '@OUTPUT@'],
      # `luajit -b` is implemented by jit/bcsave.lua, which the uninstalled
      # interpreter finds through this.
      env: {'LUA_PATH': luajit_src / 'src' / '?.lua'},
    )
    sources += bytecode
  endif
  if info['kind'] == 'ffi'
    sources += files('../ffi_shim.c')
    c_args += [
      '-DLUST_FFI_NAME=' + id,
      '-DLUST_FFI_BC_H="@0@_bc.h"'.format(id),
    ]
  elif info['lua'].length() > 0
    c_args += '-DLUST_LUA_BC_H="@0@_bc.h"'.format(id)
  endif

  mod = shared_module(
    base,
    sources,
    name_prefix: prefix,
    c_args: c_args,
    dependencies: deps,
    gnu_symbol_visibility: 'hidden',
    install: true,
    install_dir: cmoddir / 'lust' / prefix,
    install_tag: 'lib-' + name,
  )
  shared_mods += mod
  alias_target('lib-' + name, mod)

  if lust_feature.allowed() and info['embed']
    # Same sources again, as a static archive for linking into `lust`.
    archive = static_library(
      'lust_' + id,
      sources,
      c_args: c_args + ['-DLUST_STATIC'],
      dependencies: deps,
      gnu_symbol_visibility: 'hidden',
      build_by_default: false,
    )
    # link_whole: FFI libraries are only reached through dlsym (ffi.C), so
    # nothing references their objects at link time.
    static_lib_deps += declare_dependency(
      link_whole: archive,
      dependencies: info['deps'],
    )
  endif
endforeach
