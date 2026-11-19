// linux/version.h -- the Linux launcher's own version, separate from the Windows launcher's
// (core/install.cpp LAUNCHER_VERSION). Bump it for every Linux release and keep the
// mctde-launcher-linux= line of latest.txt on the `linux` branch in sync (tools/make_linux_release.sh
// does both checks), or the self-update can loop.
#pragma once

#define MCTDE_LINUX_VERSION "0.2.1"
