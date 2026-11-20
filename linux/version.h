// linux/version.h -- the launcher version, shared by the Windows and Linux launchers (keep it equal
// to LAUNCHER_VERSION in mctde_launcher.cpp and core/install.cpp). Bump all three for every launcher
// release and keep latest.txt's mctde-launcher= line in sync (tools/package_release.sh checks), or the
// self-update can loop.
#pragma once

#define MCTDE_LINUX_VERSION "0.6.1"
