// linux/cli.cpp -- the launcher's command-line interface (see cli.h).
//
// PLAY runs the game through Proton in PTDE's own prefix with Steam running -- see linux/steam.h
// for why plain Wine can't work.
#include "cli.h"
#include "exe_opener.h"
#include "../core/install.h"
#include "launch.h"
#include "steam.h"
#include "update.h"
#include "version.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <unistd.h>

std::string ExeDir() {
    char buf[PATH_MAX] = {0};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    std::string p(buf, (size_t)n);
    return p.substr(0, p.find_last_of('/'));
}

static void Usage() {
    printf(
        "mctde-launcher %s (Linux preview) -- native Linux launcher for mctde (Dark Souls: PTDE)\n"
        "\n"
        "usage: mctde-launcher [--dir <DATA folder>] <command>     (no command: open the launcher window)\n"
        "\n"
        "  play [--wait]          run the game through Proton (starts Steam if needed; also starts\n"
        "                         DSCM first when Auto-Launch DSCM is on). --wait blocks until it exits\n"
        "  dscm                   start DSCM in the game's prefix\n"
        "  status                 show the install's settings and the Steam/Proton setup PLAY will use\n"
        "  set <option> on|off    phantom, dsfix, practice, autoupdate, autoplay, dscm, dark\n"
        "  caches                 bake the overlay font/icon/gauge caches now\n"
        "  update [--check]       update this launcher and mctde-Link now (--check: only report)\n"
        "  integrate on|off       make double-clicking DARKSOULS.exe open this launcher (on by\n"
        "                         default while [Launcher] RequireLauncher=1); no value: status\n"
        "\n"
        "--dir defaults to the folder this program is in.\n",
        MCTDE_LINUX_VERSION);
}

static const char* OnOff(bool b) { return b ? "on" : "off"; }

static int CmdStatus(const mctde::Install& inst) {
    printf("Launcher:  %s (Linux preview)\n", MCTDE_LINUX_VERSION);
    printf("Install:   %s\n", inst.Dir().c_str());
    printf("  Increased Phantom Limit  %s\n", OnOff(inst.PhantomEnabled()));
    printf("  DSFix                    %s\n", inst.DsfixInstalled() ? OnOff(inst.DsfixEnabled()) : "not installed");
    printf("  Practice Tool            %s\n", inst.PracticeInstalled() ? OnOff(inst.PracticeEnabled()) : "not installed");
    printf("  Auto-Update              %s\n", OnOff(inst.AutoUpdate()));
    printf("  Auto-Play                %s\n", OnOff(inst.AutoPlay()));
    printf("  Auto-Launch DSCM         %s\n", OnOff(inst.AutoLaunchDscm()));
    printf("  Dark mode                %s\n", OnOff(inst.DarkMode()));

    steam::Env env; std::string err;
    bool ok = steam::Discover(env, err);
    int pid = 0;
    printf("\nSteam:     %s\n", env.root.empty() ? "(not found)" : env.root.c_str());
    printf("  client running           %s\n", steam::ClientRunning(&pid) ? ("yes, pid " + std::to_string(pid)).c_str() : "no (PLAY will start it)");
    printf("  PTDE prefix              %s\n", env.compatData.c_str());
    printf("  Proton                   %s%s\n", env.protonDir.c_str(),
           env.protonName.empty() ? "" : (" (Steam setting: " + env.protonName + ")").c_str());
    printf("  runtime                  %s\n", env.runtimeEntry.empty() ? "(none required)" : env.runtimeEntry.c_str());
    if (ok) {
        std::string dscm = launch::DscmLaunchPath(env, inst);
        printf("  DSCM                     %s\n", dscm.empty() ? "(not found yet -- start it once, or put DSCM.exe here)" : dscm.c_str());
    }
    for (const std::string& n : env.notes) printf("  note: %s\n", n.c_str());
    if (!ok) { printf("\nPLAY can't run yet: %s\n", err.c_str()); return 1; }
    return 0;
}

static int CmdSet(const mctde::Install& inst, const char* opt, const char* val) {
    bool on;
    if (!strcasecmp(val, "on") || !strcmp(val, "1")) on = true;
    else if (!strcasecmp(val, "off") || !strcmp(val, "0")) on = false;
    else { fprintf(stderr, "value must be on or off\n"); return 2; }

    if (!strcasecmp(opt, "phantom")) inst.SetPhantom(on);
    else if (!strcasecmp(opt, "dsfix")) {
        if (!inst.DsfixInstalled()) { fprintf(stderr, "DSFix isn't installed in this folder.\n"); return 1; }
        inst.ApplyDsfix(on);
    } else if (!strcasecmp(opt, "practice")) {
        if (!inst.PracticeInstalled()) {
            fprintf(stderr, "The Practice Tool isn't installed. Get it from %s\n", mctde::PRACTICE_RELEASE_URL);
            return 1;
        }
        inst.ApplyPractice(on);
    } else if (!strcasecmp(opt, "autoupdate")) inst.SetAutoUpdate(on);
    else if (!strcasecmp(opt, "autoplay")) inst.SetAutoPlay(on);
    else if (!strcasecmp(opt, "dscm")) inst.SetAutoLaunchDscm(on);
    else if (!strcasecmp(opt, "dark")) inst.SetDarkMode(on);
    else { fprintf(stderr, "unknown option '%s'\n", opt); return 2; }
    printf("%s %s\n", opt, OnOff(on));
    return 0;
}

static bool NeedEnv(steam::Env& env) {
    std::string err;
    if (steam::Discover(env, err)) return true;
    fprintf(stderr, "mctde-launcher: %s\n", err.c_str());
    return false;
}

static int CmdDscm(const mctde::Install& inst) {
    steam::Env env;
    if (!NeedEnv(env)) return 1;
    std::string path = launch::DscmLaunchPath(env, inst);
    if (path.empty()) { fprintf(stderr, "DSCM's location isn't known yet. Start DSCM once, or put DSCM.exe in this folder.\n"); return 1; }
    launch::Result r = launch::LaunchDscm(env, inst, path);
    if (!r.started) { fprintf(stderr, "mctde-launcher: %s\n", r.error.c_str()); return 1; }
    printf("DSCM started: %s\n", path.c_str());
    return 0;
}

static int CmdPlay(const mctde::Install& inst, bool wait) {
    steam::Env env;
    if (!NeedEnv(env)) return 1;
    if (inst.AutoLaunchDscm()) {
        std::string path = launch::DscmLaunchPath(env, inst);
        if (!path.empty()) {
            launch::Result d = launch::LaunchDscm(env, inst, path);
            if (!d.started) fprintf(stderr, "mctde-launcher: DSCM: %s\n", d.error.c_str());
        }
    }
    printf("Starting Dark Souls through %s ...\n", env.protonDir.c_str());
    launch::Result r = launch::PlayGame(env, inst, 15);
    if (!r.started || (r.exitedEarly && r.exitCode != 0)) {
        fprintf(stderr, "mctde-launcher: %s\n", r.error.c_str());
        return 1;
    }
    if (!wait) { printf("Game started (pid %d). Proton log: %s\n", (int)r.pid, r.logPath.c_str()); return 0; }
    if (r.exitedEarly) return r.exitCode;
    int code = launch::WaitForExit(r.pid);
    printf("Game exited (code %d).\n", code);
    return code;
}

static int CmdUpdate(const mctde::Install& inst, bool checkOnly) {
    if (checkOnly) {
        update::Check c = update::Fetch(inst);
        printf("Launcher     installed %-10s latest %s%s\n", MCTDE_LINUX_VERSION,
               c.linuxOk ? (c.release.version.empty() ? "(none listed)" : c.release.version.c_str()) : "(couldn't check)",
               c.LauncherOutdated() ? "   <- update available" : "");
        printf("mctde-Link   installed %-10s latest %s%s\n", c.installedLink.empty() ? "(missing)" : c.installedLink.c_str(),
               c.linkOk ? c.latestLink.c_str() : "(couldn't check)", c.LinkOutdated() ? "   <- update available" : "");
        return 0;
    }
    update::Outcome o = update::Run(inst, ExeDir(), true);
    if (!o.check.linkOk && !o.check.linuxOk) { fprintf(stderr, "Couldn't reach GitHub to check for updates.\n"); return 1; }
    if (o.linkUpdated) printf("mctde-Link updated to %s\n", o.check.installedLink.c_str());
    if (o.launcherUpdated) printf("Launcher updated to %s -- run it again to use the new version.\n", o.check.release.version.c_str());
    if (!o.linkUpdated && !o.launcherUpdated && o.error.empty()) printf("Everything is up to date.\n");
    if (!o.error.empty()) { fprintf(stderr, "mctde-launcher: %s\n", o.error.c_str()); return 1; }
    return 0;
}

int RunCli(int argc, char** argv) {
    std::string dir = ExeDir();
    int i = 1;
    if (i + 1 < argc && !strcmp(argv[i], "--dir")) { dir = argv[i + 1]; i += 2; }
    if (i >= argc || !strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { Usage(); return i >= argc ? 2 : 0; }

    mctde::Install inst(dir);
    // Regenerate a full, documented mctde-link.ini if it's missing (or stale) BEFORE anything reads
    // or writes a key -- same as the Windows launcher.
    if (inst.EnsureIniSeeded())
        fprintf(stderr, "Note: mctde-link.ini was from an older release and has been reset to this "
                        "release's defaults. Re-apply any custom settings.\n");

    std::string cmd = argv[i];
    if (cmd == "status") return CmdStatus(inst);
    if (cmd == "play")   return CmdPlay(inst, i + 1 < argc && !strcmp(argv[i + 1], "--wait"));
    if (cmd == "dscm")   return CmdDscm(inst);
    if (cmd == "caches") { inst.GenerateOverlayCaches(); printf("Overlay caches up to date in %s\n", inst.ChainloadDir().c_str()); return 0; }
    if (cmd == "update") return CmdUpdate(inst, i + 1 < argc && !strcmp(argv[i + 1], "--check"));
    if (cmd == "integrate") {
        // Double-clicking DARKSOULS.exe opens the launcher (see linux/exe_opener.h).
        const char* v = i + 1 < argc ? argv[i + 1] : "status";
        std::string err;
        if (!strcasecmp(v, "status")) { printf(".exe opener %s\n", opener::Installed() ? "on" : "off"); return 0; }
        bool on = !strcasecmp(v, "on");
        if (!on && strcasecmp(v, "off")) { Usage(); return 2; }
        if (!(on ? opener::Install(err) : opener::Uninstall(err))) { fprintf(stderr, "mctde-launcher: %s\n", err.c_str()); return 1; }
        printf(".exe opener %s\n", on ? "on: double-clicking DARKSOULS.exe opens the launcher" : "off");
        return 0;
    }
    if (cmd == "set") {
        if (i + 2 >= argc) { Usage(); return 2; }
        return CmdSet(inst, argv[i + 1], argv[i + 2]);
    }
    Usage();
    return 2;
}
