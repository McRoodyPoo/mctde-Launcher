// linux/exe_opener.cpp -- see exe_opener.h.
#include "exe_opener.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

extern char** environ;

namespace opener {
namespace {

const char* const kDesktopId = "mctde-open-exe.desktop";
// Windows executables as shared-mime-info names them (x-msdownload is what .exe files resolve to
// on current desktops; the others are older names / aliases some file managers still use).
const char* const kMimes[] = {
    "application/x-msdownload",
    "application/x-ms-dos-executable",
    "application/vnd.microsoft.portable-executable",
};

std::string Home() { const char* h = getenv("HOME"); return h ? h : ""; }
std::string DataHome() {
    const char* x = getenv("XDG_DATA_HOME");
    return (x && *x) ? x : Home() + "/.local/share";
}
std::string ConfigHome() {
    const char* x = getenv("XDG_CONFIG_HOME");
    return (x && *x) ? x : Home() + "/.config";
}
std::string StateDir()    { return DataHome() + "/mctde-launcher"; }
std::string ScriptPath()  { return StateDir() + "/open-exe"; }
std::string PrevPath()    { return StateDir() + "/previous-exe-handlers"; }
std::string DesktopPath() { return DataHome() + "/applications/" + kDesktopId; }

void MkDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
}

bool WriteFile(const std::string& path, const std::string& text, mode_t mode = 0644) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    f.close();
    chmod(path.c_str(), mode);
    return bool(f);
}
std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// Run argv (PATH lookup), optionally capturing stdout. Returns the exit code, or -1.
int Run(const std::vector<std::string>& args, std::string* out = nullptr) {
    std::vector<char*> av;
    for (auto& a : args) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);
    int pipefd[2] = {-1, -1};
    if (out && pipe(pipefd) != 0) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (out) { posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1); posix_spawn_file_actions_addclose(&fa, pipefd[0]); }
    else posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, av[0], &fa, nullptr, av.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (out) {
        close(pipefd[1]);
        if (rc == 0) {
            char buf[512]; ssize_t n;
            while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) out->append(buf, size_t(n));
        }
        close(pipefd[0]);
    }
    if (rc != 0) return -1;
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    return s;
}
std::string QueryDefault(const char* mime) {
    std::string out;
    if (Run({"xdg-mime", "query", "default", mime}, &out) != 0) return "";
    return Trim(out);
}

// The opener script. Runs for every double-clicked Windows executable, so it stays POSIX sh and
// falls through to the previous handler for anything that isn't an mctde game exe.
std::string Script() {
    return R"SH(#!/bin/sh
# mctde .exe opener (installed by mctde-launcher; remove with: mctde-launcher integrate off).
# DARKSOULS.exe / DATA.exe next to an mctde Linux launcher opens the launcher, as the launcher guard
# does on Windows. Every other .exe goes to the handler that opened it before (normally Wine).
f="$1"
case "$f" in file://*) f=$(printf '%b' "$(printf '%s' "${f#file://}" | sed 's/%/\\x/g')") ;; esac
d=$(dirname -- "$f")
b=$(basename -- "$f" | tr '[:upper:]' '[:lower:]')
if [ "$b" = darksouls.exe ] || [ "$b" = data.exe ]; then
    if [ -x "$d/mctde-launcher" ] &&
       ! grep -qiE '^[[:space:]]*RequireLauncher[[:space:]]*=[[:space:]]*0' "$d/mctde-link.ini" 2>/dev/null; then
        exec "$d/mctde-launcher" --dir "$d"
    fi
fi
state="${XDG_DATA_HOME:-$HOME/.local/share}/mctde-launcher/previous-exe-handlers"
prev=$(sed -n 's/^application\/x-msdownload=//p' "$state" 2>/dev/null)
[ -n "$prev" ] || prev=$(sed -n 's/^[^=]*=\(..*\)$/\1/p' "$state" 2>/dev/null | head -n 1)
if [ -n "$prev" ]; then
    for dir in "${XDG_DATA_HOME:-$HOME/.local/share}" $(printf '%s' "${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" | tr ':' ' '); do
        if [ -f "$dir/applications/$prev" ]; then exec gio launch "$dir/applications/$prev" "$f"; fi
    done
fi
exec wine start /unix "$f"
)SH";
}

std::string DesktopFile() {
    std::string mimes;
    for (const char* m : kMimes) mimes += std::string(m) + ";";
    return "[Desktop Entry]\n"
           "Type=Application\n"
           "Name=Windows program (mctde)\n"
           "Comment=Opens Dark Souls through the mctde launcher; other .exe files open in Wine\n"
           "Exec=\"" + ScriptPath() + "\" %f\n"
           "MimeType=" + mimes + "\n"
           "NoDisplay=true\n"
           "Terminal=false\n";
}

void RefreshDesktopCaches() {
    Run({"update-desktop-database", DataHome() + "/applications"});
    for (const char* k : {"kbuildsycoca6", "kbuildsycoca5"})
        if (Run({k}) >= 0) break;
}

// Drop "mime=mctde-open-exe.desktop" (and our id from association lists) from mimeapps.list.
void ScrubMimeapps() {
    std::string path = ConfigHome() + "/mimeapps.list";
    std::string text = ReadFile(path);
    if (text.empty()) return;
    std::stringstream in(text), out;
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq != std::string::npos && line.find(kDesktopId) != std::string::npos) {
            std::string key = line.substr(0, eq), rest = line.substr(eq + 1), kept;
            std::stringstream ids(rest); std::string id;
            while (std::getline(ids, id, ';')) if (!id.empty() && id != kDesktopId) kept += id + ";";
            if (kept.empty()) continue;           // the line only named us
            line = key + "=" + kept;
        }
        out << line << "\n";
    }
    WriteFile(path, out.str());
}

} // namespace

bool Installed() {
    struct stat st;
    return stat(ScriptPath().c_str(), &st) == 0 && QueryDefault(kMimes[0]) == kDesktopId;
}

bool Install(std::string& err) {
    if (Home().empty()) { err = "HOME isn't set"; return false; }
    MkDirs(StateDir());
    MkDirs(DataHome() + "/applications");
    // Remember what opened .exe files before us, once (a re-install must not record ourselves).
    struct stat st;
    if (stat(PrevPath().c_str(), &st) != 0) {
        std::string prev;
        for (const char* m : kMimes) {
            std::string d = QueryDefault(m);
            if (d == kDesktopId) d.clear();
            prev += std::string(m) + "=" + d + "\n";
        }
        if (!WriteFile(PrevPath(), prev)) { err = "can't write " + PrevPath(); return false; }
    }
    if (!WriteFile(ScriptPath(), Script(), 0755)) { err = "can't write " + ScriptPath(); return false; }
    if (!WriteFile(DesktopPath(), DesktopFile())) { err = "can't write " + DesktopPath(); return false; }
    std::vector<std::string> args = {"xdg-mime", "default", kDesktopId};
    for (const char* m : kMimes) args.push_back(m);
    if (Run(args) != 0) { err = "xdg-mime couldn't set the .exe handler (is xdg-utils installed?)"; return false; }
    RefreshDesktopCaches();
    return true;
}

bool Uninstall(std::string& err) {
    std::string prev = ReadFile(PrevPath());
    ScrubMimeapps();
    std::stringstream ps(prev);
    std::string line;
    while (std::getline(ps, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos || eq + 1 >= line.size()) continue;
        Run({"xdg-mime", "default", line.substr(eq + 1), line.substr(0, eq)});
    }
    unlink(DesktopPath().c_str());
    unlink(ScriptPath().c_str());
    unlink(PrevPath().c_str());
    RefreshDesktopCaches();
    if (QueryDefault(kMimes[0]) == kDesktopId) { err = "the .exe handler is still set in mimeapps.list"; return false; }
    return true;
}

} // namespace opener
