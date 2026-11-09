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

# ---------------------------------------------------------------------------
# C++: RE2 and its dependency Abseil. Both are built as static archives with
# PIC, hidden symbols (nothing of them may leak out of the modules, a host
# process could have its own Abseil) and NDEBUG (what upstream's release
# builds do; meson would keep the asserts otherwise).
#
# Neither upstream ships a meson build, so the source lists are kept here.
# The Abseil list is not the whole library but the part RE2 reaches (the
# members the linker pulls from Abseil's CMake targets when linking RE2),
# see tools/absl-sources.py. After upgrading vendor/abseil or vendor/re2,
# regenerate/compare it with that script; a stale list shows up as an
# undefined symbol when the library is loaded.
# ---------------------------------------------------------------------------

# Upstream's code: not ours to police, and (-ffile-prefix-map) it must not
# embed the checkout path through __FILE__, which Abseil's logging and the
# CHECK macros do. Builds have to be reproducible regardless of the path.
vendored_cpp_args = [
  '-DNDEBUG',
  # RE2 still calls the pointer overloads Abseil deprecated
  '-Wno-deprecated-declarations',
  '-fvisibility-inlines-hidden',
  '-ffile-prefix-map=@0@=.'.format(meson.project_source_root()),
  '-ffile-prefix-map=@0@=.'.format(meson.project_build_root()),
]

# Link the C++ runtime statically into whatever uses the C++ code. A dynamic
# libstdc++ would end up in the NEEDED list of `lust`, which the loader maps
# on every start (see AGENTS.md); statically linked, only what is used is
# pulled in. Also keeps the modules independent of the system's libstdc++.
# The runtime archives export everything by default: hide all of it (only
# `luaopen_lust_*` and what the `ffi` libraries bind may be exported).
cpp_rt_dep = declare_dependency(
  link_args: [
    '-static-libstdc++',
    '-static-libgcc',
    '-Wl,--exclude-libs,libstdc++.a',
    '-Wl,--exclude-libs,libgcc.a',
    '-Wl,--exclude-libs,libgcc_eh.a',
    '-Wl,--exclude-libs,libsupc++.a',
    '-Wl,--exclude-libs,libre2.a',
    '-Wl,--exclude-libs,libabsl.a',
  ],
)
threads_dep = dependency('threads')

absl_inc = include_directories('abseil', is_system: true)
absl_sources = files(
  'abseil/absl/base/internal/cycleclock.cc',
  'abseil/absl/base/internal/low_level_alloc.cc',
  'abseil/absl/base/internal/raw_logging.cc',
  'abseil/absl/base/internal/spinlock.cc',
  'abseil/absl/base/internal/spinlock_wait.cc',
  'abseil/absl/base/internal/strerror.cc',
  'abseil/absl/base/internal/sysinfo.cc',
  'abseil/absl/base/internal/thread_identity.cc',
  'abseil/absl/base/internal/unscaledcycleclock.cc',
  'abseil/absl/base/throw_delegate.cc',
  'abseil/absl/container/internal/hashtablez_sampler.cc',
  'abseil/absl/container/internal/hashtablez_sampler_force_weak_definition.cc',
  'abseil/absl/container/internal/raw_hash_set.cc',
  'abseil/absl/debugging/internal/address_is_readable.cc',
  'abseil/absl/debugging/internal/decode_rust_punycode.cc',
  'abseil/absl/debugging/internal/demangle.cc',
  'abseil/absl/debugging/internal/demangle_rust.cc',
  'abseil/absl/debugging/internal/elf_mem_image.cc',
  'abseil/absl/debugging/internal/examine_stack.cc',
  'abseil/absl/debugging/internal/utf8_for_code_point.cc',
  'abseil/absl/debugging/internal/vdso_support.cc',
  'abseil/absl/debugging/leak_check.cc',
  'abseil/absl/debugging/stacktrace.cc',
  'abseil/absl/debugging/symbolize.cc',
  'abseil/absl/hash/internal/city.cc',
  'abseil/absl/hash/internal/hash.cc',
  'abseil/absl/log/globals.cc',
  'abseil/absl/log/internal/check_op.cc',
  'abseil/absl/log/internal/globals.cc',
  'abseil/absl/log/internal/log_format.cc',
  'abseil/absl/log/internal/log_message.cc',
  'abseil/absl/log/internal/log_sink_set.cc',
  'abseil/absl/log/internal/nullguard.cc',
  'abseil/absl/log/internal/proto.cc',
  'abseil/absl/log/internal/structured_proto.cc',
  'abseil/absl/log/log_sink.cc',
  'abseil/absl/numeric/int128.cc',
  'abseil/absl/strings/ascii.cc',
  'abseil/absl/strings/charconv.cc',
  'abseil/absl/strings/internal/charconv_bigint.cc',
  'abseil/absl/strings/internal/charconv_parse.cc',
  'abseil/absl/strings/internal/memutil.cc',
  'abseil/absl/strings/internal/str_format/arg.cc',
  'abseil/absl/strings/internal/str_format/bind.cc',
  'abseil/absl/strings/internal/str_format/extension.cc',
  'abseil/absl/strings/internal/str_format/float_conversion.cc',
  'abseil/absl/strings/internal/str_format/output.cc',
  'abseil/absl/strings/internal/str_format/parser.cc',
  'abseil/absl/strings/internal/utf8.cc',
  'abseil/absl/strings/match.cc',
  'abseil/absl/strings/numbers.cc',
  'abseil/absl/strings/str_cat.cc',
  'abseil/absl/synchronization/internal/create_thread_identity.cc',
  'abseil/absl/synchronization/internal/futex_waiter.cc',
  'abseil/absl/synchronization/internal/graphcycles.cc',
  'abseil/absl/synchronization/internal/kernel_timeout.cc',
  'abseil/absl/synchronization/internal/per_thread_sem.cc',
  'abseil/absl/synchronization/internal/waiter_base.cc',
  'abseil/absl/synchronization/mutex.cc',
  'abseil/absl/time/clock.cc',
  'abseil/absl/time/duration.cc',
  'abseil/absl/time/internal/cctz/src/time_zone_fixed.cc',
  'abseil/absl/time/internal/cctz/src/time_zone_if.cc',
  'abseil/absl/time/internal/cctz/src/time_zone_impl.cc',
  'abseil/absl/time/internal/cctz/src/time_zone_info.cc',
  'abseil/absl/time/internal/cctz/src/time_zone_libc.cc',
  'abseil/absl/time/internal/cctz/src/time_zone_lookup.cc',
  'abseil/absl/time/internal/cctz/src/time_zone_posix.cc',
  'abseil/absl/time/internal/cctz/src/zone_info_source.cc',
  'abseil/absl/time/time.cc',
)
absl_lib = static_library(
  'absl',
  absl_sources,
  include_directories: absl_inc,
  dependencies: threads_dep,
  cpp_args: vendored_cpp_args,
  override_options: ['warning_level=0', 'cpp_std=c++17'],
  gnu_symbol_visibility: 'hidden',
  pic: true,
  build_by_default: false,
)
absl_dep = declare_dependency(
  link_with: absl_lib,
  include_directories: absl_inc,
  dependencies: [threads_dep, cpp_rt_dep],
)

re2_inc = include_directories('re2', is_system: true)
re2_lib = static_library(
  're2',
  files(
    're2/re2/bitmap256.cc',
    're2/re2/bitstate.cc',
    're2/re2/compile.cc',
    're2/re2/dfa.cc',
    're2/re2/filtered_re2.cc',
    're2/re2/mimics_pcre.cc',
    're2/re2/nfa.cc',
    're2/re2/onepass.cc',
    're2/re2/parse.cc',
    're2/re2/perl_groups.cc',
    're2/re2/prefilter.cc',
    're2/re2/prefilter_tree.cc',
    're2/re2/prog.cc',
    're2/re2/re2.cc',
    're2/re2/regexp.cc',
    're2/re2/set.cc',
    're2/re2/simplify.cc',
    're2/re2/tostring.cc',
    're2/re2/unicode_casefold.cc',
    're2/re2/unicode_groups.cc',
    're2/util/rune.cc',
    're2/util/strutil.cc',
  ),
  include_directories: re2_inc,
  dependencies: absl_dep,
  cpp_args: vendored_cpp_args,
  override_options: ['warning_level=0', 'cpp_std=c++17'],
  gnu_symbol_visibility: 'hidden',
  pic: true,
  build_by_default: false,
)
re2_dep = declare_dependency(
  link_with: re2_lib,
  include_directories: re2_inc,
  dependencies: absl_dep,
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
