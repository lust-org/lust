#!/usr/bin/env python3
"""Guard the reproducibility of the embedded bytecode of libraries.

usage: test_bytecode.py <luajit> <LuaJIT source dir> <lua file>...

The Lua part of a library (every `ffi` one, optionally a `capi` one) is
compiled to bytecode at build time and embedded (libs/ffi_shim.c). LuaJIT randomizes its string hashes per process,
and a table constructor with named fields (`{ a = f(), b = 1 }`, or one with
only constant keys) is dumped as a template laid out by those hashes: the
bytecode, and with it the library and `lust`, would differ from build to
build. Such a constructor has to be written as `local t = {}; t.a = ...`
(array style constructors are fine).

Compiles every file repeatedly and requires identical output.
"""

import os
import subprocess
import sys
import tempfile

RUNS = 8


def compile_once(luajit, src_dir, lua, out):
    env = dict(os.environ, LUA_PATH=os.path.join(src_dir, "?.lua"))
    subprocess.run([luajit, "-b", "-n", "check", lua, out], env=env, check=True)
    with open(out, "rb") as f:
        return f.read()


def main():
    luajit, src_dir, files = sys.argv[1], sys.argv[2], sys.argv[3:]
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "bc.h")
        for lua in files:
            outputs = {compile_once(luajit, src_dir, lua, out) for _ in range(RUNS)}
            name = os.path.relpath(lua, os.path.dirname(os.path.dirname(lua)))
            if len(outputs) == 1:
                print(f"ok   {name}: bytecode is reproducible")
            else:
                print(f"FAIL {name}: bytecode differs between runs of the "
                      f"compiler (see the docstring of this test)")
                ok = False
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
