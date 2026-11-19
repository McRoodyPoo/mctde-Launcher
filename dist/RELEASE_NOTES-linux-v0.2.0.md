**Linux-only preview.** Windows players: nothing here for you, keep using the regular release.

A native Linux mctde launcher. Drop `mctde-launcher` into your PTDE `DATA` folder next to
`DARKSOULS.exe` and open it. PLAY runs the game through Proton in PTDE's own Steam prefix (your
existing saves), and starts Steam for you if needed.

### New in 0.2.0
- **Settings window**: the same two-tab window as Windows. The DSFix tab edits `dsfix.ini`
  (Advanced Options adds AA, HUD Mod, backups and DLL chaining). The mctde tab edits
  `mctde-link.ini` (overlay corner, font, padding and HP display with a live preview, then the rest
  of mctde-Link's settings). Save writes both files; Cancel changes nothing.
- **New look**, matching the current Windows launcher: slide-switch toggles, rounded buttons, a gold
  PLAY button, and the refreshed light theme.
- **Auto-Play**: counts down from 5 when the launcher opens and then starts the game. Click STOP,
  press any key or click anywhere to cancel. Off by default.
- **Double-clicking DARKSOULS.exe opens the launcher**, like on Windows. On Linux a double-clicked
  `.exe` runs in plain Wine, where the game can't reach Steam and closes straight away, so the
  launcher sets up a per-user `.exe` opener: `DARKSOULS.exe` next to `mctde-launcher` opens the
  launcher, and every other `.exe` still opens in Wine. It follows **Require launcher to start the
  game** (Settings > mctde) and can be undone with `./mctde-launcher integrate off`.
- CLI: `set autoplay on|off` and `integrate on|off`.

### Also included
- PhantomUnleashed, Eloise's PTDE Practice Tool, DSFix, Auto-Launch DSCM, Dark Mode, Changelog,
  PLAY / Exit, writing the same settings as the Windows launcher.
- Auto-Update for mctde-Link and the Linux launcher itself (checksum-verified). Linux releases are
  listed on the `linux` branch's own `latest.txt`, so they never affect Windows updates.
- `mctde-launcher-cli`: command line only, no Qt needed.

### Not yet
Controller navigation in the launcher, Steam overlay in-game.

### Requirements
Steam + Proton, PTDE run through Proton at least once, Qt 6 for the window, glibc 2.34+, curl,
xdg-utils (for the `.exe` opener).

See `README-LINUX.md` in the archive for install steps and troubleshooting.
