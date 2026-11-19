// core/install.h
//
// Everything the launcher knows about an mctde install that isn't UI or OS-specific: the
// mctde-link.ini settings it owns, the DSFix / Practice Tool enable toggles, dsfix.ini editing,
// first-run ini seeding, and the pre-launch prep (Steam appid, AA guard, overlay caches). Shared
// by the Windows launcher and the Linux one, so both behave identically on the same folder.
#pragma once

#include <string>

namespace mctde {

// This launcher build's version. Bump on each launcher release and keep latest.txt's
// mctde-launcher= line in sync, or self-update can loop.
extern const char* const LAUNCHER_VERSION;
// Bump whenever a release must force-reset mctde-link.ini to the documented defaults. If the ini's
// [Launcher] ConfigVersion doesn't match, the launcher overwrites the whole ini and says so.
extern const char* const INI_CONFIG_VERSION;
// The full, documented default mctde-link.ini (single source of truth for a fresh config).
extern const char* const kDefaultIni;

// Where the greyed-out practice-tool row links when the tool isn't bundled.
extern const char* const PRACTICE_RELEASE_URL;

class Install {
public:
    // dir = the DATA folder (where DARKSOULS.exe and the launcher live). A trailing separator is
    // added if missing.
    explicit Install(std::string dir);

    const std::string& Dir() const { return dir_; }
    std::string Path(const std::string& name) const;   // dir + name
    std::string IniPath() const;                       // mctde-link.ini
    std::string DsfixIniPath() const;                  // dsfix.ini

    // ---- mctde-link.ini: generic access
    std::string Get(const char* section, const char* key, const char* def) const;
    int  GetInt(const char* section, const char* key, int def) const;
    void Set(const char* section, const char* key, const std::string& value) const;

    // ---- mctde-link.ini: settings the launcher's main window owns
    bool PhantomEnabled() const;
    void SetPhantom(bool on) const;      // also makes sure VerifyOnly= exists (absent -> 0)
    bool AaOptIn() const;                // [Launcher] AllowAA
    void SetAaOptIn(bool on) const;
    bool DarkMode() const;
    void SetDarkMode(bool on) const;
    bool AutoUpdate() const;             // default on
    void SetAutoUpdate(bool on) const;
    bool AutoPlay() const;               // [Launcher] AutoPlay, default off
    void SetAutoPlay(bool on) const;
    bool AutoLaunchDscm() const;
    void SetAutoLaunchDscm(bool on) const;
    std::string DscmSavedPath() const;   // a Windows path as seen inside the game's prefix
    void SetDscmSavedPath(const std::string& p) const;

    // Seed a missing ini, or reset one whose ConfigVersion is stale. Returns true only for a
    // reset of an EXISTING ini (so the UI can tell the player their settings were replaced).
    bool EnsureIniSeeded() const;

    // ---- optional DLLs, enabled under their real name and parked as <name>.off
    bool DsfixInstalled() const;
    bool DsfixEnabled() const;
    void ApplyDsfix(bool enable) const;
    std::string ChainloadDir() const;    // [Compatibility] ChainloadFolder, with trailing separator
    bool PracticeInstalled() const;
    bool PracticeEnabled() const;
    void ApplyPractice(bool enable) const;

    // ---- launch prep
    // Pins steam_appid.txt to 480 (the shared mctde matchmaking pool), bakes the overlay caches
    // from the player's own game files, and forces DSFix AA off unless the player opted in.
    void PrepareLaunch() const;
    void EnsureSteamAppId() const;
    void GenerateOverlayCaches() const;
    void ForceAaOffUnlessOptedIn() const;

private:
    std::string dir_;
};

// ---- dsfix.ini ("key value" lines, '#' comments): read / rewrite one key, preserving the rest
std::string DsfixGet(const std::string& text, const std::string& key, const std::string& def);
std::string DsfixSet(const std::string& text, const std::string& key, const std::string& value);

// >0 if a>b, <0 if a<b, 0 if equal (dotted-numeric compare, e.g. "0.5.10" > "0.5.4").
int CompareVersions(const std::string& a, const std::string& b);
// "key=value" lookup in a latest.txt-style file ('#' comments).
std::string ParseKv(const std::string& text, const std::string& key);

} // namespace mctde
