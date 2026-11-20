// linux/launch.cpp -- see launch.h.
#include "launch.h"
#include "../core/install.h"
#include "../core/platform.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <strings.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace launch {

static std::string DirOf(const std::string& p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? "." : p.substr(0, s);
}

// Case-insensitive lookup of a file directly inside dir (installs come from Windows).
static std::string FindInDir(const std::string& dir, const char* name) {
    std::string out;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d))
            if (strcasecmp(e->d_name, name) == 0) { out = dir + "/" + e->d_name; break; }
        closedir(d);
    }
    return out;
}

static std::string StripSlash(std::string d) {
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    return d;
}

// Environment for a Proton run: the caller's environment (display, audio, XAUTHORITY...) plus what
// Steam itself would set. Inherited Steam* game variables are dropped so a launcher that Steam
// started for another app can't leak that app's identity into the game.
// SteamAppId / SteamGameId are 480 (Spacewar), not PTDE's 211420: Steam shows "now playing" from
// them, and steam_api prefers SteamAppId over steam_appid.txt -- with 211420 here Steam reported
// "Dark Souls: Prepare to Die Edition" for the game and for DSCM (same env) instead of Spacewar.
// STEAM_COMPAT_APP_ID stays 211420: it only selects Proton's per-app compat config for PTDE's prefix.
static std::vector<std::string> BuildEnv(const steam::Env& env, const std::string& gameDir) {
    std::vector<std::string> out;
    for (char** e = environ; *e; ++e) {
        std::string kv = *e;
        std::string k = kv.substr(0, kv.find('='));
        if (k == "SteamAppId" || k == "SteamGameId" || k == "STEAM_COMPAT_APP_ID" ||
            k == "STEAM_COMPAT_DATA_PATH" || k == "STEAM_COMPAT_CLIENT_INSTALL_PATH" ||
            k == "STEAM_COMPAT_INSTALL_PATH" || k == "MCTDE_VIA_LAUNCHER") continue;
        out.push_back(kv);
    }
    out.push_back("STEAM_COMPAT_CLIENT_INSTALL_PATH=" + env.root);
    out.push_back("STEAM_COMPAT_DATA_PATH=" + env.compatData);
    out.push_back("STEAM_COMPAT_INSTALL_PATH=" + gameDir);
    out.push_back(std::string("STEAM_COMPAT_APP_ID=") + steam::PTDE_APPID);
    out.push_back(std::string("SteamAppId=") + steam::PLAY_APPID);
    out.push_back(std::string("SteamGameId=") + steam::PLAY_APPID);
    out.push_back("MCTDE_VIA_LAUNCHER=1");
    return out;
}

// argv for `<runtime> --verb=V -- <proton> V <exe>` (or just `<proton> V <exe>`).
static std::vector<std::string> BuildArgv(const steam::Env& env, const char* verb, const std::string& exe) {
    std::vector<std::string> a;
    if (!env.runtimeEntry.empty()) {
        a.push_back(env.runtimeEntry);
        a.push_back(std::string("--verb=") + verb);
        a.push_back("--");
    }
    a.push_back(env.protonDir + "/proton");
    a.push_back(verb);
    a.push_back(exe);
    return a;
}

static Result Spawn(const std::vector<std::string>& argv, const std::vector<std::string>& envv,
                    const std::string& cwd, const std::string& logPath) {
    Result r;
    r.logPath = logPath;
    std::vector<char*> av, ev;
    for (const auto& s : argv) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    for (const auto& s : envv) ev.push_back(const_cast<char*>(s.c_str()));
    ev.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    posix_spawn_file_actions_addchdir_np(&fa, cwd.c_str());
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // Own session: the game keeps running after the launcher exits or its terminal closes.
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
    int rc = posix_spawn(&r.pid, av[0], &fa, &attr, av.data(), ev.data());
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) { r.error = std::string("Couldn't start ") + av[0] + ": " + strerror(rc); return r; }
    r.started = true;
    return r;
}

// Poll the runner for up to sec seconds.
static void Watch(Result& r, int sec) {
    for (int i = 0; i < sec * 4; ++i) {
        int st = 0;
        pid_t w = waitpid(r.pid, &st, WNOHANG);
        if (w == r.pid) {
            r.exitedEarly = true;
            r.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

int WaitForExit(pid_t pid) {
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

Result PlayGame(const steam::Env& env, const mctde::Install& inst, int watchSec) {
    Result r;
    const std::string dir = StripSlash(inst.Dir());
    std::string exe = FindInDir(dir, "DARKSOULS.exe");
    if (exe.empty()) exe = FindInDir(dir, "DATA.exe");
    if (exe.empty()) {
        r.error = "Couldn't find DARKSOULS.exe (or DATA.exe) next to the launcher.\n"
                  "Put mctde-launcher in your Dark Souls PTDE DATA folder.";
        return r;
    }
    if (!steam::StartClientAndWait(90, r.error)) return r;

    inst.PrepareLaunch();

    // waitforexitandrun waits for anything else in the prefix to shut down first -- which would
    // hold the game back for as long as DSCM is open. Plain `run` when DSCM shares the prefix.
    const char* verb = FindRunningDscm().empty() ? "waitforexitandrun" : "run";
    r = Spawn(BuildArgv(env, verb, exe), BuildEnv(env, dir), dir, dir + "/mctde_launcher_proton.log");
    if (!r.started) return r;
    Watch(r, watchSec);
    if (r.exitedEarly && r.exitCode != 0) {
        r.error = "Dark Souls closed right after starting (exit code " + std::to_string(r.exitCode) + ").\n"
                  "Proton's output is in " + r.logPath + ". If DSFix is enabled, check DSfix.log too; "
                  "set EnableLogging=1 under [Settings] in mctde-link.ini for mctde-Link's log.";
    }
    return r;
}

// ---------------------------------------------------------------- DSCM
// Windows-style argv[0] of a running Wine/Proton process whose exe is `exeName` (case-insensitive),
// e.g. Z:\home\me\DSCM\DSCM.exe, or "".
std::string FindRunningWineExe(const char* exeName) {
    DIR* d = opendir("/proc");
    if (!d) return "";
    std::string found;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        FILE* f = fopen((std::string("/proc/") + e->d_name + "/cmdline").c_str(), "rb");
        if (!f) continue;
        char buf[4096]; size_t n = fread(buf, 1, sizeof(buf) - 1, f); fclose(f);
        if (n == 0) continue;
        buf[n] = 0;
        // argv[0] of a Wine process is its Windows path, e.g. Z:\home\me\DSCM\DSCM.exe
        std::string a0 = buf;
        size_t s = a0.find_last_of("\\/");
        std::string base = (s == std::string::npos) ? a0 : a0.substr(s + 1);
        if (strcasecmp(base.c_str(), exeName) == 0 && a0.size() > 2 && a0[1] == ':') { found = a0; break; }
    }
    closedir(d);
    return found;
}

std::string FindRunningDscm() { return FindRunningWineExe("DSCM.exe"); }

bool GameRunning() {
    return !FindRunningWineExe("DARKSOULS.exe").empty() || !FindRunningWineExe("DATA.exe").empty();
}

std::string DscmLaunchPath(const steam::Env& env, const mctde::Install& inst) {
    std::string running = FindRunningDscm();
    if (!running.empty()) {
        if (strcasecmp(running.c_str(), inst.DscmSavedPath().c_str()) != 0) inst.SetDscmSavedPath(running);
        return steam::WindowsToUnixPath(env, running);
    }
    std::string saved = inst.DscmSavedPath();
    if (!saved.empty()) {
        std::string p = steam::WindowsToUnixPath(env, saved);
        if (plat::FileExists(p)) return p;
    }
    std::string inData = FindInDir(StripSlash(inst.Dir()), "DSCM.exe");
    return inData;
}

Result LaunchDscm(const steam::Env& env, const mctde::Install& inst, const std::string& exePath) {
    Result r;
    if (exePath.empty() || !plat::FileExists(exePath)) { r.error = "DSCM.exe wasn't found."; return r; }
    if (!FindRunningDscm().empty()) { r.started = true; return r; }   // already up
    if (!steam::StartClientAndWait(90, r.error)) return r;
    const std::string dir = DirOf(exePath);
    // Run in place from its own folder so DSCM's self-updater keeps working.
    r = Spawn(BuildArgv(env, "run", exePath), BuildEnv(env, StripSlash(inst.Dir())), dir,
              StripSlash(inst.Dir()) + "/mctde_launcher_dscm.log");
    if (!r.started) return r;
    // Wait (up to ~30 s, longer on a first-run prefix update) until DSCM's Wine process exists.
    // PlayGame decides between `run` and `waitforexitandrun` by whether DSCM is up, and the latter
    // would make Proton wait on / tear down DSCM's session -- so the game must not race this.
    for (int i = 0; i < 120 && FindRunningDscm().empty(); ++i) {
        int st = 0;
        if (waitpid(r.pid, &st, WNOHANG) == r.pid) {
            r.exitedEarly = true;
            r.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
            r.error = "DSCM exited right away (code " + std::to_string(r.exitCode) + "); see " + r.logPath;
            r.started = false;
            return r;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return r;
}

} // namespace launch
