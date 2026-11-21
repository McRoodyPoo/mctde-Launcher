// core/install.cpp -- see install.h.
#include "install.h"
#include "caches.h"
#include "platform.h"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace mctde {

const char* const LAUNCHER_VERSION   = "0.7.0";   // == linux/version.h == mctde_launcher.cpp
const char* const INI_CONFIG_VERSION = "0.5.4";
const char* const PRACTICE_RELEASE_URL = "https://github.com/helloeloise/ptde_practice_tool/tree/tasrunner";

#include "default_ini.inc"

#ifdef _WIN32
static const char kSep = '\\';
#else
static const char kSep = '/';
#endif

Install::Install(std::string dir) : dir_(std::move(dir)) {
    if (!dir_.empty() && dir_.back() != '\\' && dir_.back() != '/') dir_ += kSep;
}

std::string Install::Path(const std::string& name) const { return dir_ + name; }
std::string Install::IniPath() const       { return Path("mctde-link.ini"); }
std::string Install::DsfixIniPath() const  { return Path("dsfix.ini"); }

std::string Install::Get(const char* s, const char* k, const char* def) const { return plat::IniGet(IniPath(), s, k, def); }
int  Install::GetInt(const char* s, const char* k, int def) const { return plat::IniGetInt(IniPath(), s, k, def); }
void Install::Set(const char* s, const char* k, const std::string& v) const { plat::IniSet(IniPath(), s, k, v); }

static bool IEquals(const std::string& a, const char* b) {
    if (a.size() != strlen(b)) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

// ---------------------------------------------------------------- mctde-link.ini settings
bool Install::PhantomEnabled() const { return IEquals(Get("PhantomUnleashed", "Mode", "Off"), "On"); }
void Install::SetPhantom(bool on) const {
    Set("PhantomUnleashed", "Mode", on ? "On" : "Off");
    // The in-game engine reads VerifyOnly with a default that means "log only, never patch", so a
    // section that holds only Mode= silently does nothing. Make sure the apply switch is present
    // (only ever write it absent -> 0; never clobber a user who set it to 1 on a suspect exe).
    if (Get("PhantomUnleashed", "VerifyOnly", "").empty())
        Set("PhantomUnleashed", "VerifyOnly", "0");
}

// AA opt-in. By default the launcher forces DSFix antialiasing OFF before every launch: SMAA (its
// stencil-masked edge blend) and SSAO collide with the in-frame overlay and smear the world render
// with red/yellow garbage until a device reset. The DSFix config's Advanced Options can set this
// flag to keep AA; the launch-time force-off then leaves their choice alone. Default 0.
bool Install::AaOptIn() const        { return GetInt("Launcher", "AllowAA", 0) != 0; }
void Install::SetAaOptIn(bool on) const { Set("Launcher", "AllowAA", on ? "1" : "0"); }
bool Install::DarkMode() const       { return GetInt("Launcher", "DarkMode", 0) != 0; }
void Install::SetDarkMode(bool on) const { Set("Launcher", "DarkMode", on ? "1" : "0"); }
bool Install::AutoUpdate() const     { return GetInt("Launcher", "AutoUpdate", 1) != 0; }
void Install::SetAutoUpdate(bool on) const { Set("Launcher", "AutoUpdate", on ? "1" : "0"); }
bool Install::AutoPlay() const       { return GetInt("Launcher", "AutoPlay", 0) != 0; }
void Install::SetAutoPlay(bool on) const { Set("Launcher", "AutoPlay", on ? "1" : "0"); }
bool Install::AutoLaunchDscm() const { return GetInt("Launcher", "AutoLaunchDSCM", 0) != 0; }
void Install::SetAutoLaunchDscm(bool on) const { Set("Launcher", "AutoLaunchDSCM", on ? "1" : "0"); }
std::string Install::DscmSavedPath() const { return Get("Launcher", "DscmPath", ""); }
void Install::SetDscmSavedPath(const std::string& p) const { Set("Launcher", "DscmPath", p); }

// Seed or reset mctde-link.ini to the full documented default.
//   * Missing ini (first run, or the user deleted it): write the complete default silently.
//   * Existing ini whose [Launcher] ConfigVersion != INI_CONFIG_VERSION: this release changed the
//     baked-in defaults, so overwrite the whole file and report it (an upgrade from older builds).
//   * Existing ini already on the current ConfigVersion: left untouched -- the per-key writers and
//     the in-game MigrateIni handle partial/legacy files from here.
bool Install::EnsureIniSeeded() const {
    const std::string ini = IniPath();
    if (!plat::FileExists(ini)) {
        plat::WriteText(ini, kDefaultIni);
        return false;
    }
    if (Get("Launcher", "ConfigVersion", "") != INI_CONFIG_VERSION) {
        plat::WriteText(ini, kDefaultIni);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- optional-DLL enable/disable
// A toggleable DLL is "active" under its real name and "parked" as <name>.off when disabled. We
// never delete it, only rename. The chainloader's *.dll scan (and the game's wrapper loader) both
// skip the .off copy.
static void ToggleParked(const std::string& active, const std::string& parked, bool enable) {
    if (enable) {
        if (!plat::FileExists(active) && plat::FileExists(parked))
            plat::Rename(parked, active, true);
    } else if (plat::FileExists(active)) {
        if (plat::FileExists(parked)) plat::Remove(parked);
        plat::Rename(active, parked, false);
    }
}

// DSFix is the dinput8.dll wrapper that sits beside DARKSOULS.exe.
bool Install::DsfixInstalled() const {
    return plat::FileExists(Path("dinput8.dll")) || plat::FileExists(Path("dinput8.dll.off"));
}
bool Install::DsfixEnabled() const { return plat::FileExists(Path("dinput8.dll")); }
void Install::ApplyDsfix(bool enable) const {
    ToggleParked(Path("dinput8.dll"), Path("dinput8.dll.off"), enable);
}

// Eloise's PTDE Practice Tool is a dinput8.dll, but it's bundled in the ChainloadFolder (default
// mctde-Link_Chainload) under a UNIQUE name -- ptde_practice.dll -- so it doesn't collide with
// DSFix's own dinput8.dll. (Windows keys loaded modules by base name: a chainloaded "dinput8.dll"
// would just return DSFix's handle and never run.) The chainloader scans <folder>\*.dll, so
// parking it as .off disables it.
std::string Install::ChainloadDir() const {
    std::string folder = Get("Compatibility", "ChainloadFolder", "mctde-Link_Chainload");
    if (folder.empty()) folder = "mctde-Link_Chainload";
    return dir_ + folder + kSep;
}
bool Install::PracticeInstalled() const {
    return plat::FileExists(ChainloadDir() + "ptde_practice.dll") ||
           plat::FileExists(ChainloadDir() + "ptde_practice.dll.off");
}
bool Install::PracticeEnabled() const { return plat::FileExists(ChainloadDir() + "ptde_practice.dll"); }
void Install::ApplyPractice(bool enable) const {
    ToggleParked(ChainloadDir() + "ptde_practice.dll", ChainloadDir() + "ptde_practice.dll.off", enable);
}

// ---------------------------------------------------------------- launch prep
// The game runs as Steam appid 480 (Spacewar) so Steam Input / controllers work, and PTDE's online
// matchmaking is keyed on that same appid -- so EVERY install must agree on it: an install left on
// PTDE's real appid (211420) lands in a pool with no other mctde players. steam_appid.txt takes
// precedence over Steam's own env var, so pin it to 480 (rewrite only when it isn't already).
void Install::EnsureSteamAppId() const {
    const std::string p = Path("steam_appid.txt");
    std::string cur = plat::FileExists(p) ? plat::ReadText(p) : std::string();
    const size_t a = cur.find_first_not_of(" \t\r\n");
    const size_t b = cur.find_last_not_of(" \t\r\n");
    const std::string val = (a == std::string::npos) ? std::string() : cur.substr(a, b - a + 1);
    if (val != "480") plat::WriteText(p, "480");
}

// Bake the game's own DSFont24 UI font (+ CJK from the other language packs, for Steam names),
// covenant icons and HP-gauge frame into the overlay's caches. First launch does the extraction;
// later launches are a cheap no-op unless the source files changed. Must exist before the overlay
// loads, so this blocks briefly by design.
void Install::GenerateOverlayCaches() const {
    const std::string cld = ChainloadDir();
    plat::MakeDir(cld);
    caches::GenerateFontCacheIfNeeded(dir_, cld + "mctde_dsfont.bin");
    caches::GenerateIconCacheIfNeeded(dir_, cld + "mctde_dsicons.bin");
    caches::GenerateGaugeCacheIfNeeded(dir_, cld + "mctde_dsgauge.bin");
}

// Unless the player opted into AA via the DSFix config's Advanced Options, force antialiasing off
// in dsfix.ini -- SMAA/SSAO collide with the overlay and corrupt the world render. Only rewrites
// when it's actually on; harmless if DSFix isn't installed.
void Install::ForceAaOffUnlessOptedIn() const {
    if (AaOptIn() || !plat::FileExists(DsfixIniPath())) return;
    std::string ini = plat::ReadText(DsfixIniPath());
    if (DsfixGet(ini, "aaType", "SMAA") != "none")
        plat::WriteText(DsfixIniPath(), DsfixSet(ini, "aaType", "none"));
}

void Install::PrepareLaunch() const {
    EnsureSteamAppId();
    GenerateOverlayCaches();
    ForceAaOffUnlessOptedIn();
}

// ---------------------------------------------------------------- dsfix.ini
// DSFix uses lines of the form "renderWidth 1920" with '#' comments. The whole file is preserved;
// only the token after a known key is rewritten (or the key is appended if missing).
static std::string LineKey(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || line[i] == '#' || line[i] == '\r' || line[i] == '\n') return "";
    size_t j = i;
    while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r' && line[j] != '\n') ++j;
    return line.substr(i, j - i);
}
std::string DsfixGet(const std::string& text, const std::string& key, const std::string& def) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (LineKey(line) != key) continue;
        size_t i = line.find(key) + key.size();
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        size_t j = i;
        while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r' && line[j] != '\n') ++j;
        if (j > i) return line.substr(i, j - i);
        return def;
    }
    return def;
}
std::string DsfixSet(const std::string& text, const std::string& key, const std::string& value) {
    std::istringstream in(text);
    std::string line, out;
    bool found = false;
    const std::string nl = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!found && LineKey(line) == key) { out += key + " " + value + nl; found = true; }
        else out += line + nl;
    }
    if (!found) out += key + " " + value + nl;
    return out;
}

// ---------------------------------------------------------------- versions
static std::string TrimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b - a + 1);
}
std::string ParseKv(const std::string& text, const std::string& key) {
    std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        size_t h = line.find('#'); if (h != std::string::npos) line = line.substr(0, h);
        size_t eq = line.find('='); if (eq == std::string::npos) continue;
        if (TrimStr(line.substr(0, eq)) == key) return TrimStr(line.substr(eq + 1));
    }
    return "";
}
// Leading decimal digits of s (atoi without libc's strtol, which newer glibc headers redirect to a
// glibc-2.38-only symbol -- that would stop the Linux build running on older distros/SteamOS).
static int LeadingInt(const std::string& s) {
    size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    bool neg = i < s.size() && s[i] == '-'; if (neg || (i < s.size() && s[i] == '+')) ++i;
    long v = 0; for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) v = v * 10 + (s[i] - '0');
    return (int)(neg ? -v : v);
}
int CompareVersions(const std::string& a, const std::string& b) {
    std::istringstream as(a), bs(b); std::string p;
    std::vector<int> av, bv;
    while (std::getline(as, p, '.')) av.push_back(LeadingInt(p));
    while (std::getline(bs, p, '.')) bv.push_back(LeadingInt(p));
    size_t n = av.size() > bv.size() ? av.size() : bv.size();
    for (size_t i = 0; i < n; ++i) {
        int x = i < av.size() ? av[i] : 0, y = i < bv.size() ? bv[i] : 0;
        if (x != y) return x - y;
    }
    return 0;
}

} // namespace mctde
