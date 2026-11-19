# mctde-Launcher

A small native Win32 launcher for **mctde**, a mod for Dark Souls: Prepare to Die Edition.
Built dependency-free (static `/MT`, no .NET) so it runs cleanly under Proton/Wine in the same
prefix as the game.

It sits in the game's `DATA` folder beside `DARKSOULS.exe` and provides:

- **Increased Phantom Limit**: writes `[PhantomUnleashed] Mode=On/Off` to `mctde-link.ini`
  (the only way to opt in under Proton, where the in-game prompt is suppressed).
- **Eloise's PTDE Practice Tool**: toggles the chainloaded `dinput8.dll` in
  `mctde-Link_Chainload`; links to the latest release when not installed.
- **DSFix**: toggles DSFix (`dinput8.dll` ↔ `dinput8.dll.off`) and opens a settings panel
  that round-trips `dsfix.ini` (with an Advanced Options mode).
- **Auto-Launch DSCM**: starts the Dark Souls Connectivity Mod (`DSCM.exe`) behind the
  launcher when it opens (or when you tick the box), skipping it if it's already running. DSCM
  is run in place from its own folder so its self-updater keeps working; the launcher learns
  its path from a running instance, so the row stays greyed *Searching for DSCM…* until found.
- **PLAY / Exit**: launches `DARKSOULS.exe` in the same directory.

## Linux (native launcher, in progress)

`mctde-launcher` is a native Linux build that sits in the same `DATA` folder. PLAY runs
`DARKSOULS.exe` through Proton in PTDE's own Steam prefix (where your saves live), using the Proton
build Steam has selected for PTDE and the Steam Linux Runtime it needs, and starts Steam first if
it isn't running. Plain Wine can't work: the game's `steam_api.dll` has to reach the Steam client,
and only Proton bridges it into the prefix. `steam_appid.txt` (480) is still what keeps the game
on the shared mctde pool.

Opening `mctde-launcher` shows the launcher window (Qt 6), laid out like the Windows one. Given a
command it acts as a CLI; `mctde-launcher-cli` is the same CLI without the Qt dependency:

```
mctde-launcher status                 # settings + the Steam/Proton setup PLAY will use
mctde-launcher play [--wait]          # also starts DSCM first when Auto-Launch DSCM is on
mctde-launcher dscm
mctde-launcher set phantom|dsfix|practice|autoupdate|autoplay|dscm|dark on|off
mctde-launcher caches                 # bake the overlay font/icon/gauge caches
mctde-launcher update [--check]       # update the launcher and mctde-Link now
mctde-launcher integrate on|off       # double-clicking DARKSOULS.exe opens the launcher
```

Controllers drive the window like on Windows (`linux/gamepad.cpp`, SDL2 loaded at runtime, so
there's no build or runtime dependency on it). The window's **Settings** button opens the same DSFix / mctde tabbed Settings window as Windows
(`linux/settings.cpp`). While `[Launcher] RequireLauncher` is on, the window also registers a
per-user `.exe` opener (`linux/exe_opener.cpp`) so a double-clicked `DARKSOULS.exe` opens the launcher:
the Linux stand-in for mctde-Link's launcher guard, which can't fire under plain Wine because the game
quits on `steam_api` before it reaches Direct3D.

**Linux releases / updates.** The Linux launcher has its own version (`linux/version.h`) and update
manifest: `latest.txt` on the `linux` branch, which names an exact release asset and its SHA-256
(never `releases/latest`), so Linux releases can't change what the Windows launcher updates to.
`tools/make_linux_release.sh <version>` builds, packages and writes that `latest.txt`. Publish the
GitHub pre-release tagged `linux-v<version>` first, then push `latest.txt` to the `linux` branch.

Build with `./build_linux.sh` (C++17 compiler; the window also needs Qt 6 base dev files /
`qmake6`). Output: `bin/mctde-launcher` and `bin/mctde-launcher-cli`. `core/` holds the install
logic ported from the Windows launcher, so both behave the same on the same folder. Player-facing
docs for the preview are in `dist/README-LINUX.md`.

## Build

```
build_launcher.bat
```

Requires MSVC (32-bit, matching the game). Edit the `VCVARS` path in the script for your VS
install. Output: `bin\mctde_launcher.exe`.

## Icon

`make_icon.ps1 -Png <art.png>` regenerates `mctde.ico` (multi-resolution) from a square PNG.
`build_launcher.bat` embeds it via `mctde_launcher.rc`.

## Deploy

Copy `bin\mctde_launcher.exe` (and `dsfix.ini` etc.) into the PTDE `DATA` folder, then point
your Steam shortcut / launch target at it.
