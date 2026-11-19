// linux/kwin_behind.h -- open DSCM behind the launcher on KDE Plasma (Wayland or X11).
//
// The Windows launcher starts DSCM with SW_SHOWNOACTIVATE and sinks its window below its own. On
// Linux neither works: Wine under KWin activates the new window anyway, the launcher (a Wayland
// client) can't restack DSCM's XWayland window, and KWin's focus-stealing prevention refuses to let
// the launcher take focus back without fresh user input. A KWin script can, though: for a short
// while it watches for DSCM's window and, when it appears (or grabs focus within a few seconds of
// appearing), activates the launcher again, which also raises it above DSCM. The script is loaded
// over D-Bus just before DSCM starts and unloaded again afterwards. Other desktops: no-op.
#pragma once

namespace kwin {

// Blocking (run it on a worker thread): loads the script, keeps it for `seconds`, unloads it.
// Returns false without doing anything when not on KDE or when no D-Bus tool is available.
bool KeepDscmBehindLauncher(int seconds);

} // namespace kwin
