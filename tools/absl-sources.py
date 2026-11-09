#!/usr/bin/env python3
"""List the Abseil sources RE2 needs, and check vendor/meson.build against it.

usage: tools/absl-sources.py [--check]

vendor/abseil and vendor/re2 ship CMake builds only, so vendor/meson.build
keeps its own source lists. For RE2 the Abseil one is not the whole library
but the part RE2 reaches. It is derived like this: build RE2 and Abseil with
upstream's CMake in a temporary directory, link them, and take the archive
members the linker pulled in (its map file) back to their .cc files.

Run it after upgrading vendor/abseil or vendor/re2 (see vendor/vendor.toml).
Without arguments it prints the list, one path per line, relative to
vendor/. With --check it compares it to the one in vendor/meson.build and
exits non-zero if they differ. Needs cmake, ninja and a C++ compiler.
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENDOR = ROOT / "vendor"

# The abseil targets re2's CMakeLists.txt links (ABSL_DEPS).
ABSL_TARGETS = [
    "base", "strings", "synchronization", "hash", "flat_hash_map",
    "flat_hash_set", "str_format", "log", "check", "fixed_array",
    "inlined_vector", "span", "optional",
]

# RE2_SOURCES of re2's CMakeLists.txt, which this script reads.
CMAKE = """\
cmake_minimum_required(VERSION 3.16)
project(probe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_BUILD_TESTING OFF)
add_subdirectory({absl} absl EXCLUDE_FROM_ALL)
set(RE2_SOURCES {re2_sources})
add_library(re2probe STATIC ${{RE2_SOURCES}})
target_include_directories(re2probe PUBLIC {re2})
target_link_libraries(re2probe PUBLIC {targets})
file(WRITE ${{CMAKE_BINARY_DIR}}/main.cc "int main() {{}}")
add_executable(p ${{CMAKE_BINARY_DIR}}/main.cc)
target_link_libraries(p -Wl,--whole-archive re2probe -Wl,--no-whole-archive)
target_link_libraries(p re2probe -Wl,-Map=${{CMAKE_BINARY_DIR}}/p.map)
"""


def re2_sources():
    """The RE2_SOURCES list of re2's CMakeLists.txt, as absolute paths."""
    text = (VENDOR / "re2" / "CMakeLists.txt").read_text()
    body = re.search(r"set\(RE2_SOURCES\s+(.*?)\)", text, re.S).group(1)
    return [str(VENDOR / "re2" / f) for f in body.split()]


def used_sources():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "CMakeLists.txt").write_text(CMAKE.format(
            absl=VENDOR / "abseil",
            re2=VENDOR / "re2",
            re2_sources=" ".join(re2_sources()),
            targets=" ".join(f"absl::{t}" for t in ABSL_TARGETS),
        ))
        build = tmp / "b"
        for cmd in (
            ["cmake", "-S", str(tmp), "-B", str(build), "-G", "Ninja"],
            ["cmake", "--build", str(build), "--target", "p"],
        ):
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)

        # object file -> source, and archive -> its object files
        ninja = (build / "build.ninja").read_text()
        source_of = {}
        for m in re.finditer(r"^build (\S+\.o):\s+\S+\s+(\S+\.cc)\b", ninja, re.M):
            source_of[m.group(1)] = m.group(2)
        members = {}
        for m in re.finditer(r"^build (\S+/(lib\S+\.a)):\s+\S+\s+([^|\n]*)", ninja, re.M):
            members[m.group(2)] = m.group(3).split()

        # archive members the linker took: "libabsl_x.a(member.o)" in the map
        linked = (build / "p.map").read_text().split("Discarded input")[0]
        sources = set()
        for archive, member in set(re.findall(r"(libabsl_\w+\.a)\(([^)]+)\)", linked)):
            objs = [o for o in members[archive] if Path(o).name == member]
            for o in objs:
                src = Path(source_of[o]).resolve()
                sources.add(str(src.relative_to(VENDOR)))
    return sorted(sources)


def listed_in_meson_build():
    text = (VENDOR / "meson.build").read_text()
    body = re.search(r"absl_sources = files\((.*?)\n\)", text, re.S).group(1)
    return sorted(re.findall(r"'([^']+\.cc)'", body))


def main():
    wanted = used_sources()
    if "--check" not in sys.argv[1:]:
        print("\n".join(wanted))
        return
    have = listed_in_meson_build()
    if have == wanted:
        print(f"ok   vendor/meson.build lists the {len(have)} Abseil sources RE2 needs")
        return
    print("FAIL vendor/meson.build (absl_sources) is out of date:")
    for f in sorted(set(wanted) - set(have)):
        print(f"  missing: {f}")
    for f in sorted(set(have) - set(wanted)):
        print(f"  not needed (or gone): {f}")
    sys.exit(1)


if __name__ == "__main__":
    main()
