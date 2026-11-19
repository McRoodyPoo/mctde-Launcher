// linux/exe_opener.h -- make double-clicking DARKSOULS.exe open the mctde launcher (Linux).
//
// On Windows, mctde-Link's launcher guard catches a direct DARKSOULS.exe start inside the game and
// reopens mctde_launcher.exe. That can't work on Linux: a double-clicked .exe runs under plain Wine,
// where the game can't reach Steam and quits before mctde-Link's guard ever runs. So the Linux
// launcher does it one step earlier, in the desktop's file association: a tiny user-level .exe
// opener that sends DARKSOULS.exe / DATA.exe to the mctde-launcher sitting beside it (when
// [Launcher] RequireLauncher isn't 0) and hands every other .exe to whatever opened it before
// (normally Wine). Everything lives under ~/.local/share and ~/.config/mimeapps.list and is
// removed again by Uninstall().
#pragma once

#include <string>

namespace opener {

// Our opener is the default handler for Windows executables.
bool Installed();
// Register / unregister it for the current user. Uninstall restores the previous handlers.
bool Install(std::string& err);
bool Uninstall(std::string& err);

} // namespace opener
