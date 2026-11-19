**Linux-only preview.** Windows players: nothing here for you, keep using the regular release.

### New in 0.2.1: controller support
Any connected controller can drive the launcher, the same way as on Windows:
- **D-pad / left stick** moves between controls (Left/Right also nudge sliders in Settings), with
  keyboard-style repeat when held.
- **A** presses or toggles, opens a dropdown and picks from it.
- **B** backs out of a dropdown or closes Settings and dialogs.
- **Start** presses PLAY (Save in Settings). **LB / RB** switch Settings tabs.
- Any button during the Auto-Play countdown cancels it.
- The focused control shows the confirm button's icon. Pick Xbox, PlayStation or Switch icons in
  Settings > mctde > Controller button icons.

Every connected pad works, including ones plugged in while the launcher is open. Needs SDL2
(installed with Steam on most distros); without it the launcher works as before, minus the
controller.

### From 0.2.0
Settings window (DSFix + mctde tabs), the new look, Auto-Play, and double-clicking DARKSOULS.exe
opening the launcher. See the 0.2.0 release for details.

### Not yet
Steam overlay in-game.

### Requirements
Steam + Proton, PTDE run through Proton at least once, Qt 6 for the window, glibc 2.34+, curl,
xdg-utils (for the `.exe` opener), SDL2 (for controllers).

See `README-LINUX.md` in the archive for install steps and troubleshooting.
