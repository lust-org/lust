# Library registry.
#
# To add a library <name>:
#   1. add it to `all_libs` below,
#   2. create libs/<name>/ with <name>.c (exporting `luaopen_lust_<name>`
#      tagged LUST_API), a test.lua and a meson.build that sets
#      `lib_sources` and `lib_deps` (see libs/crc/meson.build).
#
# From these a loadable module `lust/<name>.so` and a static archive (for
# the `lust` binary) are built. Each library only pulls in its own
# dependencies, so using one never costs anything for the others.

all_libs = ['crc', 'xxh']

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

# name -> {sources, deps} of every enabled library, see libs/lust/meson.build
lib_infos = {}
foreach name : enabled_libs
  # Contract with libs/<name>/meson.build:
  lib_sources = [] # C sources of the library
  lib_deps = [] # its dependencies (dynamic system ones or vendored static)
  subdir(name)
  lib_infos += {name: {'sources': lib_sources, 'deps': lib_deps}}
endforeach

# The targets are defined in libs/lust/ (rather than here) on purpose, see
# the comment in there.
shared_mods = []
static_lib_deps = [] # consumed by the `lust` executable
subdir('lust')
alias_target('libs', shared_mods)
