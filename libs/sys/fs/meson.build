# lust.sys.fs: file system basics (directory listing, stat, mkdir, temporary
# files and directories). Plain libc, no dependencies, so it costs nothing
# at startup and is linked into `lust`.
lib_sources = files('fs.c')
lib_deps = []
# walkdir is written in Lua (embedded as bytecode, loaded on first use).
lib_lua = files('fs.lua')
