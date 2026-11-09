# lust.pcre2: PCRE2 (Perl compatible) regular expressions, a Lua C API
# module over the vendored PCRE2 (see pcre2_dep in vendor/meson.build),
# which is linked statically, so the module does not depend on a system
# libpcre2. It is not an `ffi` library on purpose: `require`ing one costs
# the FFI, a `cdef` and bytecode (some 230k instructions), the C API
# version only registers its functions, and a regexp match is dominated by
# PCRE2 itself, so the JIT would gain nothing. Being plain C without
# static constructors it costs nothing at startup, so it is linked into
# `lust` too.
lib_sources = files('pcre2.c')
lib_deps = [pcre2_dep]
