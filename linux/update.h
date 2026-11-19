// linux/update.h -- auto-update for the Linux launcher and mctde-Link.
//
// Two manifests, read over HTTPS with the system curl:
//   * mctde-Link's main/latest.txt (shared with Windows): the mctde-link= version. d3d9.dll is taken
//     from mctde-Link.zip on mctde-Link's latest release (the asset its in-game updater uses).
//   * mctde-Launcher's `linux` branch latest.txt (Linux only):
//         mctde-launcher-linux=0.1.1
//         mctde-launcher-linux-url=https://github.com/McRoodyPoo/mctde-Launcher/releases/download/<tag>/<file>.tar.gz
//         mctde-launcher-linux-sha256=<64 hex>
//     The URL names an exact release asset (never releases/latest), so Linux releases never become the
//     repo's "latest" that the Windows launcher updates from. The archive must match the SHA-256.
#pragma once

#include <string>

namespace mctde { class Install; }

namespace update {

struct LinuxRelease {
    std::string version, url, sha256;
    bool Valid() const;
};

struct Check {
    bool linkOk = false, linuxOk = false;   // each manifest fetched
    std::string latestLink;                 // mctde-link= from mctde-Link's latest.txt
    LinuxRelease release;                   // from the linux branch's latest.txt
    std::string installedLink;              // d3d9.dll file version ("" if missing)
    bool LinkOutdated() const;
    bool LauncherOutdated() const;
};

// Fetch both manifests (no changes made). Network failures just leave the *Ok flags false.
Check Fetch(const mctde::Install& inst);

// d3d9.dll <- mctde-Link.zip from mctde-Link's latest release (must be a PE). Atomic replace.
bool UpdateLink(const mctde::Install& inst, std::string& err);

// Download, verify and install the Linux launcher release into exeDir, replacing mctde-launcher and
// mctde-launcher-cli (the running one included -- Linux allows replacing a running binary). Rolls back
// on any failure. Doesn't relaunch.
bool UpdateLauncher(const LinuxRelease& rel, const std::string& exeDir, std::string& err);

// Start exePath again (detached, own session) with MCTDE_SKIP_UPDATE=1 so it can't update-loop.
bool Relaunch(const std::string& exePath, const std::string& dataDir);

// True when this process was started by Relaunch (skip the update check this run).
bool JustRelaunched();

// Startup routine shared by the window and `mctde-launcher update`: self-heal a missing d3d9.dll
// (always), then -- when autoUpdate is on and this isn't the run right after a self-update -- update
// an outdated mctde-Link and Linux launcher. launcherUpdated is set when new binaries were installed.
struct Outcome {
    Check check;
    bool linkUpdated = false, launcherUpdated = false;
    std::string error;   // last failure, for display
};
Outcome Run(const mctde::Install& inst, const std::string& exeDir, bool autoUpdate);

} // namespace update
