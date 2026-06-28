# mctde-Launcher Changelog

## v0.5.0: MissionControl

- New "mctde Settings" button and dialog that round-trips mctde-link.ini, with curated options up front and the rest behind Advanced Options.
- Live overlay preview in mctde Settings that mirrors corner, font, size, padding, and the show/hide toggles as you change them.
- Owner-drawn font picker that renders each installed font in its own typeface, plus sliders for HP size and overlay padding.
- Hide Soul Counter toggle that hides the entire bottom-right soul display: box, icon, number, and the "+N" popup.
- "Revert to Defaults" button in the DSFix config that fills every field with the recommended defaults (nothing saved until you click Save).
- DSFix Log Level is now a full 0-11 dropdown instead of a single on/off checkbox.
- FPS Stabilizer (basic) and Unlock FPS (advanced) now stay in lockstep so neither silently overrides the other.
- HUD Mod moved into Advanced Options, since its full-HUD recomposite is a niche, glitch-prone tweak.
- Repositioned the banner art in both themes: the title and Artorias shrink and anchor to the top-right, opening the lower-left for the controls.
- Force steam_appid.txt to 480 (Spacewar) before launch so every install shares the same mctde matchmaking pool.
- PhantomUnleashed now also writes VerifyOnly=0 so the phantom-cap patch actually applies instead of only logging.
- Regenerate a complete, fully documented mctde-link.ini when it's missing, instead of a bare-bones file.

## v0.4.1: AliasArmistice

- DSFix antialiasing is now forced off before every launch. SMAA (and SSAO) collide with the in-frame overlay and smear the 3D world with red/yellow corruption that only a resolution change clears; keeping AA off sidesteps the collision entirely.
- Moved the AA type and quality controls out of the basic DSFix config into Advanced Options. A deliberate AA choice made there is remembered ([Launcher] AllowAA) and respected (the launch-time force-off then leaves it alone) so you can opt back in at your own risk.

## v0.4.0: SplitBanner

- New full-window banner artwork for both themes, replacing the previous gray-art banner: the "Dark Souls: Prepare To Die Edition" logo and Artorias figure over a black/cream split (mirrored between light and dark), with an "mctde" subtitle, zoomed in and anchored to the top-right.
- Aligned the left-column checkboxes (Phantom, Practice, DSFix, Auto-Launch DSCM) with the Dark Mode checkbox.
- Fixed the Auto-Update label disappearing where it overlaps the banner's inverse-shaded half. Its text now contrasts with that half in each theme.
- Refreshed the bundled mctde-Link changelog so the offline Changelog view includes Link 0.4.3 (Reversal), 0.4.4 (Homecoming), and 0.4.5 (Clean Exit).

## v0.3.2

- Detect DSCM.exe sitting in the DATA folder beside the launcher, so the Auto-Launch DSCM row lights up even when DSCM isn't already running and no path has been remembered yet. (DSCM is still launched in place and never copied/moved.)

## v0.3.1: Auto-DSCM

- Auto-Launch DSCM checkbox: starts the Dark Souls Connectivity Mod (DSCM.exe) when the launcher opens, or the moment you tick the box.
- DSCM opens behind the launcher window without stealing focus, and is skipped if it's already running.
- Runs DSCM from its own folder so its in-place self-updater keeps working (never copies a stale build into DATA).
- Finds DSCM's path from a running instance and remembers it; the row stays greyed "Searching for DSCM..." until found.

## v0.2.0: FengShui

- Dark Mode toggle with a fully themed light/dark UI.
- Redesigned header: live title text with a large themed Artorias filling the dead space.
- Artorias and title invert to match the theme (dark in light mode, light in dark mode).
- Auto-update for both the launcher and mctde-Link, with a version display above Play.
- Existing dsfix.ini settings are preserved: only options you actually change get written.
- Changelog viewer now fetches the latest notes live from GitHub.
- Now licensed under GPL-3.0.

## v0.1.0

First release of the standalone launcher.

- Native Win32 launcher (no .NET runtime) that runs under Proton/Wine in the same prefix
  as the game.
- PLAY launches DARKSOULS.exe; Exit closes the launcher.
- Increased Phantom Limit toggle (writes [PhantomUnleashed] Mode=On/Off to mctde-link.ini):
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
