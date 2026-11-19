#!/usr/bin/env bash
# Package an mctde launcher release: one zip per platform, with docs and checksums.
#
#   tools/package_release.sh 0.6.0
#
# Inputs:
#   bin/mctde_launcher.exe   Windows build (build_launcher.bat, MSVC). Override with EXE=...
#   the Linux build          built here by ./build_linux.sh (needs Qt 6 / qmake6)
#
# Output in dist/v<version>/ -- attach everything except legacy-linux/ to one GitHub release
# tagged v<version>, marked as the latest release:
#   mctde-launcher-windows.zip (+.sha256)  mctde_launcher.exe + README.md, CHANGELOG.md, LICENSE
#   mctde-launcher-linux.zip   (+.sha256)  mctde-launcher, mctde-launcher-cli + README-LINUX.md,
#                                          CHANGELOG-LINUX.md, LICENSE
#   mctde_launcher.exe (+.sha256)          bare exe: pre-0.6.0 Windows launchers and mctde-Link's
#                                          "install the launcher" prompt fetch it from releases/latest
#   mctde-launcher-linux-<ver>.tar.gz (+.sha256)  for pre-0.6.0 Linux launchers / installers
# and dist/v<version>/legacy-linux/latest.txt, the manifest those older Linux builds read from the
# `linux` branch.
set -euo pipefail
cd "$(dirname "$0")/.."

VER=${1:-}
[[ -n $VER ]] || { echo "usage: $0 <version>" >&2; exit 2; }
[[ $VER =~ ^[0-9]+(\.[0-9]+)*$ ]] || { echo "Use a plain dotted version (e.g. 0.6.0)." >&2; exit 1; }
WIN_VER=$(sed -n 's/^static const char\* LAUNCHER_VERSION = "\(.*\)";/\1/p' mctde_launcher.cpp)
CORE_VER=$(sed -n 's/^const char\* const LAUNCHER_VERSION *= "\([^"]*\)".*/\1/p' core/install.cpp)
LNX_VER=$(sed -n 's/^#define MCTDE_LINUX_VERSION "\(.*\)"/\1/p' linux/version.h)
for pair in "mctde_launcher.cpp:$WIN_VER" "core/install.cpp:$CORE_VER" "linux/version.h:$LNX_VER"; do
    [[ ${pair#*:} == "$VER" ]] || { echo "${pair%%:*} says ${pair#*:}, not $VER. Bump all three first." >&2; exit 1; }
done

EXE=${EXE:-bin/mctde_launcher.exe}
[[ -f $EXE ]] || { echo "Missing $EXE (build_launcher.bat on Windows)." >&2; exit 1; }
[[ $(head -c2 "$EXE") == MZ ]] || { echo "$EXE isn't a Windows program." >&2; exit 1; }
if ! strings -el "$EXE" 2>/dev/null | grep -qx "$VER" && ! strings -a "$EXE" | grep -qx "$VER"; then
    echo "Warning: $EXE doesn't seem to contain version $VER -- is it a fresh build?" >&2
fi
for d in README.md CHANGELOG.md LICENSE dist/README-LINUX.md CHANGELOG-LINUX.md; do
    [[ -f $d ]] || { echo "Missing $d." >&2; exit 1; }
done

echo "Building the Linux launcher..."
rm -rf build bin/mctde-launcher bin/mctde-launcher-cli
./build_linux.sh
[[ -x bin/mctde-launcher ]] || { echo "The Linux build failed (is Qt 6 / qmake6 installed?)." >&2; exit 1; }

OUT=dist/v$VER
rm -rf "$OUT"; mkdir -p "$OUT/stage-win" "$OUT/stage-linux" "$OUT/legacy-linux"

# zip <out.zip> <file>...: files at the zip root, Unix permissions kept.
mkzip() {
    python3 -I - "$@" <<'PY'
import os, sys, zipfile
out, files = sys.argv[1], sys.argv[2:]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for f in files:
        z.write(f, os.path.basename(f))
PY
}

# Windows
cp "$EXE" "$OUT/stage-win/mctde_launcher.exe"
cp README.md CHANGELOG.md LICENSE "$OUT/stage-win/"
mkzip "$OUT/mctde-launcher-windows.zip" "$OUT"/stage-win/{mctde_launcher.exe,README.md,CHANGELOG.md,LICENSE}
cp "$OUT/stage-win/mctde_launcher.exe" "$OUT/mctde_launcher.exe"

# Linux
cp bin/mctde-launcher bin/mctde-launcher-cli dist/README-LINUX.md CHANGELOG-LINUX.md LICENSE "$OUT/stage-linux/"
strip "$OUT/stage-linux/mctde-launcher" "$OUT/stage-linux/mctde-launcher-cli"
chmod 755 "$OUT/stage-linux/mctde-launcher" "$OUT/stage-linux/mctde-launcher-cli"
mkzip "$OUT/mctde-launcher-linux.zip" \
    "$OUT"/stage-linux/{mctde-launcher,mctde-launcher-cli,README-LINUX.md,CHANGELOG-LINUX.md,LICENSE}

# Legacy Linux tarball (same layout older launchers expect: one top-level folder).
LEG=mctde-launcher-linux-$VER
mkdir "$OUT/$LEG"
cp "$OUT"/stage-linux/{mctde-launcher,mctde-launcher-cli,README-LINUX.md,LICENSE} "$OUT/$LEG/"
(cd "$OUT" && tar -czf "$LEG.tar.gz" --owner=0 --group=0 "$LEG")
rm -rf "$OUT/$LEG" "$OUT/stage-win" "$OUT/stage-linux"

(cd "$OUT" && for f in *.zip *.tar.gz mctde_launcher.exe; do sha256sum "$f" > "$f.sha256"; done)
LEG_SHA=$(cut -d' ' -f1 "$OUT/$LEG.tar.gz.sha256")
cat > "$OUT/legacy-linux/latest.txt" <<MANIFEST
# Update manifest for Linux launchers older than 0.6.0 (and installers older than 0.4.0), read from
# the \`linux\` branch. Newer builds read mctde-Link's latest.txt (mctde-launcher=) instead.
mctde-launcher-linux=$VER
mctde-launcher-linux-url=https://github.com/McRoodyPoo/mctde-Launcher/releases/download/v$VER/$LEG.tar.gz
mctde-launcher-linux-sha256=$LEG_SHA
MANIFEST

echo
echo "Release files in $OUT:"
(cd "$OUT" && ls -l)
echo
for z in "$OUT"/*.zip; do
    echo "$(basename "$z"):"
    python3 -I -c 'import sys,zipfile; [print("  "+n) for n in zipfile.ZipFile(sys.argv[1]).namelist()]' "$z"
done
cat <<EOF

Next:
  1. GitHub release in mctde-Launcher tagged v$VER, marked LATEST (not a pre-release). Attach every
     file in $OUT except the legacy-linux folder.
  2. Copy $OUT/legacy-linux/latest.txt over latest.txt at the repo root, commit, push main, and
     fast-forward the linux branch to main (git push origin main:linux) so older Linux builds see it.
  3. In mctde-Link's latest.txt set mctde-launcher=$VER.
EOF
