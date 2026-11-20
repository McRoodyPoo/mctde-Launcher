// linux/steam.h
//
// Finds what a native Linux launcher needs to run the Windows game the way Steam would: the Steam
// client, PTDE's Proton prefix (where the saves live), the Proton build Steam has picked for PTDE,
// and the Steam Linux Runtime container that Proton build requires.
//
// Why not plain Wine: DARKSOULS.exe's steam_api.dll must reach a running Steam client. On Linux the
// client is a native program, and only Proton bridges it into the prefix (lsteamclient + its
// steam.exe stand-in). Under plain Wine SteamAPI_Init fails and the game quits with exit code 1.
#pragma once

#include <string>
#include <vector>

namespace steam {

// PTDE's real Steam appid. The game itself still runs as 480 via steam_appid.txt (shared mctde
// matchmaking pool); 211420 only picks the Proton prefix and Proton's per-game settings.
constexpr const char* PTDE_APPID = "211420";
// The appid the game (and DSCM) present to Steam: 480, Spacewar -- the shared mctde matchmaking pool,
// and what Steam should show as "now playing". PTDE_APPID is only used to find PTDE's Proton prefix.
constexpr const char* PLAY_APPID = "480";

struct Env {
    std::string root;            // Steam client install (…/.local/share/Steam)
    std::vector<std::string> libraries;   // every library folder, root first
    std::string compatData;      // …/steamapps/compatdata/211420 (may not exist yet; Proton makes it)
    std::string protonName;      // Steam's internal tool name, e.g. "proton_experimental"
    std::string protonDir;       // directory holding the `proton` script
    std::string runtimeEntry;    // …/SteamLinuxRuntime_N/_v2-entry-point, "" if not required
    std::vector<std::string> notes;   // human-readable discovery notes (for `status`)
};

// Discover everything. Returns false and sets err when something essential is missing (no Steam,
// no Proton, or a required runtime that isn't installed).
bool Discover(Env& env, std::string& err);

// Steam client process state.
bool ClientRunning(int* pidOut = nullptr);
// Start Steam in the background (`steam -silent`) and wait up to timeoutSec for it to come up.
bool StartClientAndWait(int timeoutSec, std::string& err);

// Map a Windows path as seen inside the prefix (Z:\home\…, C:\…) to a Linux path.
std::string WindowsToUnixPath(const Env& env, const std::string& winPath);
// Map a Linux path to the Z:\ form Wine shows the game (used when remembering a DSCM path).
std::string UnixToWindowsPath(const std::string& unixPath);

} // namespace steam
