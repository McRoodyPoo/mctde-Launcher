**Linux-only preview.** Windows players: nothing here for you, keep using the regular release.

A native Linux mctde launcher. Drop `mctde-launcher` into your PTDE `DATA` folder next to
`DARKSOULS.exe` and open it: same window, same toggles, and PLAY runs the game through Proton in
PTDE's own Steam prefix (your existing saves), with Steam started for you if needed.

### What's in it
- The main launcher window: PhantomUnleashed, Eloise's PTDE Practice Tool, DSFix, Auto-Launch DSCM,
  Dark Mode, Changelog, PLAY / Exit. It writes the same settings as the Windows launcher.
- PLAY: finds Steam, PTDE's prefix, the Proton build Steam picked for PTDE and the Steam Linux
  Runtime it needs, then launches the game. If the game dies on startup you get an error with
  where to look, instead of nothing happening.
- DSCM runs in the same prefix, auto-started when the launcher opens if you've enabled it.
- The overlay font/icon/gauge caches are baked the same way as on Windows (byte-identical output).
- Auto-Update: keeps mctde-Link and the Linux launcher itself up to date (the launcher update is
  checksum-verified). Linux releases are listed on the `linux` branch's own `latest.txt`, so they
  never affect Windows updates.
- `mctde-launcher-cli` is a command-line-only build with no Qt needed (`status`, `play`, `dscm`,
  `set`, `caches`, `update`).

### Not yet
Full Settings window (it opens the ini files for now), Steam overlay in-game.

### Requirements
Steam + Proton, PTDE run through Proton at least once, Qt 6 for the window, glibc 2.34+, curl.

See `README-LINUX.md` in the archive for install steps and troubleshooting.
