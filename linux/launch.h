// linux/launch.h -- start DARKSOULS.exe / DSCM.exe from the DATA folder through Proton.
#pragma once

#include "steam.h"

#include <string>
#include <sys/types.h>

namespace mctde { class Install; }

namespace launch {

struct Result {
    bool started = false;     // the Proton runner was spawned
    bool exitedEarly = false; // ...and exited within the watch window
    int  exitCode = 0;        // runner exit code (or 128+signal)
    pid_t pid = 0;
    std::string error;        // set when !started, or a human explanation of an early exit
    std::string logPath;      // Proton runner's stdout/stderr
};

// Run the game: Steam must be reachable (started if needed), PrepareLaunch() runs first (appid,
// caches, AA guard), then DARKSOULS.exe (or DATA.exe) runs through Proton in PTDE's prefix with
// MCTDE_VIA_LAUNCHER=1 so mctde-Link's launcher guard lets it through. Watches the runner for
// watchSec seconds and reports a non-zero exit in that window as a failed launch.
Result PlayGame(const steam::Env& env, const mctde::Install& inst, int watchSec);

// Wait for a runner started by PlayGame to exit; returns its exit code.
int WaitForExit(pid_t pid);

// ---- DSCM (Dark Souls Connectivity Mod), run in place from its own folder in the same prefix
// Windows-style argv[0] of a running Wine process with this exe name (case-insensitive), or "".
std::string FindRunningWineExe(const char* exeName);
// Windows-style path of a running DSCM.exe (as Wine reports it), or "".
std::string FindRunningDscm();
// True while Dark Souls (DARKSOULS.exe / DATA.exe) is running under Wine/Proton.
bool GameRunning();
// Linux path to launch DSCM from: a running instance (remembered in the ini), else the remembered
// path, else DSCM.exe in the DATA folder. "" if unknown.
std::string DscmLaunchPath(const steam::Env& env, const mctde::Install& inst);
Result LaunchDscm(const steam::Env& env, const mctde::Install& inst, const std::string& exePath);

} // namespace launch
