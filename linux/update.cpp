// linux/update.cpp -- see update.h.
#include "update.h"
#include "pe_version.h"
#include "sha256.h"
#include "version.h"
#include "../core/install.h"
#include "../core/platform.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace update {

static const char* LINK_MANIFEST_URL  = "https://raw.githubusercontent.com/McRoodyPoo/mctde-Link/main/latest.txt";
// The full refs/heads/ form on purpose: GitHub's raw CDN was seen serving the short "/linux/" path
// stale for well past its 5-minute max-age after a push (query strings don't bust it), while the
// refs/heads/ path was already current.
static const char* LINUX_MANIFEST_URL = "https://raw.githubusercontent.com/McRoodyPoo/mctde-Launcher/refs/heads/linux/latest.txt";
static const char* LINK_ZIP_URL       = "https://github.com/McRoodyPoo/mctde-Link/releases/latest/download/mctde-Link.zip";
// Launcher archives may only come from this repo's release assets.
static const char* RELEASE_URL_PREFIX = "https://github.com/McRoodyPoo/mctde-Launcher/releases/download/";

// Testing hook: MCTDE_UPDATE_MANIFEST_URL points the Linux manifest elsewhere (e.g. file:///tmp/x.txt)
// and then also lets the archive URL be a file:// URL. Never set in normal use.
static const char* TestManifest() { const char* v = getenv("MCTDE_UPDATE_MANIFEST_URL"); return (v && *v) ? v : nullptr; }

// ---------------------------------------------------------------- process helpers
// Run argv, optionally capturing stdout (capped). Returns the exit code, or -1 if it couldn't start.
static int RunProcess(const std::vector<std::string>& argv, std::string* out, size_t cap = 64u << 20) {
    std::vector<char*> av;
    for (const auto& s : argv) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    int pfd[2] = {-1, -1};
    if (out && pipe2(pfd, O_CLOEXEC) != 0) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (out) posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
    else posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, av[0], &fa, nullptr, av.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (out) close(pfd[1]);
    if (rc != 0) { if (out) close(pfd[0]); return -1; }
    if (out) {
        char buf[65536]; ssize_t n;
        while ((n = read(pfd[0], buf, sizeof(buf))) != 0) {
            if (n < 0) { if (errno == EINTR) continue; break; }
            if (out->size() < cap) out->append(buf, (size_t)n);
        }
        close(pfd[0]);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

// HTTPS only (redirects too); file:// is allowed only under the testing hook.
static std::vector<std::string> Curl(const std::string& url, int maxSeconds) {
    const char* proto = TestManifest() ? "=https,file" : "=https";
    return { "curl", "-fsSL", "--proto", proto, "--proto-redir", "=https",
             "--connect-timeout", "10", "--max-time", std::to_string(maxSeconds), "--retry", "1",
             "-A", std::string("mctde-launcher-linux/") + MCTDE_LINUX_VERSION, url };
}
static bool HttpGet(const std::string& url, std::string& body) {
    body.clear();
    return RunProcess(Curl(url, 15), &body, 1u << 20) == 0;
}
static bool Download(const std::string& url, const std::string& path) {
    auto a = Curl(url, 180); a.push_back("-o"); a.push_back(path);
    return RunProcess(a, nullptr) == 0;
}

// ---------------------------------------------------------------- manifests
bool LinuxRelease::Valid() const {
    if (version.empty() || sha256.size() != 64) return false;
    for (char c : sha256) if (!isxdigit((unsigned char)c)) return false;
    if (url.rfind(RELEASE_URL_PREFIX, 0) == 0) return true;
    return TestManifest() && url.rfind("file://", 0) == 0;
}
bool Check::LinkOutdated() const {
    return linkOk && !latestLink.empty() && !installedLink.empty() &&
           mctde::CompareVersions(latestLink, installedLink) > 0;
}
bool Check::LauncherOutdated() const {
    return linuxOk && release.Valid() && mctde::CompareVersions(release.version, MCTDE_LINUX_VERSION) > 0;
}

Check Fetch(const mctde::Install& inst) {
    Check c;
    c.installedLink = PeFileVersion(inst.Path("d3d9.dll"));
    std::string body;
    if (HttpGet(LINK_MANIFEST_URL, body)) { c.linkOk = true; c.latestLink = mctde::ParseKv(body, "mctde-link"); }
    if (HttpGet(TestManifest() ? TestManifest() : LINUX_MANIFEST_URL, body)) {
        c.linuxOk = true;
        c.release.version = mctde::ParseKv(body, "mctde-launcher-linux");
        c.release.url     = mctde::ParseKv(body, "mctde-launcher-linux-url");
        c.release.sha256  = mctde::ParseKv(body, "mctde-launcher-linux-sha256");
        for (auto& ch : c.release.sha256) ch = (char)tolower((unsigned char)ch);
    }
    return c;
}

// ---------------------------------------------------------------- mctde-Link
// Name (path inside the zip) of the first entry whose basename is `base`, case-insensitive.
static std::string FindZipEntry(const std::string& listing, const char* base) {
    size_t i = 0;
    while (i < listing.size()) {
        size_t j = listing.find('\n', i);
        if (j == std::string::npos) j = listing.size();
        std::string name = listing.substr(i, j - i);
        if (!name.empty() && name.back() == '\r') name.pop_back();
        size_t s = name.find_last_of('/');
        std::string b = s == std::string::npos ? name : name.substr(s + 1);
        if (strcasecmp(b.c_str(), base) == 0) return name;
        i = j + 1;
    }
    return "";
}

// Pull one file (by basename, anywhere in the archive) out of a zip into outPath, with whichever
// extractor the system has: unzip, bsdtar (libarchive -- on SteamOS/Arch), or Python's zipfile.
static bool ExtractFromZip(const std::string& zip, const char* base, const std::string& outPath) {
    struct Tool { std::vector<std::string> list; std::vector<std::string> cat; };
    const std::string py = "import sys,zipfile; z=zipfile.ZipFile(sys.argv[1]); ";
    const Tool tools[] = {
        { { "unzip", "-Z1", zip }, { "unzip", "-p", zip } },
        { { "bsdtar", "-tf", zip }, { "bsdtar", "-xOf", zip } },
        { { "python3", "-I", "-c", py + "print('\\n'.join(z.namelist()))", zip },
          { "python3", "-I", "-c", py + "sys.stdout.buffer.write(z.read(sys.argv[2]))", zip } },
    };
    for (const Tool& t : tools) {
        std::string listing;
        if (RunProcess(t.list, &listing, 1u << 20) != 0) continue;
        std::string entry = FindZipEntry(listing, base);
        if (entry.empty()) return false;   // the tool works; the file just isn't in the zip
        std::vector<std::string> cat = t.cat; cat.push_back(entry);
        std::string data;
        if (RunProcess(cat, &data, 64u << 20) != 0 || data.empty()) continue;
        return plat::WriteAtomic(outPath, data.data(), data.size());
    }
    return false;
}

// mctde-Link ships as mctde-Link.zip on its releases (the same asset its in-game updater uses); the
// launcher only replaces d3d9.dll from it, leaving the player's mctde-link.ini alone.
bool UpdateLink(const mctde::Install& inst, std::string& err) {
    const std::string zip = inst.Path("mctde-Link_update.zip"), tmp = inst.Path("d3d9.dll.new");
    auto fail = [&](const char* m) { plat::Remove(zip); plat::Remove(tmp); err = m; return false; };
    if (!Download(LINK_ZIP_URL, zip)) return fail("Couldn't download mctde-Link.");
    if (!ExtractFromZip(zip, "d3d9.dll", tmp))
        return fail("Couldn't unpack d3d9.dll from mctde-Link.zip (needs unzip, bsdtar or python3).");
    std::vector<uint8_t> d = plat::ReadAll(tmp);
    if (d.size() < 1024 || d[0] != 'M' || d[1] != 'Z') return fail("The mctde-Link download didn't contain a valid d3d9.dll.");
    if (!plat::Rename(tmp, inst.Path("d3d9.dll"), true)) return fail("Couldn't replace d3d9.dll.");
    plat::Remove(zip);
    return true;
}

// ---------------------------------------------------------------- Linux launcher
static bool IsElf(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    unsigned char m[4] = {0};
    size_t n = fread(m, 1, 4, f); fclose(f);
    return n == 4 && m[0] == 0x7f && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
}
static void RemoveTree(const std::string& dir) {
    RunProcess({ "rm", "-rf", "--", dir }, nullptr);
}
// The single top-level folder an archive extracted into (or dir itself if files sit at the root).
static std::string PackageRoot(const std::string& dir) {
    std::string only; int count = 0;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            ++count; only = dir + "/" + e->d_name;
        }
        closedir(d);
    }
    struct stat st;
    return (count == 1 && stat(only.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ? only : dir;
}

bool UpdateLauncher(const LinuxRelease& rel, const std::string& exeDir, std::string& err) {
    if (!rel.Valid()) { err = "The Linux update manifest is incomplete or points outside the mctde-Launcher releases."; return false; }
    const char* names[] = { "mctde-launcher", "mctde-launcher-cli" };
    for (const char* n : names) plat::Remove(exeDir + "/" + n + ".old");   // leftovers of the last update

    const std::string work = exeDir + "/.mctde-update";
    RemoveTree(work);
    if (mkdir(work.c_str(), 0755) != 0 || mkdir((work + "/x").c_str(), 0755) != 0) {
        err = "Couldn't create " + work + " (is the DATA folder writable?)."; return false;
    }
    auto fail = [&](const std::string& m) { RemoveTree(work); err = m; return false; };

    const std::string pkg = work + "/pkg.tar.gz";
    if (!Download(rel.url, pkg)) return fail("Couldn't download the launcher update.");
    std::string got = Sha256FileHex(pkg);
    if (got != rel.sha256) return fail("The launcher update failed its checksum (expected " + rel.sha256 + ", got " + got + ").");
    if (RunProcess({ "tar", "-xzf", pkg, "-C", work + "/x", "--no-same-owner", "--no-same-permissions" }, nullptr) != 0)
        return fail("Couldn't unpack the launcher update.");

    const std::string root = PackageRoot(work + "/x");
    std::vector<std::string> present;
    for (const char* n : names) {
        std::string src = root + "/" + n;
        if (access(src.c_str(), F_OK) != 0) continue;
        if (!IsElf(src)) return fail(std::string("The update's ") + n + " isn't a Linux program.");
        present.push_back(n);
    }
    if (present.empty() || std::find(present.begin(), present.end(), "mctde-launcher") == present.end())
        return fail("The update archive doesn't contain mctde-launcher.");

    // Swap: current -> .old, new -> current. Any failure puts everything back.
    std::vector<std::string> moved;   // names whose current binary was moved to .old
    std::vector<std::string> placed;  // names whose new binary is in place
    auto rollback = [&]() {
        for (const std::string& n : placed) unlink((exeDir + "/" + n).c_str());
        for (const std::string& n : moved) rename((exeDir + "/" + n + ".old").c_str(), (exeDir + "/" + n).c_str());
    };
    for (const std::string& n : present) {
        std::string dst = exeDir + "/" + n, src = root + "/" + n;
        if (access(dst.c_str(), F_OK) == 0) {
            if (rename(dst.c_str(), (dst + ".old").c_str()) != 0) { rollback(); return fail("Couldn't move the old " + n + " aside."); }
            moved.push_back(n);
        }
        if (rename(src.c_str(), dst.c_str()) != 0) { rollback(); return fail("Couldn't install the new " + n + "."); }
        placed.push_back(n);
        chmod(dst.c_str(), 0755);
    }
    RemoveTree(work);
    return true;
}

bool JustRelaunched() {
    const char* v = getenv("MCTDE_SKIP_UPDATE");
    return v && *v;
}

bool Relaunch(const std::string& exePath, const std::string& dataDir) {
    std::vector<std::string> env;
    for (char** e = environ; *e; ++e) if (strncmp(*e, "MCTDE_SKIP_UPDATE=", 18) != 0) env.push_back(*e);
    env.push_back("MCTDE_SKIP_UPDATE=1");
    std::vector<char*> ev;
    for (auto& s : env) ev.push_back(const_cast<char*>(s.c_str()));
    ev.push_back(nullptr);
    std::string dirArg = dataDir;
    char a0[] = "mctde-launcher", a1[] = "--dir";
    char* argv[] = { a0, a1, &dirArg[0], nullptr };
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, exePath.c_str(), nullptr, &attr, argv, ev.data());
    posix_spawnattr_destroy(&attr);
    return rc == 0;
}

Outcome Run(const mctde::Install& inst, const std::string& exeDir, bool autoUpdate) {
    Outcome o;
    // Self-heal: a missing d3d9.dll breaks the mod entirely, so re-fetch it regardless of the
    // Auto-Update toggle (same as the Windows launcher).
    if (!plat::FileExists(inst.Path("d3d9.dll"))) {
        if (UpdateLink(inst, o.error)) o.linkUpdated = true;
    }
    o.check = Fetch(inst);
    if (!autoUpdate || JustRelaunched()) return o;
    if (o.check.LinkOutdated()) {
        if (UpdateLink(inst, o.error)) { o.linkUpdated = true; o.check.installedLink = PeFileVersion(inst.Path("d3d9.dll")); }
    }
    if (o.check.LauncherOutdated()) {
        if (UpdateLauncher(o.check.release, exeDir, o.error)) o.launcherUpdated = true;
    }
    return o;
}

} // namespace update
