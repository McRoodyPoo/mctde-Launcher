# mctde Launcher for Linux (preview)

A native Linux version of the mctde launcher, for Dark Souls: Prepare to Die Edition with mctde.
It's an early preview: the main window works the same as on Windows, and a few extras aren't
ported yet (see Known limitations).

## What you need

- **Steam** and **Proton**. The launcher uses the Proton version Steam has set for PTDE (Proton
  Experimental by default).
- **PTDE launched through Steam/Proton at least once.** That's what creates the Proton prefix your
  saves live in. The launcher uses that same prefix.
- **Qt 6** for the window. It's preinstalled on KDE desktops and the Steam Deck's desktop mode.
  Elsewhere, install it from your package manager (`qt6-base`, `libqt6widgets6` or similar). If you
  can't, use `mctde-launcher-cli` instead (see below).
- A 2022-or-newer distro (glibc 2.34+): Ubuntu 22.04+, Debian 12+, Fedora 35+, Arch, SteamOS 3.

## Install

1. Copy `mctde-launcher` and `mctde-launcher-cli` into your PTDE `DATA` folder, next to
   `DARKSOULS.exe` (the same folder the Windows `mctde_launcher.exe` goes in).
2. Make sure they're executable: `chmod +x mctde-launcher mctde-launcher-cli`.
3. Open `mctde-launcher` (double-click it; your file manager may ask whether to run it).

## Playing

Set your options and press **PLAY**. The launcher:

1. saves your settings (the same `mctde-link.ini` keys and DSFix/Practice Tool toggles as Windows),
2. starts Steam if it isn't running,
3. runs `DARKSOULS.exe` through Proton in PTDE's prefix, then closes itself once the game is up.

If **Auto-Launch DSCM** is on, DSCM starts in the same prefix when the launcher opens, and PLAY
waits for it.

Why not just Wine? The game has to reach the Steam client, and on Linux only Proton bridges Steam
into the game's prefix. Under plain Wine the game quits immediately. `steam_appid.txt` (480) still
keeps you on the shared mctde matchmaking pool.

## Settings

**Settings** opens the same two-tab window as on Windows. The **DSFix** tab edits `dsfix.ini`
(resolution, SSAO, filtering, cursor, DoF, framerate; antialiasing, HUD Mod, backups and DLL
chaining under Advanced Options). The **mctde** tab edits `mctde-link.ini` (overlay corner, font,
padding, HP display with a live preview, then the rest of mctde-Link's settings). Save writes both
files; Cancel changes nothing. Only the DSFix keys you actually change are rewritten.

## Controller

Any connected controller can drive the launcher, the same way as on Windows. The D-pad or left stick
moves between controls; **A** presses or toggles, opens a dropdown and picks from it; **B** backs
out of a dropdown or closes Settings; **Start** presses PLAY (Save in Settings); **LB / RB** switch
Settings tabs. Any button during the Auto-Play countdown cancels it. The focused control shows the
confirm button's icon; pick Xbox, PlayStation or Switch icons under Settings > mctde > Controller
button icons. Needs SDL2 (installed with Steam on most distros); without it the launcher works as
before, just without the controller.

## Double-clicking DARKSOULS.exe

On Windows, starting `DARKSOULS.exe` directly reopens the launcher instead. On Linux a double-clicked
`.exe` runs in plain Wine, where the game can't reach Steam and closes straight away, so the launcher
sets this up in your desktop's file associations instead. While **Require launcher to start the
game** is on (Settings > mctde, the default), double-clicking `DARKSOULS.exe` in a folder that has
`mctde-launcher` opens the launcher. Every other `.exe` still opens in Wine as before. It's
per-user and reversible: `./mctde-launcher integrate off` (or untick that setting) puts the old
handler back.

## Auto-Play

With **Auto-Play** on, the launcher counts down from 5 when it opens and then starts the game by
itself. The PLAY button reads **STOP (n)** while it counts; clicking it, or pressing any key or
clicking anywhere else, stops the countdown so you can change settings first. It doesn't start the
countdown if the game is already running. Off by default (`[Launcher] AutoPlay` in `mctde-link.ini`).

## Updates

With **Auto-Update** ticked (the default), the launcher checks for updates each time it opens:

- **mctde-Link** (`d3d9.dll`) is updated from mctde-Link's latest release, the same as on Windows.
  A missing `d3d9.dll` is always re-downloaded.
- **The Linux launcher** updates itself from the Linux release listed on the `linux` branch, checks
  the download's SHA-256, swaps in the new version and reopens.

You can also run `./mctde-launcher update` (or `update --check` to only look). Downloads use your
system's `curl`. Unpacking mctde-Link uses `unzip`, `bsdtar` or `python3`, whichever you have
(SteamOS has them).

## Command line

`mctde-launcher <command>` (or `mctde-launcher-cli <command>`, which doesn't need Qt):

```
status                  your settings, plus the Steam/Proton setup PLAY will use
play [--wait]           start the game (and DSCM first, if Auto-Launch DSCM is on)
dscm                    start DSCM
set <option> on|off     phantom, dsfix, practice, autoupdate, autoplay, dscm, dark
caches                  bake the overlay font/icon/gauge caches
update [--check]        update the launcher and mctde-Link now (--check: only report)
```

Add `--dir <DATA folder>` before the command to point at a different install.

## If something goes wrong

- **"Dark Souls closed right after starting"**: Proton's output is in
  `mctde_launcher_proton.log` in the DATA folder. If DSFix is on, also check `DSfix.log`. For
  mctde-Link's own log, set `EnableLogging=1` under `[Settings]` in `mctde-link.ini`.
- **Not sure which Proton/prefix it's using**: run `./mctde-launcher status`.
- DSCM's output goes to `mctde_launcher_dscm.log`.

## Known limitations (preview)

- **Settings** opens `dsfix.ini` / `mctde-link.ini` in your text editor. The full Settings window
  (DSFix and mctde tabs, overlay preview) isn't ported yet.
- **No Steam overlay** in-game when started from this launcher (the game still connects to Steam).
- Pressing **Play in Steam** still goes through the Windows launcher if `mctde_launcher.exe` is in
  the folder (it works under Proton). Use the Linux launcher for the native experience.
- The Changelog button shows the Windows launcher's changelog and mctde-Link's.
