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
    # (one argument with `=`: argparse would take a value starting with
    # `-` for an option)
    '--xcflags=' + get_option('luajit_xcflags'),
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

# ---------------------------------------------------------------------------
# PCRE2 (8 bit code units only, with Unicode support). Upstream's own build
# is autotools/CMake, so the source list is kept here (it is the list of
# PCRE2_HEADERS/SOURCES of vendor/pcre2/CMakeLists.txt; compare it after an
# upgrade). The configuration is upstream's generic one (config.h.generic,
# pcre2.h.generic), copied into the build dir, with the features switched
# on by the compile arguments below. No JIT: its sljit is a git submodule
# that is not part of the vendored tree.
# ---------------------------------------------------------------------------
pcre2_src = 'pcre2' / 'src'
pcre2_gen = [
  configure_file(
    input: pcre2_src / 'config.h.generic',
    output: 'config.h',
    copy: true,
  ),
  configure_file(
    input: pcre2_src / 'pcre2.h.generic',
    output: 'pcre2.h',
    copy: true,
  ),
  # The character tables of the "C" locale, which is what upstream ships
  # (it is what its own build does unless asked to regenerate them).
  configure_file(
    input: pcre2_src / 'pcre2_chartables.c.dist',
    output: 'pcre2_chartables.c',
    copy: true,
  ),
]
pcre2_inc = include_directories('.', pcre2_src, is_system: true)
pcre2_lib = static_library(
  'pcre2',
  pcre2_gen[2],
  files(
    pcre2_src / 'pcre2_auto_possess.c',
    pcre2_src / 'pcre2_chkdint.c',
    pcre2_src / 'pcre2_compile.c',
    pcre2_src / 'pcre2_compile_cgroup.c',
    pcre2_src / 'pcre2_compile_class.c',
    pcre2_src / 'pcre2_config.c',
    pcre2_src / 'pcre2_context.c',
    pcre2_src / 'pcre2_convert.c',
    pcre2_src / 'pcre2_dfa_match.c',
    pcre2_src / 'pcre2_error.c',
    pcre2_src / 'pcre2_extuni.c',
    pcre2_src / 'pcre2_find_bracket.c',
    pcre2_src / 'pcre2_jit_compile.c',
    pcre2_src / 'pcre2_maketables.c',
    pcre2_src / 'pcre2_match.c',
    pcre2_src / 'pcre2_match_data.c',
    pcre2_src / 'pcre2_match_next.c',
    pcre2_src / 'pcre2_newline.c',
    pcre2_src / 'pcre2_ord2utf.c',
    pcre2_src / 'pcre2_pattern_info.c',
    pcre2_src / 'pcre2_script_run.c',
    pcre2_src / 'pcre2_serialize.c',
    pcre2_src / 'pcre2_string_utils.c',
    pcre2_src / 'pcre2_study.c',
    pcre2_src / 'pcre2_substitute.c',
    pcre2_src / 'pcre2_substring.c',
    pcre2_src / 'pcre2_tables.c',
    pcre2_src / 'pcre2_ucd.c',
    pcre2_src / 'pcre2_valid_utf.c',
    pcre2_src / 'pcre2_xclass.c',
  ),
  include_directories: pcre2_inc,
  c_args: [
    '-DHAVE_CONFIG_H',
    '-DPCRE2_CODE_UNIT_WIDTH=8',
    '-DSUPPORT_PCRE2_8=1',
    '-DSUPPORT_UNICODE=1',
    # (what upstream's release builds do; meson would keep the asserts)
    '-DNDEBUG',
    '-ffile-prefix-map=@0@=.'.format(meson.project_source_root()),
    '-ffile-prefix-map=@0@=.'.format(meson.project_build_root()),
  ],
  override_options: ['warning_level=0'],
  gnu_symbol_visibility: 'hidden',
  pic: true,
  build_by_default: false,
)
# Users of the library only see the public header (pcre2.h, in the build
# dir; they define PCRE2_CODE_UNIT_WIDTH themselves). The archive's symbols
# must not leak out of the modules (nor out of `lust`, which exports its
# symbols for the FFI): only `luaopen_lust_*` and what the FFI libraries
# bind may be exported.
pcre2_dep = declare_dependency(
  link_with: pcre2_lib,
  include_directories: include_directories('.', is_system: true),
  link_args: ['-Wl,--exclude-libs,libpcre2.a'],
)

# ---------------------------------------------------------------------------
# yyjson: a single C file (the upstream CMake/meson files are not used).
# The features no library needs are compiled out: file I/O, the JSON
# pointer/patch utilities and the incremental reader.
# ---------------------------------------------------------------------------
yyjson_inc = include_directories('yyjson/src', is_system: true)
yyjson_lib = static_library(
  'yyjson',
  'yyjson/src/yyjson.c',
  include_directories: yyjson_inc,
  c_args: [
    '-DYYJSON_DISABLE_FILE=1',
    '-DYYJSON_DISABLE_UTILS=1',
    '-DYYJSON_DISABLE_INCR_READER=1',
    '-DNDEBUG',
    '-ffile-prefix-map=@0@=.'.format(meson.project_source_root()),
    '-ffile-prefix-map=@0@=.'.format(meson.project_build_root()),
  ],
  override_options: ['warning_level=0'],
  gnu_symbol_visibility: 'hidden',
  pic: true,
  build_by_default: false,
)
# (yyjson's own API is tagged visibility("default"): keep it out of the
# modules' and `lust`'s exports, only `luaopen_lust_*` may be exported)
yyjson_dep = declare_dependency(
  link_with: yyjson_lib,
  include_directories: yyjson_inc,
  compile_args: ['-DYYJSON_DISABLE_FILE=1', '-DYYJSON_DISABLE_UTILS=1', '-DYYJSON_DISABLE_INCR_READER=1'],
  link_args: ['-Wl,--exclude-libs,libyyjson.a'],
)
