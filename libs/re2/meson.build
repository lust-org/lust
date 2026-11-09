# lust.re2: Google's RE2 regular expressions. RE2 is C++ (and needs Abseil),
# so the module is an `ffi` library with a small extern "C" shim (shim.cc);
# both vendored dependencies are linked statically (re2_dep, see
# vendor/meson.build), and so is the C++ runtime (it comes with re2_dep), so
# the module does not depend on the system's libstdc++.
lib_kind = 'ffi'
lib_lua = files('re2.lua')
lib_sources = files('shim.cc')
lib_deps = [re2_dep]
# Not linked into `lust`: the C++ runtime (iostream/locale setup, the
# emergency exception pool, all pulled in by Abseil's logging and RE2's
# operator<<) runs static constructors at startup worth ~110k instructions,
# which every start of `lust` would pay, used or not. As a .so they only run
# on `require "lust.re2"`; `lust` loads it from the install cmoddir.
lib_embed = false
