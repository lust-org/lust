# lust.crc: zlib checksums. Example of a *dynamic* system dependency: the
# module links against the system libz.
lib_sources = files('crc.c')
lib_deps = [dependency('zlib')]
# Linking libz into `lust` would make the loader map it on every start, used
# or not. So only the .so exists, which `lust` loads on demand.
lib_embed = false
