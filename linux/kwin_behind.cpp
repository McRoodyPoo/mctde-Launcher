// linux/kwin_behind.cpp -- see kwin_behind.h.
#include "kwin_behind.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace kwin {
namespace {

const char* const kPlugin = "mctde-dscm-behind";

// Matches DSCM by its window title ("Wulf's Dark Souls Connectivity Mod"). Not by class: under
// Proton every window in the prefix -- DSCM and the game alike -- has the class steam_app_211420,
// and the script must never pull focus off the game after PLAY. The launcher is matched by its
// Wayland app id / X11 class ("mctde-launcher", from QApplication::setDesktopFileName). DSCM gets
// a few seconds after its window appears in which taking focus is undone; after that the
// player's clicks win.
const char* const kScript = R"JS(
const GRACE_MS = 4000;
let addedAt = 0;
function isDscm(w) {
    if (!w) return false;
    const t = (w.caption || "").toLowerCase(), c = (w.resourceClass || "").toLowerCase();
    return t.indexOf("connectivity mod") >= 0 || t.indexOf("dscm") >= 0 || c.indexOf("dscm") >= 0;
}
function launcher() {
    const ws = workspace.windowList();
    for (let i = 0; i < ws.length; ++i)
        if (ws[i].resourceClass === "mctde-launcher" || ws[i].desktopFileName === "mctde-launcher") return ws[i];
    return null;
}
function sink() { const l = launcher(); if (l) workspace.activeWindow = l; }
function check(w) { if (isDscm(w) && !addedAt) { addedAt = Date.now(); sink(); } }
workspace.windowAdded.connect(function (w) {
    check(w);
    w.captionChanged.connect(function () { check(w); });   // Wine may title the window after mapping it
});
workspace.windowActivated.connect(function (w) { if (isDscm(w) && addedAt && Date.now() - addedAt < GRACE_MS) sink(); });
check(workspace.activeWindow);
)JS";

bool OnKde() {
    const char* d = getenv("XDG_CURRENT_DESKTOP");
    return d && strstr(d, "KDE");
}

int Run(const std::vector<std::string>& args) {
    std::vector<char*> av;
    for (auto& a : args) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, av[0], &fa, nullptr, av.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return -1;
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

// org.kde.KWin /Scripting org.kde.kwin.Scripting.<method>(args...) with whichever tool exists.
int Call(const char* method, const std::vector<std::string>& args) {
    const std::string iface = std::string("org.kde.kwin.Scripting.") + method;
    for (const char* tool : { "qdbus6", "qdbus" }) {
        std::vector<std::string> a = { tool, "org.kde.KWin", "/Scripting", iface };
        a.insert(a.end(), args.begin(), args.end());
        int rc = Run(a);
        if (rc >= 0) return rc;   // the tool exists (rc reports the call)
    }
    std::vector<std::string> a = { "gdbus", "call", "--session", "--dest", "org.kde.KWin",
                                   "--object-path", "/Scripting", "--method", iface };
    a.insert(a.end(), args.begin(), args.end());
    return Run(a);
}

} // namespace

bool KeepDscmBehindLauncher(int seconds) {
    if (!OnKde()) return false;
    const char* rt = getenv("XDG_RUNTIME_DIR");
    std::string path = std::string(rt && *rt ? rt : "/tmp") + "/" + kPlugin + "-" + std::to_string(getpid()) + ".js";
    { std::ofstream f(path, std::ios::trunc); f << kScript; if (!f) return false; }
    Call("unloadScript", { kPlugin });                 // a leftover from a launcher that crashed
    if (Call("loadScript", { path, kPlugin }) != 0) { unlink(path.c_str()); return false; }
    Call("start", {});
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    Call("unloadScript", { kPlugin });
    unlink(path.c_str());
    return true;
}

} // namespace kwin
