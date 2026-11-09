# Defines the build targets of all libraries.
#
# `require "lust.<name>"` makes the stock interpreter look for
# <cpath>/lust/<name>.so. Defining the modules from within this `lust/`
# directory mirrors that layout in the build tree (build/libs/lust/*.so),
# so the build directory can be used directly through
# LUA_CPATH=build/libs/?.so (that is what the tests do).

foreach name, info : lib_infos
  deps = [lust_dep] + info['deps']
  sources = info['sources']
  c_args = []

  if info['kind'] == 'ffi'
    # The Lua part is compiled to (stripped, hence reproducible) bytecode by
    # the LuaJIT built from vendor/luajit, and embedded by ffi_shim.c, which
    # provides the luaopen function. See ffi_shim.c for the details.
    bytecode = custom_target(
      name + '_bc',
      input: info['lua'],
      output: name + '_bc.h',
      command: [luajit_exe, '-b', '-n', 'lust_' + name, '@INPUT@', '@OUTPUT@'],
      # `luajit -b` is implemented by jit/bcsave.lua, which the uninstalled
      # interpreter finds through this.
      env: {'LUA_PATH': luajit_src / 'src' / '?.lua'},
    )
    sources += [files('../ffi_shim.c'), bytecode]
    c_args += [
      '-DLUST_FFI_NAME=' + name,
      '-DLUST_FFI_BC_H="@0@_bc.h"'.format(name),
    ]
  endif

  mod = shared_module(
    name,
    sources,
    name_prefix: '',
    c_args: c_args,
    dependencies: deps,
    gnu_symbol_visibility: 'hidden',
    install: true,
    install_dir: cmoddir / 'lust',
    install_tag: 'lib-' + name,
  )
  shared_mods += mod
  alias_target('lib-' + name, mod)

  if lust_feature.allowed()
    # Same sources again, as a static archive for linking into `lust`.
    archive = static_library(
      'lust_' + name,
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
