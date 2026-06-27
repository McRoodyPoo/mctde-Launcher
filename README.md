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
