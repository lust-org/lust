# lust.pcre2: PCRE2 (Perl compatible) regular expressions. PCRE2 is plain C
# but its API is full of opaque handles, macros and out parameters, so the
# module is an `ffi` library with a small shim (shim.c) in front of it;
# re-exporting PCRE2 itself would leak its whole API out of the module. The
# vendored library is linked statically (pcre2_dep, see vendor/meson.build),
# so the module does not depend on a system libpcre2. Being plain C without
# static constructors it costs nothing at startup, so it is linked into
# `lust` too.
lib_kind = 'ffi'
lib_lua = files('pcre2.lua')
lib_sources = files('shim.c')
lib_deps = [pcre2_dep]
