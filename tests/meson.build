# Tests, run with `meson test` (or `./dev.py test`).
#
# Every library's libs/<name>/test.lua runs twice:
#   - "shared": the stock `luajit` from PATH loading the freshly built .so
#   - "static": the `lust` binary, the library coming from package.preload
# Both are pointed away from any installed copy via LUA_CPATH.

tests_dir = meson.current_source_dir()
luajit = find_program('luajit', required: false)

lua_path = tests_dir / '?.lua;;'
shared_cpath = meson.project_build_root() / 'libs' / '?.so'
nowhere_cpath = '/nonexistent/?.so' # force use of the preloaded libraries

if not luajit.found()
  warning('luajit not found in PATH, skipping tests of the shared modules')
endif

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
  if lust_feature.allowed()
    test(
      name + ' (static)',
      lust_exe,
      args: [lib_test],
      env: {'LUA_PATH': lua_path, 'LUA_CPATH': nowhere_cpath},
      suite: ['static', name],
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
  if lust_feature.allowed()
    test(
      'lazy loading (static)',
      lust_exe,
      args: [lazy, 'static'] + enabled_libs,
      env: {'LUA_PATH': lua_path, 'LUA_CPATH': nowhere_cpath},
      suite: ['static', 'lazy'],
    )
  endif
endif
