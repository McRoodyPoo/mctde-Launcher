#!/bin/sh
# Builds the native Linux mctde launcher (the Windows launcher builds with build_launcher.bat).
#   bin/mctde-launcher      launcher window (Qt 6); run with a command for the CLI
#   bin/mctde-launcher-cli  command line only, no Qt needed (fallback / scripting)
# Needs a C++17 compiler; the window also needs Qt 6 base dev files (qmake6).
# Copy them into the PTDE DATA folder beside DARKSOULS.exe.
set -e
cd "$(dirname "$0")"
CXX="${CXX:-g++}"
mkdir -p bin build
"$CXX" -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -I. \
    core/install.cpp core/caches.cpp core/platform_posix.cpp \
    linux/cli_main.cpp linux/cli.cpp linux/steam.cpp linux/launch.cpp linux/vdf.cpp \
    linux/pe_version.cpp linux/update.cpp linux/sha256.cpp linux/exe_opener.cpp \
    -o bin/mctde-launcher-cli
echo "Built: bin/mctde-launcher-cli"

QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake-qt6 || true)}"
if [ -z "$QMAKE" ]; then
    echo "qmake6 not found -- skipping the window build (install Qt 6 base dev files)." >&2
    exit 0
fi
(cd build && "$QMAKE" ../mctde-launcher.pro >/dev/null && make -s -j"$(nproc)")
echo "Built: bin/mctde-launcher"
