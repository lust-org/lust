# Tests, run with `meson test` (or `./dev.py test`).
#
# Every library's libs/<name>/test.lua runs twice:
#   - "shared": the stock `luajit` from PATH loading the freshly built .so
#   - "static": the `lust` binary, the library coming from its embedded set
#     (libraries with lib_embed = false are loaded as .so by `lust`)
# Both are pointed away from any installed copy via LUA_CPATH.

tests_dir = meson.current_source_dir()
luajit = find_program('luajit', required: false)

lua_path = tests_dir / '?.lua;;'
shared_cpath = meson.project_build_root() / 'libs' / '?.so'
nowhere_cpath = '/nonexistent/?.so' # force use of the preloaded libraries

if not luajit.found()
  warning('luajit not found in PATH, skipping tests of the shared modules')
endif

embedded_libs = []
foreach name, info : lib_infos
  if info['embed']
    embedded_libs += name
  endif
endforeach

foreach name : enabled_libs
  lib_test = files('../libs' / name / 'test.lua')

  if luajit.found()
    test(
      name + ' (shared)',
      luajit,
      args: [lib_test],
      env: {'LUA_PATH': lua_path, 'LUA_CPATH': shared_cpath},
      depends: [shared_mods],
      suite: ['shared', name],
    )
  endif
  if lust_feature.allowed() and lib_infos[name]['embed']
    test(
      name + ' (static)',
      lust_exe,
      args: [lib_test],
      env: {'LUA_PATH': lua_path, 'LUA_CPATH': nowhere_cpath},
      suite: ['static', name],
    )
  elif lust_feature.allowed()
    test(
      name + ' (lust, .so)',
      lust_exe,
      args: [lib_test],
      env: {'LUA_PATH': lua_path, 'LUA_CPATH': shared_cpath},
      depends: [shared_mods],
      suite: ['lust-so', name],
    )
  endif
endforeach

# Loading one library must not drag in the others.
if enabled_libs.length() > 1
  lazy = files('test_lazy.lua')
  if luajit.found()
    test(
      'lazy loading (shared)',
      luajit,
      args: [lazy, 'shared'] + enabled_libs,
      env: {'LUA_PATH': lua_path, 'LUA_CPATH': shared_cpath},
      depends: [shared_mods],
      suite: ['shared', 'lazy'],
    )
  endif
  if lust_feature.allowed() and embedded_libs.length() > 1
    test(
      'lazy loading (static)',
      lust_exe,
      args: [lazy, 'static'] + embedded_libs,
      env: {'LUA_PATH': lua_path, 'LUA_CPATH': nowhere_cpath},
      suite: ['static', 'lazy'],
    )
  endif
endif

# Every library's doc-shortform.txt lists exactly what the module exports,
# and `lust`'s usage text contains them.
doc_args = []
foreach name, info : lib_infos
  doc_args += '@0@=@1@'.format(name, info['doc'][0].full_path())
endforeach
test(
  'library documentation',
  lust_feature.allowed() ? lust_exe : luajit,
  args: [files('test_doc.lua')] + doc_args,
  env: {'LUA_PATH': lua_path, 'LUA_CPATH': shared_cpath},
  depends: [shared_mods],
  suite: ['doc'],
)
if lust_feature.allowed()
  test(
    'usage text',
    py,
    args: [files('test_usage.py'), lust_exe] + doc_args,
    suite: ['doc'],
  )
endif

# Startup budget of `lust` compared to plain luajit (see the script).
if lust_feature.allowed()
  test(
    'startup budget',
    py,
    args: [
      files('test_startup.py'),
      lust_exe,
      luajit_exe,
      embedded_libs.length().to_string(),
    ],
    suite: ['startup'],
  )
endif

# Benchmark regressions: every library's libs/<name>/bench/*.lua, in both
# flavours, must stay within a few percent of the instruction counts in
# libs/<name>/bench/baseline.txt (see the script). The counts are only
# meaningful for the default optimized build.
bench_skip = []
if get_option('buildtype') != 'release'
  bench_skip = ['--skip', 'buildtype is not release']
elif '@0@'.format(get_option('b_sanitize')) not in ['none', '[]']
  # (a string up to meson 1.7, an array from 1.8 on)
  bench_skip = ['--skip', 'built with sanitizers']
elif get_option('luajit_xcflags') != ''
  bench_skip = ['--skip', 'custom luajit_xcflags']
endif
foreach name : enabled_libs
  bench_dir = meson.project_source_root() / 'libs' / name / 'bench'
  if not fs.is_dir(bench_dir)
    error('libs/@0@ has no bench/ (see "Adding a library" in README.md)'.format(name))
  endif
  # The vendored luajit (the VM build `lust` uses), not the one from PATH:
  # its counts must not depend on what the system has installed.
  flavours = ['--flavour', 'luajit', luajit_exe, shared_cpath]
  if lust_feature.allowed()
    flavours += [
      '--flavour',
      'lust',
      lust_exe,
      lib_infos[name]['embed'] ? nowhere_cpath : shared_cpath,
    ]
  endif
  test(
    name + ' (bench)',
    py,
    args: [files('test_bench.py'), bench_dir] + flavours + bench_skip,
    depends: [shared_mods],
    suite: ['bench', name],
    # Every benchmark runs RUNS times per flavour (in parallel, but the
    # machine may be busy).
    timeout: 300,
  )
endforeach

# The embedded bytecode of `ffi` libraries has to be reproducible.
ffi_luas = []
foreach name, info : lib_infos
  if info['kind'] == 'ffi'
    ffi_luas += info['lua']
  endif
endforeach
if ffi_luas.length() > 0
  test(
    'ffi bytecode is reproducible',
    py,
    args: [
      files('test_bytecode.py'),
      luajit_exe,
      luajit_src / 'src',
      ffi_luas,
    ],
    suite: ['bytecode'],
  )
endif
