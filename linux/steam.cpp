// linux/steam.cpp -- see steam.h.
#include "steam.h"
#include "vdf.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <chrono>
#include <unistd.h>

extern char** environ;

namespace steam {

static bool IsDir(const std::string& p)  { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
static bool IsFile(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }
static std::string Home() { const char* h = getenv("HOME"); return h ? h : ""; }
static std::string RealPath(const std::string& p) {
    char buf[4096];
    return realpath(p.c_str(), buf) ? std::string(buf) : p;
}
static std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

// ---------------------------------------------------------------- Steam root + libraries
static std::string FindRoot(std::vector<std::string>& notes) {
    const std::string h = Home();
    const std::string flatpak = h + "/.var/app/com.valvesoftware.Steam/.local/share/Steam";
    const std::string cands[] = { h + "/.steam/root", h + "/.local/share/Steam", h + "/.steam/steam", h + "/.steam/debian-installation" };
    for (const std::string& c : cands)
        if (IsFile(c + "/steamapps/libraryfolders.vdf") || IsDir(c + "/steamapps"))
            return RealPath(c);
    if (IsDir(flatpak)) notes.push_back("Only the Flatpak Steam was found; the Flatpak sandbox isn't supported yet.");
    return "";
}

static std::vector<std::string> FindLibraries(const std::string& root) {
    std::vector<std::string> libs{ root };
    auto lf = vdf::ParseFile(root + "/steamapps/libraryfolders.vdf");
    if (const vdf::Node* top = lf->Child("libraryfolders")) {
        for (const auto& c : top->children) {
            std::string p = c.second->IsLeaf() ? c.second->value : c.second->Get({"path"});
            if (p.empty() || !IsDir(p + "/steamapps")) continue;
            p = RealPath(p);
            if (std::find(libs.begin(), libs.end(), p) == libs.end()) libs.push_back(p);
        }
    }
    return libs;
}

// Library holding appmanifest_<appid>.acf, or "".
static std::string LibraryOfApp(const std::vector<std::string>& libs, const std::string& appid) {
    for (const std::string& l : libs)
        if (IsFile(l + "/steamapps/appmanifest_" + appid + ".acf")) return l;
    return "";
}

// ---------------------------------------------------------------- PTDE's prefix
// Steam keeps an app's prefix in the library the app is installed in. PTDE's saves live in that
// prefix (pfx/drive_c/users/steamuser/Documents/NBGI/DarkSouls), so it's the one to use. If the
// manifest is gone, fall back to any library that already has the prefix, then the root library.
static std::string FindCompatData(const std::vector<std::string>& libs, std::vector<std::string>& notes) {
    std::string owner = LibraryOfApp(libs, PTDE_APPID);
    if (!owner.empty()) {
        std::string p = owner + "/steamapps/compatdata/" + PTDE_APPID;
        if (IsDir(p)) return p;
        notes.push_back("PTDE's library has no prefix yet; Proton will create one.");
        return p;
    }
    for (const std::string& l : libs) {
        std::string p = l + "/steamapps/compatdata/" + PTDE_APPID;
        if (IsDir(p)) { notes.push_back("PTDE isn't in any Steam library; using its existing prefix in " + l); return p; }
    }
    notes.push_back("No PTDE prefix found; Proton will create a fresh one (no existing saves).");
    return libs.front() + "/steamapps/compatdata/" + PTDE_APPID;
}

// ---------------------------------------------------------------- Proton selection
// The tool Steam has assigned to PTDE, else the global default ("0"), else "".
static std::string CompatToolName(const std::string& root) {
    auto cfg = vdf::ParseFile(root + "/config/config.vdf");
    const vdf::Node* map = cfg->Find({"InstallConfigStore", "Software", "Valve", "Steam", "CompatToolMapping"});
    if (!map) return "";
    std::string n = map->Get({PTDE_APPID, "name"});
    if (n.empty()) n = map->Get({"0", "name"});
    return n;
}

// Steam's own Proton builds have internal names that don't appear on disk; map the known ones to
// their install folder names ("proton_9" -> "Proton 9.0", "proton_63" -> "Proton 6.3").
static std::string SteamToolFolder(const std::string& name) {
    const std::string n = Lower(name);
    if (n == "proton_experimental") return "Proton - Experimental";
    if (n == "proton_hotfix")       return "Proton Hotfix";
    if (n.rfind("proton_", 0) == 0) {
        std::string d = n.substr(7);
        if (!d.empty() && std::all_of(d.begin(), d.end(), ::isdigit)) {
            if (d.size() <= 2) return "Proton " + d + ".0";                // proton_9, proton_10
            return "Proton " + d.substr(0, 1) + "." + d.substr(1);         // proton_63, proton_513
        }
    }
    return "";
}

static bool IsProtonDir(const std::string& d) { return IsFile(d + "/proton") && IsFile(d + "/toolmanifest.vdf"); }

// Custom tools (GE-Proton etc.) in compatibilitytools.d declare their internal name.
static std::string FindCustomTool(const std::string& root, const std::string& name) {
    const std::string dirs[] = { root + "/compatibilitytools.d", "/usr/share/steam/compatibilitytools.d" };
    for (const std::string& base : dirs) {
        DIR* d = opendir(base.c_str());
        if (!d) continue;
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string dir = base + "/" + e->d_name;
            auto ct = vdf::ParseFile(dir + "/compatibilitytool.vdf");
            const vdf::Node* tools = ct->Find({"compatibilitytools", "compat_tools"});
            if (!tools) continue;
            for (const auto& t : tools->children) {
                if (strcasecmp(t.first.c_str(), name.c_str()) != 0) continue;
                std::string inst = t.second->Get({"install_path"}, ".");
                std::string full = (inst == ".") ? dir : (inst[0] == '/' ? inst : dir + "/" + inst);
                if (IsProtonDir(full)) { closedir(d); return full; }
            }
        }
        closedir(d);
    }
    return "";
}

// Highest-versioned "Proton X.Y" folder across libraries (fallback when nothing is configured).
static std::string NewestSteamProton(const std::vector<std::string>& libs) {
    std::string best; double bestV = -1;
    for (const std::string& l : libs) {
        std::string common = l + "/steamapps/common";
        DIR* d = opendir(common.c_str());
        if (!d) continue;
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.rfind("Proton ", 0) != 0) continue;
            double v = atof(n.c_str() + 7);
            if (v > bestV && IsProtonDir(common + "/" + n)) { bestV = v; best = common + "/" + n; }
        }
        closedir(d);
    }
    return best;
}

static std::string FindProton(const std::string& root, const std::vector<std::string>& libs,
                              const std::string& name, std::vector<std::string>& notes) {
    if (!name.empty()) {
        std::string folder = SteamToolFolder(name);
        if (!folder.empty())
            for (const std::string& l : libs) {
                std::string d = l + "/steamapps/common/" + folder;
                if (IsProtonDir(d)) return d;
            }
        std::string custom = FindCustomTool(root, name);
        if (!custom.empty()) return custom;
        notes.push_back("Steam's selected tool '" + name + "' wasn't found on disk; falling back.");
    }
    for (const std::string& l : libs) {
        std::string d = l + "/steamapps/common/Proton - Experimental";
        if (IsProtonDir(d)) return d;
    }
    return NewestSteamProton(libs);
}

// Newer Proton builds must run inside a Steam Linux Runtime container: toolmanifest.vdf names it
// by appid (require_tool_appid). Resolve that to the runtime's _v2-entry-point.
static bool FindRuntime(const Env& env, std::string& entry, std::string& err) {
    auto tm = vdf::ParseFile(env.protonDir + "/toolmanifest.vdf");
    std::string rt = tm->Get({"manifest", "require_tool_appid"});
    if (rt.empty()) { entry.clear(); return true; }
    std::string lib = LibraryOfApp(env.libraries, rt);
    if (!lib.empty()) {
        auto acf = vdf::ParseFile(lib + "/steamapps/appmanifest_" + rt + ".acf");
        std::string dir = acf->Get({"AppState", "installdir"});
        std::string e = lib + "/steamapps/common/" + dir + "/_v2-entry-point";
        if (!dir.empty() && IsFile(e)) { entry = e; return true; }
    }
    err = "This Proton build needs the Steam Linux Runtime (appid " + rt + "), which isn't installed. "
          "Launch any Proton game from Steam once so Steam downloads it.";
    return false;
}

bool Discover(Env& env, std::string& err) {
    env = Env();
    env.root = FindRoot(env.notes);
    if (env.root.empty()) { err = "Steam isn't installed (no Steam folder found under your home)."; return false; }
    env.libraries = FindLibraries(env.root);
    env.compatData = FindCompatData(env.libraries, env.notes);
    env.protonName = CompatToolName(env.root);
    env.protonDir = FindProton(env.root, env.libraries, env.protonName, env.notes);
    if (env.protonDir.empty()) {
        err = "No Proton build found. Install Proton from Steam (Library > Tools > Proton Experimental).";
        return false;
    }
    return FindRuntime(env, env.runtimeEntry, err);
}

// ---------------------------------------------------------------- Steam client process
// Decimal digits at the start of s (avoids atoi/fscanf: see LeadingInt in core/install.cpp).
static int ParsePid(const std::string& s) {
    int v = 0; size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9' && v < 100000000; ++i) v = v * 10 + (s[i] - '0');
    return v;
}

static bool PidIsSteam(int pid) {
    if (pid <= 0) return false;
    std::string comm;
    FILE* f = fopen(("/proc/" + std::to_string(pid) + "/comm").c_str(), "r");
    if (!f) return false;
    char buf[64] = {0};
    if (fgets(buf, sizeof(buf), f)) comm = buf;
    fclose(f);
    return comm.rfind("steam", 0) == 0;
}

bool ClientRunning(int* pidOut) {
    const std::string h = Home();
    // registry.vdf's SteamPID is the client's own record of its live process.
    auto reg = vdf::ParseFile(h + "/.steam/registry.vdf");
    int pid = ParsePid(reg->Get({"Registry", "HKLM", "Software", "Valve", "Steam", "SteamPID"}, "0"));
    if (!PidIsSteam(pid)) {
        FILE* f = fopen((h + "/.steam/steam.pid").c_str(), "r");
        pid = 0;
        if (f) { char b[32] = {0}; pid = fgets(b, sizeof(b), f) ? ParsePid(b) : 0; fclose(f); }
        if (!PidIsSteam(pid)) pid = 0;
    }
    if (pidOut) *pidOut = pid;
    return pid > 0;
}

bool StartClientAndWait(int timeoutSec, std::string& err) {
    if (ClientRunning()) return true;
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    char a0[] = "steam", a1[] = "-silent";
    char* argv[] = { a0, a1, nullptr };
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, "steam", &fa, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) { err = "Couldn't start Steam (is the `steam` command installed?)."; return false; }
    for (int i = 0; i < timeoutSec * 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        waitpid(pid, nullptr, WNOHANG);              // reap the bootstrap if it already handed off
        if (ClientRunning()) {
            // The process is up before it has signed in; give it a moment so the game's
            // SteamAPI_Init doesn't race the login.
            std::this_thread::sleep_for(std::chrono::seconds(8));
            return true;
        }
    }
    err = "Steam didn't start within " + std::to_string(timeoutSec) + " seconds.";
    return false;
}

// ---------------------------------------------------------------- path mapping
std::string WindowsToUnixPath(const Env& env, const std::string& winPath) {
    if (winPath.size() < 3 || winPath[1] != ':') return winPath;
    std::string rest = winPath.substr(2);
    for (auto& c : rest) if (c == '\\') c = '/';
    char drive = (char)tolower((unsigned char)winPath[0]);
    if (drive == 'z') return rest;
    if (drive == 'c') return env.compatData + "/pfx/drive_c" + rest;
    std::string link = env.compatData + "/pfx/dosdevices/" + std::string(1, drive) + ":";
    return RealPath(link) + rest;
}

std::string UnixToWindowsPath(const std::string& unixPath) {
    std::string p = "Z:" + unixPath;
    for (auto& c : p) if (c == '/') c = '\\';
    return p;
}

} // namespace steam
