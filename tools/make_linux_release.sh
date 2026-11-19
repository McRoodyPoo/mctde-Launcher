#!/bin/sh
# Build and package a Linux launcher release, and write the latest.txt for the `linux` branch.
#
#   tools/make_linux_release.sh 0.1.1
#
# Produces in dist/:
#   mctde-launcher-linux-<ver>.tar.gz (+ .sha256)   -> upload to a GitHub release tagged linux-v<ver>
#   latest.txt                                     -> commit to the `linux` branch as /latest.txt
#
# The manifest names that exact release asset (never releases/latest), so publishing the Linux
# release can't change the repo's "latest" release that the Windows launcher updates from. Mark it
# as a pre-release anyway. Publish the release BEFORE pushing latest.txt, or Linux launchers will
# try to download an asset that doesn't exist yet.
set -e
cd "$(dirname "$0")/.."
VER="$1"
[ -n "$VER" ] || { echo "usage: $0 <version>   (must match MCTDE_LINUX_VERSION in linux/version.h)" >&2; exit 2; }
SRC_VER=$(sed -n 's/^#define MCTDE_LINUX_VERSION "\(.*\)"/\1/p' linux/version.h)
if [ "$SRC_VER" != "$VER" ]; then
    echo "linux/version.h says $SRC_VER, not $VER. Bump MCTDE_LINUX_VERSION first." >&2; exit 1
fi
case "$VER" in *[!0-9.]*) echo "Use a plain dotted version (e.g. 0.1.1) so update comparison works." >&2; exit 1;; esac

TAG="linux-v$VER"
NAME="mctde-launcher-linux-$VER"
REPO="McRoodyPoo/mctde-Launcher"

rm -rf build bin
./build_linux.sh
[ -x bin/mctde-launcher ] || { echo "The window build failed (is Qt 6 / qmake6 installed?)." >&2; exit 1; }

mkdir -p dist
rm -rf "dist/$NAME" "dist/$NAME.tar.gz" "dist/$NAME.tar.gz.sha256"
mkdir "dist/$NAME"
cp bin/mctde-launcher bin/mctde-launcher-cli dist/README-LINUX.md LICENSE "dist/$NAME/"
strip "dist/$NAME/mctde-launcher" "dist/$NAME/mctde-launcher-cli"
chmod 755 "dist/$NAME/mctde-launcher" "dist/$NAME/mctde-launcher-cli"
(cd dist && tar -czf "$NAME.tar.gz" --owner=0 --group=0 "$NAME" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
rm -rf "dist/$NAME"
SHA=$(cut -d' ' -f1 "dist/$NAME.tar.gz.sha256")

cat > dist/latest.txt <<MANIFEST
# Linux launcher update manifest -- read only by mctde-launcher on Linux. Lives on the \`linux\`
# branch; the Windows launcher reads mctde-Link's main/latest.txt and never sees this file.
mctde-launcher-linux=$VER
mctde-launcher-linux-url=https://github.com/$REPO/releases/download/$TAG/$NAME.tar.gz
mctde-launcher-linux-sha256=$SHA
MANIFEST

echo
echo "Release files in dist/:"
echo "  $NAME.tar.gz  (sha256 $SHA)"
echo "  latest.txt"
echo
echo "Next:"
echo "  1. Create a GitHub PRE-release tagged $TAG and upload dist/$NAME.tar.gz (+ .sha256)."
echo "  2. Commit dist/latest.txt to the linux branch as /latest.txt and push."
