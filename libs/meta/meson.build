# lust.meta: versions of lust, LuaJIT and the bundled dependencies. The
# dependencies' come from their headers (only their include directories
# are used, nothing is linked), for the libraries in this build. A library
# that bundles a dependency adds it here.
lib_sources = files('meta.c')
meta_args = ['-DLUST_VERSION="@0@"'.format(meson.project_version())]
lib_deps = []
# (one dependency may be bundled by several libraries: added once)
meta_seen = []
foreach lib, dep : {
  'hash.xxh': 'xxhash',
  'hash.md5': 'mbedtls',
  'hash.sha1': 'mbedtls',
  'hash.sha256': 'mbedtls',
  'json': 'yyjson',
  'pcre2': 'pcre2',
}
  if lib in enabled_libs and dep not in meta_seen
    meta_seen += dep
    meta_args += '-DMETA_HAVE_' + dep.to_upper()
    lib_deps += get_variable(dep + '_dep').partial_dependency(
      includes: true,
      compile_args: true,
    )
  endif
endforeach
lib_deps += declare_dependency(compile_args: meta_args)
