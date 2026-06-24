# mctde-Launcher Changelog

## v0.2.0 — FengShui

- Dark Mode toggle with a fully themed light/dark UI.
- Redesigned header: live title text with a large themed Artorias filling the dead space.
- Artorias and title invert to match the theme (dark in light mode, light in dark mode).
- Auto-update for both the launcher and mctde-Link, with a version display above Play.
- Existing dsfix.ini settings are preserved — only options you actually change get written.
- Changelog viewer now fetches the latest notes live from GitHub.
- Now licensed under GPL-3.0.

## v0.1.0

First release of the standalone launcher.

- Native Win32 launcher (no .NET runtime) that runs under Proton/Wine in the same prefix
  as the game.
- PLAY launches DARKSOULS.exe; Exit closes the launcher.
- Increased Phantom Limit toggle (writes [PhantomUnleashed] Mode=On/Off to mctde-link.ini) —
  the way to opt in on Linux/Proton, where the in-game prompt is unavailable.
- Eloise's PTDE Practice Tool toggle; links to the latest release when not installed.
- DSFix toggle (greyed out when DSFix isn't present) plus a Configure panel that
  round-trips dsfix.ini:
  - Video: internal/output resolution, AA + AA Quality, SSAO, Texture Filtering,
    Borderless Fullscreen, HUD Mod.
  - Cursor: capture/disable cursor.
  - Other: skip intro, logging.
  - Depth of Field and Framerate, with a simple basic mode (Original DoF / FPS Stabilizer)
    and a detailed Advanced Options mode.
  - Advanced Options also exposes Save Game Backups, a "not ready to use" section, and
    Dinput DLL chaining (a cascading list that detects other DLLs in the DATA folder).
- Custom mask app icon and Dark Souls banner art.
- Changelog viewer for both the launcher and mctde-Link.
