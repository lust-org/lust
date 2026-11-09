# Defines the build targets of all libraries.
#
# `require "lust.<name>"` makes the stock interpreter look for
# <cpath>/lust/<name>.so. Defining the modules from within this `lust/`
# directory mirrors that layout in the build tree (build/libs/lust/*.so),
# so the build directory can be used directly through
# LUA_CPATH=build/libs/?.so (that is what the tests do).

foreach name, info : lib_infos
  deps = [lust_dep] + info['deps']

  mod = shared_module(
    name,
    info['sources'],
    name_prefix: '',
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
      info['sources'],
      dependencies: deps,
      gnu_symbol_visibility: 'hidden',
      build_by_default: false,
    )
    static_lib_deps += declare_dependency(
      link_with: archive,
      dependencies: info['deps'],
    )
  endif
endforeach
