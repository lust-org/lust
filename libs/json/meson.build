# lust.json: JSON, a Lua C API module over the vendored yyjson (see
# yyjson_dep in vendor/meson.build), which is linked statically, so the
# module does not depend on a system libyyjson. It is not an `ffi` library
# on purpose: building the Lua tables and strings dominates a JSON
# conversion, and the JIT cannot speed that up, while `require`ing an FFI
# library costs far more than registering a few functions. Being plain C
# without static constructors it costs nothing at startup, so it is linked
# into `lust` too.
lib_sources = files('json.c')
lib_deps = [yyjson_dep]
