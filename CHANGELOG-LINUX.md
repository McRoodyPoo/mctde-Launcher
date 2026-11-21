# mctde-Launcher for Linux Changelog

## v0.7.1

- Settings → Vulkan no longer comes up blank: the greyed toggle and dxvk.conf rows now show.

## v0.7.0

- New Settings tab "Vulkan" with dxvk.conf tuning: present interval, max frame latency, HUD.
- The Enable Vulkan toggle is greyed out: Proton already renders through its own DXVK.
- LB / RB cycle through all three Settings tabs.

## v0.6.1: RightLook

- The launcher keeps your desktop's theme and fonts when it's started by the installer, a Steam shortcut or the game. Before, it inherited those programs' bundled libraries, couldn't load KDE's Plasma theme and came up in a plain style with serif fonts.

## v0.6.0: OneRelease

- Steam now shows you playing Spacewar, not Dark Souls: Prepare to Die Edition, both in game and while DSCM is open. The game also joins the same Spacewar (480) matchmaking pool as Windows players; before, Linux launches quietly ran as PTDE's own appid.
- The launcher disappears the moment you press PLAY and quits once the game is up. If the game fails to start, it comes back with the error.
- Bakes the game's own LB / RB / LT / RT button labels for mctde-Link 0.4.9's controller-setup hint.
- The Linux launcher now shares its version and releases with the Windows launcher: each release carries a Windows zip and a Linux zip, and Auto-Update picks the one for your OS. It reads the version from mctde-Link's `latest.txt` instead of the separate manifest on the `linux` branch, and checks the zip against the SHA-256 published with it.

- DSCM now opens behind the launcher instead of on top of it, like on Windows (KDE Plasma). A short-lived KWin script hands focus back to the launcher when DSCM's window appears; after a few seconds it lets go, so clicking DSCM works as normal.
- The Changelog's Launcher tab now shows this Linux changelog instead of the Windows launcher's.
- Update checks read the release manifest from a URL that GitHub's cache doesn't hold onto as long, so new releases show up within minutes.

## v0.2.1: Controller support

- Any connected controller drives the launcher, the same way as on Windows: D-pad or left stick to move, A to press, toggle or pick from a dropdown, B to back out or close Settings, Start for PLAY (Save in Settings), LB / RB to switch Settings tabs.
- Left / Right nudge sliders in Settings, and the page scrolls to follow the focused control.
- Any button during the Auto-Play countdown cancels it.
- The focused control shows the confirm button's icon: Xbox, PlayStation or Switch, picked under Settings > mctde > Controller button icons.
- Every connected pad works, including ones plugged in while the launcher is open. Needs SDL2; without it the launcher works as before, minus the controller.

## v0.2.0: Settings, new look, Auto-Play

- Settings window: the same two-tab window as Windows. The DSFix tab edits dsfix.ini (Advanced Options adds AA, HUD Mod, backups and DLL chaining); the mctde tab edits mctde-link.ini (overlay corner, font, padding and HP display with a live preview, then the rest of mctde-Link's settings). Save writes both files; Cancel changes nothing.
- New look matching the current Windows launcher: slide-switch toggles, rounded buttons, a gold PLAY button and the refreshed light theme.
- Auto-Play: counts down from 5 when the launcher opens, then starts the game. Click STOP, press any key or click anywhere to cancel. Off by default.
- Double-clicking DARKSOULS.exe opens the launcher, like on Windows. A double-clicked .exe runs in plain Wine, where the game can't reach Steam, so the launcher sets up a per-user .exe opener; every other .exe still opens in Wine. Follows "Require launcher to start the game"; undo with `mctde-launcher integrate off`.
- The Settings window stays usable when the desktop won't let it grow: Save / Cancel stay visible and the page scrolls.
- CLI: `set autoplay on|off` and `integrate on|off`.

## v0.1.0: First preview

- Native Linux launcher in the PTDE DATA folder: PhantomUnleashed, Eloise's PTDE Practice Tool, DSFix, Auto-Launch DSCM, Dark Mode, Changelog, PLAY / Exit, writing the same settings as the Windows launcher.
- PLAY runs the game through Proton in PTDE's own Steam prefix (your saves), with the Proton build and Steam Linux Runtime Steam picked for PTDE, starting Steam first if needed.
- DSCM runs in the same prefix, auto-started when enabled.
- Auto-Update for mctde-Link and the Linux launcher itself (checksum-verified), from the `linux` branch's own manifest so Windows updates are never affected.
- `mctde-launcher-cli`: the same commands without Qt (`status`, `play`, `dscm`, `set`, `caches`, `update`).
