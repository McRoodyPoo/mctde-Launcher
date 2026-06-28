// mctde_launcher.cpp
//
// Standalone launcher for the mctde-Link mod. Native Win32 (no .NET / no runtime) so it
// runs cleanly under Proton/Wine in the SAME prefix as the game -- it spawns DARKSOULS.exe
// directly. Point your Steam shortcut at this exe.
//
// It sits in the DATA folder next to d3d9.dll, dinput8.dll (DSFix), dsfix.ini, mctde-link.ini
// and DARKSOULS.exe, and exposes:
//   * "PhantomUnleashed (uncommon)"  -> writes [PhantomUnleashed] Mode=On/Off in mctde-link.ini.
//        On Proton the in-game Ask prompt is suppressed (PhantomUnleashed.cpp), so this is the
//        only way Linux players can opt in without hand-editing the ini.
//   * "DSFix" (+ Config)         -> enables/disables DSFix by renaming its wrapper
//        dinput8.dll <-> dinput8.dll.off, and opens a settings panel that round-trips dsfix.ini.
//   * PLAY / Exit.
//
// Build: launcher\build_launcher.bat  (32-bit, /MT, no external deps).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <commctrl.h>
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "comctl32.lib")

#define WM_CHANGELOG_READY (WM_APP + 1)
#define WM_VERSIONS_READY  (WM_APP + 2)

// This launcher's own version. Bump on each launcher release and keep latest.txt's
// mctde-launcher= line in sync, or self-update can loop.
static const char* LAUNCHER_VERSION = "0.5.0";

// ------------------------------------------------------------ control IDs
#define IDC_CHK_PHANTOM  1001
#define IDC_CHK_DSFIX    1002
#define IDC_BTN_CONFIG   1003
#define IDC_BTN_PLAY     1004
#define IDC_BTN_EXIT     1005
#define IDC_CHK_PRACTICE 1006
#define IDC_LNK_PRACTICE 1007
#define IDC_BTN_CHANGELOG 1008
#define IDC_CHK_AUTOUPDATE 1009
#define IDC_CHK_DARK 1010
#define IDC_CHK_DSCM 1011

// Timer that keeps re-scanning for DSCM while the row reads "Searching for DSCM...".
#define IDT_DSCM_POLL 1
// Short-lived timer that keeps a just-launched DSCM window behind the launcher (its window
// appears a moment after launch, landing on top, so we sink it back for a few seconds).
#define IDT_DSCM_FRONT 2
// Ticks once a second to refresh the top-left date/time readout.
#define IDT_CLOCK 3

// Where the greyed-out practice-tool row links when the tool isn't bundled.
// Eloise's PTDE Practice Tool (tasrunner branch).
static const wchar_t* PRACTICE_RELEASE_URL =
    L"https://github.com/helloeloise/ptde_practice_tool/tree/tasrunner";

// DSFix config dialog controls
#define IDC_DS_ADV       2050
#define IDC_DS_RW        2001
#define IDC_DS_RH        2002
#define IDC_DS_PW        2003
#define IDC_DS_PH        2004
#define IDC_DS_AATYPE    2005
#define IDC_DS_AAQUAL    2006
#define IDC_DS_SSAO      2007
#define IDC_DS_FILTER    2009
#define IDC_DS_BORDER    2010
#define IDC_DS_CAPCUR    2014
#define IDC_DS_DISCUR    2015
#define IDC_DS_SKIP      2016
#define IDC_DS_LOG       2017
#define IDC_DS_HUDMOD    2018
#define IDC_DS_DINPUT    2100   // base of 10 consecutive ids (2100..2109) for chain dropdowns
#define IDC_DS_DOFRES    2008
#define IDC_DS_DISDOFSCL 2020
#define IDC_DS_DOFBLUR   2021
#define IDC_DS_DEFDOF    2022
#define IDC_DS_UNLOCKFPS 2023
#define IDC_DS_FPSLIMIT  2024
#define IDC_DS_FPSTHRESH 2025
#define IDC_DS_FPSSTAB   2026
#define IDC_DS_ENBACKUP  2027
#define IDC_DS_BKPINT    2028
#define IDC_DS_BKPMAX    2029
#define IDC_DS_FORCEWIN  2030
#define IDC_DS_ENVSYNC   2031
#define IDC_DS_FSHZ      2032
#define IDC_DS_SAVE      2011
#define IDC_DS_CANCEL    2012
#define IDC_DS_REVERT    2013
// HUD Mod sub-window
#define IDC_HUD_EN       2060
#define IDC_HUD_MIN      2061
#define IDC_HUD_SCALE    2062
#define IDC_HUD_OP       2063
#define IDC_HUD_SAVE     2064
#define IDC_HUD_CANCEL   2065
#define IDC_HUD_LBL_SCALE 2066
#define IDC_HUD_LBL_OP    2067

// Hide Souls Counter (Advanced): toggles the mctde-link [HideSoulCounter] code patch. With
// HideBox=1 it clears the soul dialog's draw-enable byte ([this+0x98]) every frame, hiding the
// ENTIRE bottom-right soul display -- box/plate + icon + number + "+N" popup -- leaving the
// corner empty. Code-only (no DSFix HUD Mod, no texture edits); reversible on game close.
#define IDC_DS_HIDESOUL   2070

// Main-window "mctde Settings" button + its dialog.
#define IDC_BTN_MCTDE     1012
#define IDC_MC_ADV        2200
#define IDC_MC_SAVE       2201
#define IDC_MC_CANCEL     2202
#define IDC_MC_FONT       2203
#define IDC_MC_SIZE       2204
#define IDC_MC_CORNER     2205
#define IDC_MC_PADX       2206
#define IDC_MC_PADY       2207
#define IDC_MC_SHOWHDR    2208
#define IDC_MC_SHOWHP     2209
#define IDC_MC_SHOWPING   2210
#define IDC_MC_SHOWNAME   2211
#define IDC_MC_FORCETOP   2212
#define IDC_MC_PREVIEW    2213
#define IDC_MC_HPBARS     2214
#define IDC_MC_HPSIZE     2215
#define IDC_MC_DYN        2300   // base for the data-driven settings table (2300..)

static HINSTANCE g_inst = nullptr;
static HFONT     g_uiFont = nullptr;     // standard control font
static HFONT     g_titleFont = nullptr;  // "Dark Souls"
static HFONT     g_subFont = nullptr;    // "Prepare To Die Edition"
static HFONT     g_mctdeFont = nullptr;  // "mctde"
static HBITMAP   g_artorias = nullptr;   // trimmed white-on-black silhouette, drawn themed
static HBITMAP   g_bgDark = nullptr;     // full pre-composed banner; whole-window bg in dark mode
static HBITMAP   g_bgLight = nullptr;    // same, for light mode (pre-letterboxed to the client)

static HWND g_hMain       = nullptr;     // main window, for gating the bg image to it alone
static HWND g_chkPhantom  = nullptr;
static HWND g_chkDsfix    = nullptr;
static HWND g_btnConfig   = nullptr;
static HWND g_chkPractice = nullptr;
static HWND g_chkAutoUpdate = nullptr;
static HWND g_chkDark     = nullptr;
static HWND g_chkDscm     = nullptr;
static bool g_dscmReady   = false;      // a launchable DSCM.exe path is known
static DWORD g_dscmPid    = 0;          // pid of a DSCM we launched (to keep it behind us)
static int  g_dscmFrontTicks = 0;       // IDT_DSCM_FRONT countdown
static HWND g_lblVersion  = nullptr;
static HWND g_lblClock    = nullptr;     // live date/time readout in the top-left corner
static std::string g_latestLink, g_latestLauncher;  // filled by the update thread
static HWND g_lnkPractice = nullptr;   // shown instead of a label when the tool isn't bundled
static HFONT g_linkFont   = nullptr;   // underlined font for the link
static WNDPROC g_origStaticProc = nullptr;

// Subclass for the link static: show a hand cursor on hover; everything else is default.
static LRESULT CALLBACK LinkProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
    return CallWindowProcW(g_origStaticProc, h, m, w, l);
}

// ------------------------------------------------------------ paths
static std::wstring g_dir;  // directory the launcher lives in (the DATA folder), trailing '\\'

static std::wstring ModuleDir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"" : p.substr(0, slash + 1);
}
static std::wstring PathIn(const wchar_t* name) { return g_dir + name; }
static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// ------------------------------------------------------------ theme (light / dark)
static bool   g_dark = false;
static HBRUSH g_brBg = nullptr, g_brField = nullptr;

static COLORREF ThBg()     { return g_dark ? RGB(30, 30, 30)    : GetSysColor(COLOR_BTNFACE); }
static COLORREF ThText()   { return g_dark ? RGB(225, 225, 225) : RGB(0, 0, 0); }
static COLORREF ThField()  { return g_dark ? RGB(45, 45, 48)    : RGB(255, 255, 255); }
static COLORREF ThBtn()    { return g_dark ? RGB(55, 55, 58)    : RGB(225, 225, 228); }
static COLORREF ThBorder() { return g_dark ? RGB(95, 95, 100)   : RGB(120, 120, 120); }
static COLORREF ThAccent() { return g_dark ? RGB(205, 175, 95)  : RGB(150, 110, 30); }

static bool DarkModeEnabled() {
    return GetPrivateProfileIntW(L"Launcher", L"DarkMode", 0, PathIn(L"mctde-link.ini").c_str()) != 0;
}
static void SetDarkMode(bool on) {
    WritePrivateProfileStringW(L"Launcher", L"DarkMode", on ? L"1" : L"0", PathIn(L"mctde-link.ini").c_str());
}
static void ApplyTheme() {
    if (g_brBg)    DeleteObject(g_brBg);
    if (g_brField) DeleteObject(g_brField);
    g_brBg = CreateSolidBrush(ThBg());
    g_brField = CreateSolidBrush(ThField());
}

// Flat owner-drawn button (drawn in both themes -- the only portable way to get dark buttons
// without the Win10 dark-control APIs, which don't exist under Wine/Proton).
static void ThemeDrawButton(LPDRAWITEMSTRUCT d) {
    RECT r = d->rcItem;
    bool pressed   = (d->itemState & ODS_SELECTED) != 0;
    bool isDefault = (d->itemState & ODS_DEFAULT) != 0;
    bool disabled  = (d->itemState & ODS_DISABLED) != 0;
    COLORREF face = ThBtn();
    if (pressed) face = g_dark ? RGB(72, 72, 76) : RGB(200, 200, 203);
    HBRUSH fb = CreateSolidBrush(face); FillRect(d->hDC, &r, fb); DeleteObject(fb);
    HPEN pen = CreatePen(PS_SOLID, isDefault ? 2 : 1, isDefault ? ThAccent() : ThBorder());
    HGDIOBJ op = SelectObject(d->hDC, pen);
    HGDIOBJ ob = SelectObject(d->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(d->hDC, r.left, r.top, r.right, r.bottom);
    SelectObject(d->hDC, op); SelectObject(d->hDC, ob); DeleteObject(pen);
    wchar_t txt[64] = {0}; GetWindowTextW(d->hwndItem, txt, 64);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, disabled ? (g_dark ? RGB(120,120,120) : RGB(150,150,150)) : ThText());
    HFONT of = (HFONT)SelectObject(d->hDC, g_uiFont);
    RECT tr = r; if (pressed) { tr.left++; tr.top++; }
    DrawTextW(d->hDC, txt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->hDC, of);
}

// The pre-composed banner for the active theme (null if it failed to load).
static HBITMAP CurrentBg() { return g_dark ? g_bgDark : g_bgLight; }

// True when a pre-composed banner is the live background (on the main window). In that state the
// window's solid fill is replaced by the image and child controls paint transparently so it
// shows through behind them; the programmatic header is suppressed (the image carries it).
static bool BgActive(HWND hWnd) { return CurrentBg() && hWnd == g_hMain; }

// Stretch the active banner to fill the whole client area. The dark banner matches the client
// aspect and the light banner is pre-letterboxed to it, so this is a 1:1/uniform blit -- no
// visible stretch.
static void PaintBg(HWND hWnd, HDC hdc) {
    RECT rc; GetClientRect(hWnd, &rc);
    HBITMAP bmp = CurrentBg();
    BITMAP bm; GetObjectW(bmp, sizeof(bm), &bm);
    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ o = SelectObject(mem, bmp);
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(mem, o);
    DeleteDC(mem);
}

// Transparent controls don't erase, so when one repaints (checkbox toggled, label retitled) it
// can leave ghosts over the banner. Repaint the banner behind the control, then redraw it clean.
static void RepaintOverBg(HWND child) {
    if (!BgActive(g_hMain) || !child) return;
    RECT r; GetWindowRect(child, &r);
    MapWindowPoints(HWND_DESKTOP, g_hMain, (POINT*)&r, 2);
    RedrawWindow(g_hMain, &r, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// Shared themed message handling for every launcher window. Returns true (with *result set)
// when it handled the message; in light mode it mostly declines so the OS draws natively.
static bool ThemeHandle(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result) {
    switch (msg) {
    case WM_ERASEBKGND:
        if (BgActive(hWnd)) { PaintBg(hWnd, (HDC)wParam); *result = 1; return true; }
        if (g_dark) { RECT rc; GetClientRect(hWnd, &rc); FillRect((HDC)wParam, &rc, g_brBg); *result = 1; return true; }
        return false;
    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam == g_lnkPractice) {            // the download link keeps a blue
            SetTextColor((HDC)wParam, g_dark ? RGB(90, 160, 250) : RGB(0, 90, 200));
            SetBkColor((HDC)wParam, ThBg());
            SetBkMode((HDC)wParam, BgActive(hWnd) ? TRANSPARENT : OPAQUE);
            *result = (LRESULT)(BgActive(hWnd) ? GetStockObject(NULL_BRUSH)
                                               : (g_dark ? g_brBg : GetSysColorBrush(COLOR_BTNFACE)));
            return true;
        }
        // fall through to the generic static/button colouring
    case WM_CTLCOLORBTN:
        if (BgActive(hWnd)) {                            // let the banner show through the control
            // The Auto-Update checkbox sits over the banner's right half, which is the inverse
            // shade of the theme (cream in dark mode, black in light). Colour its text to contrast
            // with that half instead of the theme, or it vanishes into the background.
            SetTextColor((HDC)wParam, (HWND)lParam == g_chkAutoUpdate
                                          ? (g_dark ? RGB(20, 20, 20) : RGB(230, 230, 230))
                                          : ThText());
            SetBkMode((HDC)wParam, TRANSPARENT);
            *result = (LRESULT)GetStockObject(NULL_BRUSH);
            return true;
        }
        if (!g_dark) return false;
        SetTextColor((HDC)wParam, ThText());
        SetBkColor((HDC)wParam, ThBg());
        SetBkMode((HDC)wParam, OPAQUE);
        *result = (LRESULT)g_brBg;
        return true;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (!g_dark) return false;
        SetTextColor((HDC)wParam, ThText());
        SetBkColor((HDC)wParam, ThField());
        SetBkMode((HDC)wParam, OPAQUE);
        *result = (LRESULT)g_brField;
        return true;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lParam;
        if (d->CtlType == ODT_BUTTON) { ThemeDrawButton(d); *result = TRUE; return true; }
        return false;
    }
    }
    return false;
}

// ------------------------------------------------------------ mctde-link.ini (standard INI)
static bool PhantomEnabled() {
    wchar_t buf[16] = {0};
    GetPrivateProfileStringW(L"PhantomUnleashed", L"Mode", L"Off", buf, 16, PathIn(L"mctde-link.ini").c_str());
    return _wcsicmp(buf, L"On") == 0;
}
static void SetPhantom(bool on) {
    const std::wstring ini = PathIn(L"mctde-link.ini");
    WritePrivateProfileStringW(L"PhantomUnleashed", L"Mode", on ? L"On" : L"Off", ini.c_str());
    // The in-game engine reads VerifyOnly with a default that means "log only, never patch", so a
    // section that holds only Mode= silently does nothing. Make sure the apply switch is present
    // (only ever write it absent -> 0; never clobber a user who set it to 1 on a suspect exe).
    wchar_t vo[8] = {0};
    GetPrivateProfileStringW(L"PhantomUnleashed", L"VerifyOnly", L"", vo, 8, ini.c_str());
    if (vo[0] == L'\0')
        WritePrivateProfileStringW(L"PhantomUnleashed", L"VerifyOnly", L"0", ini.c_str());
}

// AA opt-in. By default the launcher forces DSFix antialiasing OFF before every launch:
// SMAA (its stencil-masked edge blend) and SSAO collide with the in-frame overlay and smear
// the world render with red/yellow garbage until a device reset. A user can deliberately
// re-enable AA from the DSFix config's Advanced Options, which sets this flag; the launch-time
// force-off then leaves their choice alone. Default 0 = AA enforced off.
static bool AaOptIn() {
    return GetPrivateProfileIntW(L"Launcher", L"AllowAA", 0, PathIn(L"mctde-link.ini").c_str()) != 0;
}
static void SetAaOptIn(bool on) {
    WritePrivateProfileStringW(L"Launcher", L"AllowAA", on ? L"1" : L"0",
                               PathIn(L"mctde-link.ini").c_str());
}

// ------------------------------------------------------------ optional-DLL enable/disable (rename)
// A toggleable DLL is "active" under its real name and "parked" as <name>.off when disabled.
// We never delete it, only rename. The chainloader's *.dll scan (and the game's wrapper loader)
// both skip the .off copy.
static void ToggleParked(const std::wstring& active, const std::wstring& parked, bool enable) {
    if (enable) {
        if (!FileExists(active) && FileExists(parked))
            MoveFileExW(parked.c_str(), active.c_str(), MOVEFILE_REPLACE_EXISTING);
    } else {
        if (FileExists(active)) {
            if (FileExists(parked)) DeleteFileW(parked.c_str());
            MoveFileW(active.c_str(), parked.c_str());
        }
    }
}

// DSFix is the dinput8.dll wrapper that sits beside DARKSOULS.exe.
static bool DsfixInstalled() {  // bundled at all (either active or parked)?
    return FileExists(PathIn(L"dinput8.dll")) || FileExists(PathIn(L"dinput8.dll.off"));
}
static bool DsfixEnabled() { return FileExists(PathIn(L"dinput8.dll")); }
static void ApplyDsfix(bool enable) {
    ToggleParked(PathIn(L"dinput8.dll"), PathIn(L"dinput8.dll.off"), enable);
}

// Eloise's PTDE Practice Tool is a dinput8.dll, but we bundle it in the ChainloadFolder
// (default mctde-Link_Chainload) under a UNIQUE name -- ptde_practice.dll -- so it does not
// collide with DSFix's own dinput8.dll beside DARKSOULS.exe. (Windows keys loaded modules by
// base name: a chainloaded "dinput8.dll" would just return DSFix's already-loaded handle and
// never run, so the unique name lets both run together.) A chainloaded tool's filename is
// irrelevant to the game -- mctde-Link only LoadLibrary's it to run its DllMain hooks. The
// chainloader scans <folder>\*.dll (dllmain.cpp), so parking it as .off disables it.
static std::wstring ChainloadDir() {
    wchar_t buf[MAX_PATH] = {0};
    GetPrivateProfileStringW(L"Compatibility", L"ChainloadFolder", L"mctde-Link_Chainload",
                             buf, MAX_PATH, PathIn(L"mctde-link.ini").c_str());
    std::wstring folder = (buf[0]) ? buf : L"mctde-Link_Chainload";
    return g_dir + folder + L"\\";
}
static std::wstring PracticeActive() { return ChainloadDir() + L"ptde_practice.dll"; }
static std::wstring PracticeParked() { return ChainloadDir() + L"ptde_practice.dll.off"; }
static bool PracticeInstalled() { return FileExists(PracticeActive()) || FileExists(PracticeParked()); }
static bool PracticeEnabled()   { return FileExists(PracticeActive()); }
static void ApplyPractice(bool enable) {
    ToggleParked(PracticeActive(), PracticeParked(), enable);
}

// ------------------------------------------------------------ dsfix.ini (space-separated "key value")
// DSFix uses lines of the form "renderWidth 1920" with '#' comments. We preserve the whole
// file and only rewrite the token following a known key (or append the key if it is missing).
static std::string ReadFileUtf8(const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return "";
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}
static void WriteFileUtf8(const std::wstring& path, const std::string& data) {
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    f.write(data.data(), (std::streamsize)data.size());
}
static std::wstring DsfixIniPath() { return PathIn(L"dsfix.ini"); }

// The full, documented default mctde-link.ini. SINGLE SOURCE OF TRUTH for a freshly-generated
// config: if the user deletes their ini, we rewrite THIS so they get every section + comment
// back, not a bare-bones file pieced together one key at a time by the first WritePrivateProfile
// call. Keep in sync with samples/mctde-link.sample.ini.
static const char* const kDefaultIni =
R"INI([Settings]
EnableLogging=0
; Force a clean process exit when the game window closes, so the overlay's background threads
; can't leave a lingering "zombie" DARKSOULS.exe behind. Leave at 1 unless it ever exits early.
ExitWithGame=1

[Launcher]
; 1 = if the game is started WITHOUT mctde_launcher.exe, close the game and open the launcher
; instead, so you always go through it. Only enforced when mctde_launcher.exe is present next
; to the game; set to 0 to allow launching the game directly.
RequireLauncher=1

[PhantomUnleashed]
; Built-in phantom-cap raiser. Rewrite of Metal-Crow's MultiPhantom/Overhaul patch on
; mctde-Link's patch engine; offset facts credited to his reverse-engineering work.
; Mode = Ask | On | Off
;   Ask = show a Yes/No box every launch (default).
;   On  = always enable, no prompt.   Off = never enable, no prompt.
; LINUX / PROTON: the Ask prompt is NOT supported and is skipped (PhantomUnleashed stays off).
;   You MUST set Mode=On or Mode=Off manually on Proton.
; IMPORTANT: while enabled you can ONLY connect with other players who also have
;   PhantomUnleashed enabled. Disabled = vanilla matchmaking with everyone.
Mode=Ask
; Total player slots in your world (stock is 4). KEEP THIS AT 18 for a stable >4 session:
; the Stage 2 offset-shift trampolines are calibrated for 18. Other values (4-32) apply
; only the Stage 1 cap + pool segregation and will NOT be a stable large session.
MaxPhantoms=18
; Matchmaking pool key (1 byte). The game only connects players whose NetworkVersion
; matches, so this is what segregates you from the vanilla pool. Vanilla retail = 0x2E.
; Default 0x4D = the PhantomUnleashed pool. ALL players in a session must share the SAME
; NetworkVersion *and* MaxPhantoms. Accepts hex (0x4D) or decimal (77). Set 0x2E to
; deliberately rejoin the vanilla pool (no segregation).
NetworkVersion=0x4D
; Game internal memory pool, in MB. Stock DS1 allocates only ~10 MB, which is sized for 4
; players and runs out once many phantoms load their models/gear/animations. Raised so an
; 18-phantom session has room. Range 0-255 (0 = leave stock). 192 is a safe default.
MemoryPoolMB=192
; SAFETY/DIAGNOSTIC: 0 = apply the patches when enabled (normal). 1 = only write the offset
;   self-check to PhantomUnleashed.log and never modify the game (use if you suspect a bad exe).
VerifyOnly=0

[HideSoulCounter]
; Hides the NUMBER on the bottom-right HUD soul counter. The soul icon and the rest of
; the HUD stay; only the digits stop being drawn. Reversible (reverts on game close).
; How it works: a single JZ->JMP byte flip in the F20 soul_param widget, located by a
; signature that is unique in the exe -- so it's safe across relocation. It only forces a
; code path the game already runs every frame your souls aren't changing.
; Enabled: 0 = off (default), 1 = on.
Enabled=0
; HideGainPopup: also suppress the transient "+N souls gained" popup that flashes on load-in
;   and when you pick up souls. 1 = hide it too (default), 0 = leave it.
HideGainPopup=1
; HideBox: hide the ENTIRE bottom-right soul display -- box/plate + soul icon + number + the
;   "+N" popup -- leaving the corner empty. Works by clearing the soul dialog's own draw-enable
;   byte every frame (the soul widget's per-frame update has the dialog object in hand), so the
;   dialog draws none of its children. Per-object, so no other HUD element or menu is affected;
;   no DSFix and no texture edits. When 1 it supersedes the number/popup hides above (a hidden
;   dialog draws neither). 0 = keep the box, hide only the number/popup (default); 1 = hide all.
HideBox=0
; HideRegion: the robust way to hide the WHOLE bottom-right soul counter (box + number + popup)
;   with no DSFix and no texture edits. Link wraps the D3D9 device; when this is on it drops the
;   game's 2D HUD draws (pre-transformed XYZRHW quads) whose every vertex lands in the bottom-
;   right rectangle below. No render capture/recomposite, so unlike DSFix HUD Mod it does NOT go
;   boxy during fades. 0 = off (default), 1 = on. (Your own overlay is drawn separately and is
;   never affected.)
HideRegion=0
; RegionX / RegionY: top-left corner of that kill rectangle, as a fraction of screen width/height
;   (the rect runs from here to the bottom-right corner). Defaults 0.78 / 0.78 cover the soul
;   counter only. Raise toward 1.0 to shrink the rect if it clips something else; lower to grow it.
RegionX=0.78
RegionY=0.78
; SAFETY/DIAGNOSTIC: 1 = only write the self-check to HideSoulCounter.log, never modify the
;   game (default). Set to 0 (with Enabled=1) to actually hide the number.
VerifyOnly=1

[Controller]
; First-launch controller helper. We run under Steam appid 480 (Spacewar), whose
; stock "Official Configuration" doesn't map to a real pad, so controllers look
; dead until you apply Steam's "Gamepad" template. When enabled, on the first
; launch that finds a connected controller we pop the Steam binding panel once
; (Browse Configs -> Templates -> Gamepad), then never again (a marker file
; records it). Keyboard/mouse users are never nudged.
; Requires a modern steam_api.dll shipped beside d3d9.dll as BindingNudgeModule
; (PTDE's own steam_api.dll is too old to open the panel).
BindingNudge=1
; Re-show every launch, ignoring the once-done marker (testing only).
BindingNudgeForce=0
; Filename of the modern Steam DLL we drive (ships next to d3d9.dll).
BindingNudgeModule=mctde_input.dll
; Steam interface version strings. These MUST match the SDK that BindingNudgeModule
; was built from (its flat wrappers call a vtable slot baked for that version).
; The shipped mctde_input.dll uses SteamInput002 / SteamClient020 -- only change
; these if you swap in a different steam_api.dll build.
BindingNudgeInputVersion=SteamInput002
BindingNudgeClientVersion=SteamClient020
; Delay (ms) after overlay start before nudging, so Steam's overlay/input settle.
BindingNudgeWaitMs=9000
; How long (ms) to wait for a controller to appear before giving up for this launch.
BindingNudgeControllerTimeoutMs=6000
; Optional manual re-trigger key (Windows virtual-key code; 0 = off). Opens the
; binding panel on demand if the first-launch timing missed. e.g. F10 = 0x79.
BindingNudgeKey=0

[Compatibility]
ChainloadFolder=mctde-Link_Chainload

[DLLs]
GenericDLL0=
GenericDLL1=
GenericDLL2=
GenericDLL3=
GenericDLL4=
GenericDLL5=
GenericDLL6=
GenericDLL7=
GenericDLL8=
GenericDLL9=

[Render]
; Backend = d3d (in-frame overlay, no stutter; drawn inside d3d9.dll itself --
;   no companion DLL needed) or gdi (old separate-window fallback).
; NOTE: the old mctde_overlay.dll companion is obsolete as of 0.1.3. If one is
;   left over in your ChainloadFolder it is ignored automatically -- you can delete it.
Backend=d3d
; How often (ms) the overlay bitmap is refreshed (16-500). 66 = ~15 Hz.
SubmitMs=66
; --- gdi-backend-only option (ignored when Backend=d3d) ---
; GDI-window repaint interval ms (66 = ~15Hz; lower = smoother but more DWM cost).
RepaintMs=66

[Overlay]
ShowHeader=1
MarkerGutterExtra=8
FontFace=Tahoma
FontHeight=24
HpFontHeight=24
LineHeight=20
Corner=top_left
PaddingX=5
PaddingY=5
RefreshMs=1000
HideLocal=0
ForceTopmost=0
; --- Master on/off hotkey ---
; Show/hide the whole overlay. Fires only while the game window is focused.
; Default is Shift+F3 (ToggleModifier=0x10 Shift, ToggleKey=0x72 F3) to avoid DSFix binds.
; Values are Windows virtual-key codes, decimal or hex.
;   Keys:      F3=0x72  F7=0x76  F8=0x77  F9=0x78  H=0x48  Insert=0x2D  ScrollLock=0x91
;   Modifiers: Shift=0x10  Ctrl=0x11  Alt=0x12  (set ToggleModifier=0 for no modifier)
ToggleModifier=0x10
ToggleKey=0x72
; --- Per-element display toggles (1=show, 0=hide) ---
; These hide elements without affecting the underlying systems; changes hot-reload ~1s.
; ShowHp also requires [HP] Enabled=1 (that switch gates HP polling entirely).
ShowHp=1
; HpBars: draw each player's HP as a Souls-style filled gauge (green->amber->red as it drops)
;   instead of a number. 0 = numbers (default), 1 = bars. Needs a readable max HP for that row
;   (falls back to the number when max HP isn't known, e.g. some remote players).
HpBars=0
ShowPing=1
ShowName=1
ShowLocalMarker=1
; When a player leaves the session WITHOUT dying, keep their row as "Disconnected  ---MS  Name"
; (greyed out) until a newcomer joins, who then replaces it. Set 0 to just drop the row instead.
ShowDisconnected=1
; --- Spacewar controller-setup instructions ---
; Shows a step-by-step panel for setting up a controller via Steam's Spacewar config.
; Visible by default (1). Pressing the hide bind (default Shift+F2, while the game window is
; focused) hides the panel AND writes ShowControllerSetup=0 here, so it stays gone next launch.
; Set back to 1 by hand to bring the instructions back.
ShowControllerSetup=1
ControllerSetupHideModifier=0x10
ControllerSetupHideKey=0x71

[HP]
Enabled=1
CurrentOffset=724
MaxOffset=728
OneHpLingerMs=500
; DamageLingerMs: with [Overlay] HpBars=1, how long (ms) the yellow "just lost" chip lingers
;   on an HP bar after a hit before it recedes to the new (green) HP level. Default 1000 (1s).
DamageLingerMs=1000
PollMs=33

[Debug]
DumpOverlayData=0
DebugP2PBridge=0

[WebSocket]
Enabled=0
Port=39876
SendMs=33

[TruePing]
UseHighResTimer=0
PollSleepMs=33
DisplayMode=2
BestWindow=8
Enabled=1
Debug=0
PreferOverlay=1
SendEnabled=1
ReceiveEnabled=1
Channel=63
AllowGameChannel=0
SendType=2
SendMs=1000
HelloMs=500
StaleMs=4000
)INI";

// If mctde-link.ini is missing (e.g. the user deleted it), write the full documented default so
// it regenerates complete rather than as a bare-bones file. Never touches an existing ini -- the
// per-key writers and the in-game MigrateIni handle partial/legacy files.
static void EnsureIniSeeded() {
    const std::wstring ini = PathIn(L"mctde-link.ini");
    if (!FileExists(ini))
        WriteFileUtf8(ini, kDefaultIni);
}

// Trim leading whitespace, return first token (the key) of a config line; "" if comment/blank.
static std::string LineKey(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || line[i] == '#' || line[i] == '\r' || line[i] == '\n') return "";
    size_t j = i;
    while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r' && line[j] != '\n') ++j;
    return line.substr(i, j - i);
}
static std::string DsfixGet(const std::string& text, const std::string& key, const std::string& def) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (LineKey(line) != key) continue;
        // value is the token after the key
        size_t i = line.find(key) + key.size();
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        size_t j = i;
        while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r' && line[j] != '\n') ++j;
        if (j > i) return line.substr(i, j - i);
        return def;
    }
    return def;
}
// Update key in-place (preserving comments/order) or append it. Returns new text.
static std::string DsfixSet(const std::string& text, const std::string& key, const std::string& value) {
    std::istringstream in(text);
    std::string line, out;
    bool found = false;
    bool crlf = text.find("\r\n") != std::string::npos;
    const std::string nl = crlf ? "\r\n" : "\n";
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!found && LineKey(line) == key) {
            out += key + " " + value + nl;
            found = true;
        } else {
            out += line + nl;
        }
    }
    if (!found) out += key + " " + value + nl;
    return out;
}

// ------------------------------------------------------------ launching the game
// The launcher runs the game as Steam appid 480 (Spacewar) so Steam Input / controllers work.
// PTDE's online matchmaking is keyed on that same appid, so EVERY install must agree on it: an
// install left on PTDE's real appid (211420) lands in a pool with no other mctde players and
// "can't connect to anyone". steam_appid.txt takes precedence over Steam's own env var, so we
// force it to 480 here. Overwrite only when it isn't already 480 (avoid needless disk writes).
static void EnsureSteamAppId() {
    const std::wstring p = PathIn(L"steam_appid.txt");
    std::string cur = FileExists(p) ? ReadFileUtf8(p) : std::string();
    const size_t a = cur.find_first_not_of(" \t\r\n");
    const size_t b = cur.find_last_not_of(" \t\r\n");
    const std::string val = (a == std::string::npos) ? std::string() : cur.substr(a, b - a + 1);
    if (val != "480")
        WriteFileUtf8(p, "480");
}

static bool LaunchGame() {
    // Mark the game process as launcher-spawned so mctde-Link's launcher guard lets it run
    // (the child inherits this env var). Without it, the mod relaunches the launcher and quits.
    SetEnvironmentVariableW(L"MCTDE_VIA_LAUNCHER", L"1");

    // Keep this install on the shared mctde matchmaking appid before the game's Steam init runs.
    EnsureSteamAppId();

    // Unless the user opted into AA via the DSFix config's Advanced Options, force antialiasing
    // off in dsfix.ini before launch -- SMAA/SSAO collide with the overlay and corrupt the world
    // render. Only rewrites when it's actually on; harmless if DSFix isn't installed.
    if (!AaOptIn() && FileExists(DsfixIniPath())) {
        std::string ini = ReadFileUtf8(DsfixIniPath());
        if (DsfixGet(ini, "aaType", "SMAA") != "none")
            WriteFileUtf8(DsfixIniPath(), DsfixSet(ini, "aaType", "none"));
    }

    const wchar_t* exes[] = { L"DARKSOULS.exe", L"DATA.exe" };
    for (const wchar_t* exe : exes) {
        std::wstring full = PathIn(exe);
        if (!FileExists(full)) continue;
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {0};
        std::wstring cmd = L"\"" + full + L"\"";
        std::vector<wchar_t> cmdbuf(cmd.begin(), cmd.end());
        cmdbuf.push_back(0);
        if (CreateProcessW(full.c_str(), cmdbuf.data(), nullptr, nullptr, FALSE,
                           0, nullptr, g_dir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }
    }
    MessageBoxW(nullptr,
        L"Could not find DARKSOULS.exe (or DATA.exe) next to the launcher.\n"
        L"Place mctde_launcher.exe in your Dark Souls PTDE DATA folder.",
        L"mctde Launcher", MB_OK | MB_ICONERROR);
    return false;
}

// ------------------------------------------------------------ DSFix config dialog
static HWND DsMakeLabel(HWND parent, const wchar_t* text, int x, int y, int w) {
    HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                           x, y, w, 18, parent, nullptr, g_inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    return h;
}
static HWND DsMakeEdit(HWND parent, int id, const std::string& val, int x, int y, int w) {
    std::wstring wv(val.begin(), val.end());
    HWND h = CreateWindowW(L"EDIT", wv.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                           x, y, w, 22, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    return h;
}
static HWND DsMakeCombo(HWND parent, int id, int x, int y, int w,
                        const std::vector<std::wstring>& items, const std::string& current) {
    HWND h = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                           x, y, w, 200, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    std::wstring cur(current.begin(), current.end());
    int sel = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)items[i].c_str());
        if (_wcsicmp(items[i].c_str(), cur.c_str()) == 0) sel = (int)i;
    }
    SendMessageW(h, CB_SETCURSEL, sel, 0);
    return h;
}
static HWND DsMakeCheck(HWND parent, int id, const wchar_t* text, bool checked) {
    HWND h = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                           0, 0, 10, 22, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    SendMessageW(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return h;
}
// small helpers to read control state
static bool   CtlChecked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }
static std::string CtlText(HWND h) {
    wchar_t b[128] = {0}; GetWindowTextW(h, b, 128);
    std::wstring w(b); return std::string(w.begin(), w.end());
}
static std::string CtlCombo(HWND h) {
    int s = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    wchar_t b[64] = {0}; SendMessageW(h, CB_GETLBTEXT, s, (LPARAM)b);
    std::wstring w(b); return std::string(w.begin(), w.end());
}

// DLLs in the DATA folder that could be chainloaded by DSFix's dinput8dllWrapper --
// excludes DSFix's own dinput8.dll, the d3d9.dll proxy, and DLLs that are core to the
// vanilla game (chaining those would do nothing useful or break the game). "none" leads.
static bool IsCoreDll(const wchar_t* n) {
    static const wchar_t* core[] = {
        L"dinput8.dll", L"d3d9.dll", L"fmodex.dll", L"fmod_event.dll",
        L"steam_api.dll", L"steam_api64.dll"
    };
    for (auto c : core) if (_wcsicmp(n, c) == 0) return true;
    return false;
}
static std::vector<std::wstring> DetectChainDlls() {
    std::vector<std::wstring> out;
    out.push_back(L"none");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_dir + L"*.dll").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (IsCoreDll(fd.cFileName)) continue;
            out.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return out;
}

// A chain dropdown listing the detected DLLs (+ "none"), keeping a configured-but-absent
// value selectable so it isn't silently lost.
static HWND MakeChainCombo(HWND parent, int id, const std::string& cur) {
    std::vector<std::wstring> items = DetectChainDlls();
    std::wstring cw(cur.begin(), cur.end());
    bool present = false;
    for (auto& s : items) if (_wcsicmp(s.c_str(), cw.c_str()) == 0) { present = true; break; }
    if (!present && !cur.empty()) items.push_back(cw);
    return DsMakeCombo(parent, id, 0, 0, 240, items, cur);
}

// Combos whose visible label differs from the value stored in dsfix.ini.
static const std::vector<std::wstring> AAQ_LBL  = { L"Low", L"Medium", L"High", L"Ultra" };
static const std::vector<std::string>  AAQ_VAL  = { "1", "2", "3", "4" };
static const std::vector<std::wstring> SSAO_LBL = { L"Off", L"Medium", L"High", L"Ultra" };
static const std::vector<std::string>  SSAO_VAL = { "0", "1", "2", "3" };
static const std::vector<std::wstring> FILT_LBL = { L"None", L"Bilinear", L"Anisotropic" };
static const std::vector<std::string>  FILT_VAL = { "0", "1", "2" };
// DSFix logLevel 0-11 (higher = more logging = more per-frame disk I/O). 0 = off, recommended
// for play; the higher levels are debug-only and can cause frametime spikes.
static const std::vector<std::wstring> LOG_LBL = {
    L"Off (0)", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"10", L"11" };
static const std::vector<std::string>  LOG_VAL = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11" };

static HWND MakeMappedCombo(HWND parent, int id, int w,
                            const std::vector<std::wstring>& labels,
                            const std::vector<std::string>& values, const std::string& cur) {
    HWND h = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                           0, 0, w, 200, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    int sel = 0;
    for (size_t i = 0; i < labels.size(); ++i) {
        SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)labels[i].c_str());
        if (values[i] == cur) sel = (int)i;
    }
    SendMessageW(h, CB_SETCURSEL, sel, 0);
    return h;
}
static std::string MappedValue(HWND h, const std::vector<std::string>& values) {
    int s = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    if (s < 0 || s >= (int)values.size()) s = 0;
    return values[s];
}
// Select the combo entry whose visible label matches (case-insensitive). No-op if absent.
static void SetCombo(HWND h, const wchar_t* label) {
    int n = (int)SendMessageW(h, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; ++i) {
        wchar_t b[64] = {0}; SendMessageW(h, CB_GETLBTEXT, i, (LPARAM)b);
        if (_wcsicmp(b, label) == 0) { SendMessageW(h, CB_SETCURSEL, i, 0); return; }
    }
}

static std::string g_dsText;        // loaded dsfix.ini contents, edited on save
static bool g_advanced = false;     // Advanced Options toggle (per-open, not persisted)
static bool g_fpsStabInit = false;  // initial basic-mode "FPS Stabilizer" state (for change detection)
static bool g_defDofInit = false;   // initial basic-mode "Original DoF" state (for change detection)
// HUD Mod values live here; the HUD sub-window edits them, the main Save writes them.
static std::string g_hudEn, g_hudMin, g_hudScale, g_hudOpacity;

// Every config control we need to lay out / read back.
struct DsUi {
    HWND adv, hdrVideo;
    HWND lblRes, rw, resX, rh;
    HWND lblOut, pw, outX, ph;
    HWND lblAA, aaType, lblAAQ, aaQual;
    HWND lblSSAO, ssao, lblFilter, filter;
    HWND border;
    HWND hudMod;
    HWND hdrCursor, capCur, disCur;
    HWND hdrOther, skipIntro, lblLog, logLvl;
    HWND hdrDinput, dinput[10];
    HWND hdrDof, lblDofRes, dofRes, disDofScale, lblDofBlur, dofBlur, defDof;
    HWND hdrFps, unlockFps, lblFpsLimit, fpsLimit, lblFpsThresh, fpsThresh, fpsStab;
    HWND hdrBackup, enBackup, lblBkpInt, bkpInt, lblBkpMax, bkpMax;
    HWND hdrNotReady, forceWin, enVsync, lblFsHz, fsHz;
    HWND credit, revert, save, cancel;
};
static DsUi U;

static const int DS_CLIENTW = 400;

// ---------------- HUD Mod sub-window ----------------
// Everything except the master "Enable HUD Mod" checkbox is greyed out when it's unchecked.
static void HudSetGroupEnabled(HWND w, bool en) {
    EnableWindow(GetDlgItem(w, IDC_HUD_MIN), en);
    EnableWindow(GetDlgItem(w, IDC_HUD_LBL_SCALE), en);
    EnableWindow(GetDlgItem(w, IDC_HUD_SCALE), en);
    EnableWindow(GetDlgItem(w, IDC_HUD_LBL_OP), en);
    EnableWindow(GetDlgItem(w, IDC_HUD_OP), en);
}

static LRESULT CALLBACK HudWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT _tr = 0;
    if (ThemeHandle(hWnd, msg, wParam, lParam, &_tr)) return _tr;
    switch (msg) {
    case WM_CREATE: {
        int lx = 16, fx = 180, y = 16, dy = 30;
        DsMakeCheck(hWnd, IDC_HUD_EN,  L"Enable HUD Mod",     g_hudEn == "1");
        SetWindowPos(GetDlgItem(hWnd, IDC_HUD_EN), nullptr, lx, y, 250, 22, SWP_NOZORDER); y += dy;
        DsMakeCheck(hWnd, IDC_HUD_MIN, L"Minimal HUD",        g_hudMin == "1");
        SetWindowPos(GetDlgItem(hWnd, IDC_HUD_MIN), nullptr, lx, y, 250, 22, SWP_NOZORDER); y += dy;
        HWND ls = CreateWindowW(L"STATIC", L"HUD scale factor", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                lx, y + 3, 150, 18, hWnd, (HMENU)IDC_HUD_LBL_SCALE, g_inst, nullptr);
        SendMessageW(ls, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        DsMakeEdit(hWnd, IDC_HUD_SCALE, g_hudScale, fx, y, 70); y += dy;
        HWND lo = CreateWindowW(L"STATIC", L"Opacity (0-1)", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                lx, y + 3, 150, 18, hWnd, (HMENU)IDC_HUD_LBL_OP, g_inst, nullptr);
        SendMessageW(lo, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        DsMakeEdit(hWnd, IDC_HUD_OP, g_hudOpacity, fx, y, 70); y += dy + 6;
        HWND s = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
                               fx, y, 70, 28, hWnd, (HMENU)IDC_HUD_SAVE, g_inst, nullptr);
        HWND c = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
                               fx + 80, y, 70, 28, hWnd, (HMENU)IDC_HUD_CANCEL, g_inst, nullptr);
        SendMessageW(s, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        SendMessageW(c, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        HudSetGroupEnabled(hWnd, g_hudEn == "1");   // initial grey-out state
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_HUD_EN) {
            HudSetGroupEnabled(hWnd, CtlChecked(GetDlgItem(hWnd, IDC_HUD_EN)));
            return 0;
        }
        if (LOWORD(wParam) == IDC_HUD_SAVE) {
            g_hudEn      = CtlChecked(GetDlgItem(hWnd, IDC_HUD_EN)) ? "1" : "0";
            g_hudMin     = CtlChecked(GetDlgItem(hWnd, IDC_HUD_MIN)) ? "1" : "0";
            g_hudScale   = CtlText(GetDlgItem(hWnd, IDC_HUD_SCALE));
            g_hudOpacity = CtlText(GetDlgItem(hWnd, IDC_HUD_OP));
            DestroyWindow(hWnd);
            return 0;
        }
        if (LOWORD(wParam) == IDC_HUD_CANCEL) { DestroyWindow(hWnd); return 0; }
        return 0;
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY:
        if (HWND owner = GetWindow(hWnd, GW_OWNER)) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void OpenHudConfig(HWND owner) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = HudWndProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"mctdeHudConfig";
        RegisterClassW(&wc);
        registered = true;
    }
    RECT rc; GetWindowRect(owner, &rc);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"mctdeHudConfig", L"HUD Mod",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        rc.left + 50, rc.top + 60, 360, 215, owner, nullptr, g_inst, nullptr);
    EnableWindow(owner, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (!IsWindow(dlg)) break;
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

// ---------------- layout (depends on g_advanced) ----------------
// position helper (default height suits single-line labels) + visibility helper
static void mv(HWND h, int x, int y, int w, int ht = 18) { SetWindowPos(h, nullptr, x, y, w, ht, SWP_NOZORDER); }
static void sh(HWND h, bool v) { ShowWindow(h, v ? SW_SHOW : SW_HIDE); }

static void LayoutDsfix(HWND hWnd) {
    const bool adv = g_advanced;
    const int LX = 16, FX = 178, ROW = 26, HDR = 22;
    int y = 14;

    mv(U.adv, LX, y, 250, 22); y += 30;

    // --- Video ---
    mv(U.hdrVideo, LX, y, 250, 18); y += HDR;
    mv(U.lblRes, LX, y + 3, 150); mv(U.rw, FX, y, 56, 22); mv(U.resX, FX + 60, y + 3, 10, 18); mv(U.rh, FX + 74, y, 56, 22); y += ROW;
    mv(U.lblOut, LX, y + 3, 150); mv(U.pw, FX, y, 56, 22); mv(U.outX, FX + 60, y + 3, 10, 18); mv(U.ph, FX + 74, y, 56, 22); y += ROW;
    // AA is Advanced-only: by default the launcher forces it off before every launch (overlay
    // collision), so it's hidden in basic mode and surfaced under Advanced for deliberate opt-in.
    sh(U.lblAA, adv); sh(U.aaType, adv); sh(U.lblAAQ, adv); sh(U.aaQual, adv);
    if (adv) {
        mv(U.lblAA, LX, y + 3, 28); mv(U.aaType, 48, y, 96, 200); mv(U.lblAAQ, 150, y + 3, 66); mv(U.aaQual, 220, y, 80, 200); y += ROW;
    }
    mv(U.lblSSAO, LX, y + 3, 40); mv(U.ssao, 58, y, 78, 200); mv(U.lblFilter, 150, y + 3, 110); mv(U.filter, 262, y, 110, 200); y += ROW;
    mv(U.border, LX, y, 250, 22); y += ROW;        // normal left checkbox
    // HUD Mod is Advanced-only: its sub-window recomposites the whole HUD off the 3D scene
    // (boxy/glitchy on cutscene fades), so it's a niche, opt-in tweak.
    sh(U.hudMod, adv);
    if (adv) { mv(U.hudMod, LX, y, 250, 22); y += ROW; }

    // --- Cursor ---
    mv(U.hdrCursor, LX, y, 250, 18); y += HDR;
    mv(U.capCur, LX, y, 180, 22); mv(U.disCur, 200, y, 180, 22); y += ROW;

    // --- Other ---
    mv(U.hdrOther, LX, y, 250, 18); y += HDR;
    mv(U.skipIntro, LX, y, 180, 22); mv(U.lblLog, 200, y + 3, 66); mv(U.logLvl, 270, y, 96, 200); y += ROW;

    // --- Depth of Field ---
    mv(U.hdrDof, LX, y, 250, 18); y += HDR;
    sh(U.lblDofRes, adv); sh(U.dofRes, adv); sh(U.disDofScale, adv); sh(U.lblDofBlur, adv); sh(U.dofBlur, adv);
    sh(U.defDof, !adv);
    if (adv) {
        mv(U.lblDofRes, LX, y + 3, 150); mv(U.dofRes, FX, y, 60, 22); y += ROW;
        mv(U.disDofScale, LX, y, 250, 22); y += ROW;
        mv(U.lblDofBlur, LX, y + 3, 150); mv(U.dofBlur, FX, y, 60, 22); y += ROW;
    } else {
        mv(U.defDof, LX, y, 250, 22); y += ROW;
    }

    // --- Framerate ---
    mv(U.hdrFps, LX, y, 250, 18); y += HDR;
    sh(U.unlockFps, adv); sh(U.lblFpsLimit, adv); sh(U.fpsLimit, adv); sh(U.lblFpsThresh, adv); sh(U.fpsThresh, adv);
    sh(U.fpsStab, !adv);
    if (adv) {
        mv(U.unlockFps, LX, y, 250, 22); y += ROW;
        mv(U.lblFpsLimit, LX, y + 3, 110); mv(U.fpsLimit, 122, y, 56, 22);
        mv(U.lblFpsThresh, 196, y + 3, 92); mv(U.fpsThresh, 292, y, 56, 22); y += ROW;
    } else {
        mv(U.fpsStab, LX, y, 250, 22); y += ROW;
    }

    // --- Save Game Backups (advanced only) ---
    sh(U.hdrBackup, adv); sh(U.enBackup, adv); sh(U.lblBkpInt, adv); sh(U.bkpInt, adv); sh(U.lblBkpMax, adv); sh(U.bkpMax, adv);
    if (adv) {
        mv(U.hdrBackup, LX, y, 280, 18); y += HDR;
        mv(U.enBackup, LX, y, 250, 22); y += ROW;
        mv(U.lblBkpInt, LX, y + 3, 150); mv(U.bkpInt, FX, y, 60, 22); y += ROW;
        mv(U.lblBkpMax, LX, y + 3, 150); mv(U.bkpMax, FX, y, 60, 22); y += ROW;
    }

    // --- Not-ready-to-use section (advanced only) ---
    sh(U.hdrNotReady, adv); sh(U.forceWin, adv); sh(U.enVsync, adv); sh(U.lblFsHz, adv); sh(U.fsHz, adv);
    if (adv) {
        mv(U.hdrNotReady, LX, y, 360, 18); y += HDR;
        mv(U.forceWin, LX, y, 210, 22); mv(U.enVsync, 230, y, 150, 22); y += ROW;
        mv(U.lblFsHz, LX, y + 3, 150); mv(U.fsHz, FX, y, 60, 22); y += ROW;
    }

    // --- Dinput DLL Chaining (advanced only): cascading dropdowns ---
    sh(U.hdrDinput, adv);
    if (adv) {
        mv(U.hdrDinput, LX, y, 280, 18); y += HDR;
        bool prevInUse = true;   // first dropdown always shows; each next appears once the prior is used
        for (int i = 0; i < 10; ++i) {
            sh(U.dinput[i], prevInUse);
            if (prevInUse) { mv(U.dinput[i], LX, y, 240, 200); y += ROW; }
            std::string v = CtlCombo(U.dinput[i]);
            prevInUse = prevInUse && v != "none" && !v.empty();
        }
    } else {
        for (int i = 0; i < 10; ++i) sh(U.dinput[i], false);
    }

    mv(U.credit, LX, y + 4, 370, 16); y += 26;
    mv(U.revert, LX, y, 130, 28);
    mv(U.save, DS_CLIENTW - 164, y, 70, 28); mv(U.cancel, DS_CLIENTW - 86, y, 70, 28); y += 40;

    // resize the window to fit the laid-out content, then nudge it up if the (taller)
    // advanced layout would run off the bottom of the screen's work area.
    RECT r = { 0, 0, DS_CLIENTW, y };
    AdjustWindowRect(&r, (DWORD)GetWindowLongPtrW(hWnd, GWL_STYLE), FALSE);
    int winW = r.right - r.left, winH = r.bottom - r.top;
    RECT cur; GetWindowRect(hWnd, &cur);
    RECT wa = {0}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int left = cur.left, top = cur.top;
    if (top + winH > wa.bottom) top = (wa.bottom - winH > wa.top) ? (wa.bottom - winH) : wa.top;
    SetWindowPos(hWnd, nullptr, left, top, winW, winH, SWP_NOZORDER);
}

static LRESULT CALLBACK DsfixWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT _tr = 0;
    if (ThemeHandle(hWnd, msg, wParam, lParam, &_tr)) return _tr;
    switch (msg) {
    case WM_CREATE: {
        g_dsText = ReadFileUtf8(DsfixIniPath());
        g_advanced = false;
        auto G = [&](const char* k, const char* d) { return DsfixGet(g_dsText, k, d); };

        // HUD values cached for the sub-window + save
        g_hudEn      = G("enableHudMod", "0");
        g_hudMin     = G("enableMinimalHud", "0");
        g_hudScale   = G("hudScaleFactor", "1.0");
        g_hudOpacity = G("hudOpacity", "1.0");

        U.adv = DsMakeCheck(hWnd, IDC_DS_ADV, L"Enable Advanced Options", false);
        U.hdrVideo = DsMakeLabel(hWnd, L"--- Video ---", 0, 0, 250);

        U.lblRes = DsMakeLabel(hWnd, L"Internal resolution (W x H)", 0, 0, 150);
        U.rw = DsMakeEdit(hWnd, IDC_DS_RW, G("renderWidth", "1920"), 0, 0, 56);
        U.resX = DsMakeLabel(hWnd, L"x", 0, 0, 10);
        U.rh = DsMakeEdit(hWnd, IDC_DS_RH, G("renderHeight", "1080"), 0, 0, 56);

        U.lblOut = DsMakeLabel(hWnd, L"Output resolution (W x H)", 0, 0, 150);
        U.pw = DsMakeEdit(hWnd, IDC_DS_PW, G("presentWidth", "1920"), 0, 0, 56);
        U.outX = DsMakeLabel(hWnd, L"x", 0, 0, 10);
        U.ph = DsMakeEdit(hWnd, IDC_DS_PH, G("presentHeight", "1080"), 0, 0, 56);

        // Reflect the effective AA state: when the user hasn't opted in, the launch-time
        // force-off keeps it at none regardless of what dsfix.ini currently says, so show none.
        std::string aaInit = AaOptIn() ? G("aaType", "SMAA") : std::string("none");
        U.lblAA = DsMakeLabel(hWnd, L"AA", 0, 0, 28);
        U.aaType = DsMakeCombo(hWnd, IDC_DS_AATYPE, 0, 0, 96, { L"none", L"SMAA", L"FXAA" }, aaInit);
        U.lblAAQ = DsMakeLabel(hWnd, L"AA Quality", 0, 0, 66);
        U.aaQual = MakeMappedCombo(hWnd, IDC_DS_AAQUAL, 80, AAQ_LBL, AAQ_VAL, G("aaQuality", "4"));

        U.lblSSAO = DsMakeLabel(hWnd, L"SSAO", 0, 0, 40);
        U.ssao = MakeMappedCombo(hWnd, IDC_DS_SSAO, 80, SSAO_LBL, SSAO_VAL, G("ssaoStrength", "0"));
        U.lblFilter = DsMakeLabel(hWnd, L"Texture Filtering", 0, 0, 110);
        U.filter = MakeMappedCombo(hWnd, IDC_DS_FILTER, 110, FILT_LBL, FILT_VAL, G("filteringOverride", "0"));

        U.border = DsMakeCheck(hWnd, IDC_DS_BORDER, L"Borderless Fullscreen", G("borderlessFullscreen", "0") == "1");
        U.hudMod = DsMakeCheck(hWnd, IDC_DS_HUDMOD, L"HUD Mod (opens options)", g_hudEn == "1");
        // (Hide Soul Counter moved out of DSFix Config -> it's an mctde-Link feature, configured
        //  in the "mctde Settings" dialog off the main launcher, not here.)
        // AA Quality is meaningless when antialiasing is off.
        if (aaInit == "none") EnableWindow(U.aaQual, FALSE);

        U.hdrOther = DsMakeLabel(hWnd, L"--- Other ---", 0, 0, 250);
        U.skipIntro = DsMakeCheck(hWnd, IDC_DS_SKIP, L"Skip intro", G("skipIntro", "1") == "1");
        U.lblLog = DsMakeLabel(hWnd, L"Log Level", 0, 0, 66);
        U.logLvl = MakeMappedCombo(hWnd, IDC_DS_LOG, 96, LOG_LBL, LOG_VAL, G("logLevel", "6"));

        U.hdrCursor = DsMakeLabel(hWnd, L"--- Cursor ---", 0, 0, 250);
        U.capCur = DsMakeCheck(hWnd, IDC_DS_CAPCUR, L"Capture cursor", G("captureCursor", "1") == "1");
        U.disCur = DsMakeCheck(hWnd, IDC_DS_DISCUR, L"Disable cursor", G("disableCursor", "1") == "1");

        // Dinput DLL chaining (Advanced): a cascading list of dropdowns. The first holds
        // DSFix's dinput8dllWrapper; extras are stored in dinputChain1..9 (DSFix ignores them).
        U.hdrDinput = DsMakeLabel(hWnd, L"--- Dinput DLL Chaining ---", 0, 0, 280);
        for (int i = 0; i < 10; ++i) {
            std::string cur = (i == 0) ? G("dinput8dllWrapper", "none")
                                       : G(("dinputChain" + std::to_string(i)).c_str(), "none");
            U.dinput[i] = MakeChainCombo(hWnd, IDC_DS_DINPUT + i, cur);
        }

        // DoF
        U.hdrDof = DsMakeLabel(hWnd, L"--- Depth of Field ---", 0, 0, 250);
        U.lblDofRes = DsMakeLabel(hWnd, L"DOF override resolution", 0, 0, 150);
        U.dofRes = DsMakeEdit(hWnd, IDC_DS_DOFRES, G("dofOverrideResolution", "540"), 0, 0, 60);
        U.disDofScale = DsMakeCheck(hWnd, IDC_DS_DISDOFSCL, L"Disable DOF scaling", G("disableDofScaling", "1") == "1");
        U.lblDofBlur = DsMakeLabel(hWnd, L"DOF additional blur", 0, 0, 150);
        U.dofBlur = DsMakeEdit(hWnd, IDC_DS_DOFBLUR, G("dofBlurAmount", "1"), 0, 0, 60);
        U.defDof = DsMakeCheck(hWnd, IDC_DS_DEFDOF, L"Original DoF", false);

        // Framerate
        U.hdrFps = DsMakeLabel(hWnd, L"--- Framerate ---", 0, 0, 250);
        U.unlockFps = DsMakeCheck(hWnd, IDC_DS_UNLOCKFPS, L"Unlock FPS", G("unlockFPS", "1") == "1");
        U.lblFpsLimit = DsMakeLabel(hWnd, L"FPS limit", 0, 0, 110);
        U.fpsLimit = DsMakeEdit(hWnd, IDC_DS_FPSLIMIT, G("FPSlimit", "30"), 0, 0, 56);
        U.lblFpsThresh = DsMakeLabel(hWnd, L"FPS threshold", 0, 0, 92);
        U.fpsThresh = DsMakeEdit(hWnd, IDC_DS_FPSTHRESH, G("FPSthreshold", "28"), 0, 0, 56);
        U.fpsStab = DsMakeCheck(hWnd, IDC_DS_FPSSTAB, L"FPS Stabilizer", G("unlockFPS", "1") != "0");
        // Snapshot the basic-mode toggle states so Save can tell whether the user changed them.
        g_fpsStabInit = CtlChecked(U.fpsStab);
        g_defDofInit = CtlChecked(U.defDof);

        // Backups (advanced)
        U.hdrBackup = DsMakeLabel(hWnd, L"--- Save Game Backup Options ---", 0, 0, 280);
        U.enBackup = DsMakeCheck(hWnd, IDC_DS_ENBACKUP, L"Enable backups", G("enableBackups", "1") == "1");
        U.lblBkpInt = DsMakeLabel(hWnd, L"Backup interval", 0, 0, 150);
        U.bkpInt = DsMakeEdit(hWnd, IDC_DS_BKPINT, G("backupInterval", "10"), 0, 0, 60);
        U.lblBkpMax = DsMakeLabel(hWnd, L"Max backups", 0, 0, 150);
        U.bkpMax = DsMakeEdit(hWnd, IDC_DS_BKPMAX, G("maxBackups", "10"), 0, 0, 60);

        // Not-ready section (advanced)
        U.hdrNotReady = DsMakeLabel(hWnd, L"--- These settings are not ready to use ---", 0, 0, 360);
        U.forceWin = DsMakeCheck(hWnd, IDC_DS_FORCEWIN, L"ForceWindowed/ForceFullscreen", G("forceWindowed", "0") == "1");
        U.enVsync = DsMakeCheck(hWnd, IDC_DS_ENVSYNC, L"Enable Vsync", G("enableVsync", "0") == "1");
        U.lblFsHz = DsMakeLabel(hWnd, L"Fullscreen Hz", 0, 0, 150);
        U.fsHz = DsMakeEdit(hWnd, IDC_DS_FSHZ, G("fullscreenHz", "60"), 0, 0, 60);

        U.credit = CreateWindowW(L"STATIC",
            L"DSFix by Durante - GPL-3.0. Source: github.com/PeterTh/dsfix",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 370, 16, hWnd, nullptr, g_inst, nullptr);
        SendMessageW(U.credit, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        U.revert = CreateWindowW(L"BUTTON", L"Revert to Defaults", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
                                 0, 0, 130, 28, hWnd, (HMENU)IDC_DS_REVERT, g_inst, nullptr);
        SendMessageW(U.revert, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        U.save = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
                               0, 0, 70, 28, hWnd, (HMENU)IDC_DS_SAVE, g_inst, nullptr);
        U.cancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
                                 0, 0, 70, 28, hWnd, (HMENU)IDC_DS_CANCEL, g_inst, nullptr);
        SendMessageW(U.save, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        SendMessageW(U.cancel, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        LayoutDsfix(hWnd);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            if (id == IDC_DS_AATYPE) { EnableWindow(U.aaQual, CtlCombo(U.aaType) != "none"); return 0; }
            if (id >= IDC_DS_DINPUT && id < IDC_DS_DINPUT + 10) { LayoutDsfix(hWnd); return 0; }
        }
        if (id == IDC_DS_ADV) {
            bool checked = CtlChecked(U.adv);
            if (checked && !g_advanced) {
                int r = MessageBoxW(hWnd,
                    L"Experimenting with advanced features could result in unpredictable behavior.",
                    L"Enable Advanced Options", MB_YESNO | MB_ICONWARNING);
                if (r != IDYES) { SendMessageW(U.adv, BM_SETCHECK, BST_UNCHECKED, 0); return 0; }
                g_advanced = true; LayoutDsfix(hWnd);
            } else if (!checked && g_advanced) {
                g_advanced = false; LayoutDsfix(hWnd);
            }
            return 0;
        }
        if (id == IDC_DS_HUDMOD) {
            if (CtlChecked(U.hudMod)) {
                OpenHudConfig(hWnd);
                SendMessageW(U.hudMod, BM_SETCHECK, g_hudEn == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
            } else {
                g_hudEn = "0";
            }
            return 0;
        }
        // The basic "FPS Stabilizer" and advanced "Unlock FPS" checkboxes are two faces of
        // the same unlockFPS key. Keep them in lockstep so flipping one (then switching modes
        // or saving) doesn't leave the other showing the stale ini value and silently winning.
        if (id == IDC_DS_FPSSTAB) {
            bool on = CtlChecked(U.fpsStab);
            SendMessageW(U.unlockFps, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            SetWindowTextW(U.fpsLimit, L"30");   // basic mode pins the limit at 30 either way
            return 0;
        }
        if (id == IDC_DS_UNLOCKFPS) {
            SendMessageW(U.fpsStab, BM_SETCHECK,
                         CtlChecked(U.unlockFps) ? BST_CHECKED : BST_UNCHECKED, 0);
            return 0;
        }
        if (id == IDC_DS_REVERT) {
            if (MessageBoxW(hWnd,
                    L"Reset all DSFix settings to the launcher defaults?\n"
                    L"Nothing is written to disk until you click Save.",
                    L"Revert to Defaults", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
            auto setChk = [&](HWND h, bool on) {
                SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            };
            // Fill the visible form with the defaults.
            SetWindowTextW(U.rw, L"1920"); SetWindowTextW(U.rh, L"1080");
            SetWindowTextW(U.pw, L"0");    SetWindowTextW(U.ph, L"0");
            SetCombo(U.aaType, L"none");   SetCombo(U.aaQual, L"Low");
            SetCombo(U.ssao, L"Off");      SetCombo(U.filter, L"None");
            setChk(U.border, true);
            setChk(U.hudMod, false);
            setChk(U.capCur, false);       setChk(U.disCur, true);
            setChk(U.skipIntro, true);     SetCombo(U.logLvl, L"Off (0)");
            SetWindowTextW(U.dofRes, L"540"); setChk(U.disDofScale, true); SetWindowTextW(U.dofBlur, L"1");
            setChk(U.defDof, false);
            setChk(U.unlockFps, false);    SetWindowTextW(U.fpsLimit, L"30"); SetWindowTextW(U.fpsThresh, L"28");
            setChk(U.fpsStab, false);
            setChk(U.enBackup, false);     SetWindowTextW(U.bkpInt, L"1500"); SetWindowTextW(U.bkpMax, L"10");
            setChk(U.forceWin, false);     setChk(U.enVsync, false); SetWindowTextW(U.fsHz, L"60");
            for (int i = 0; i < 10; ++i) SetCombo(U.dinput[i], L"none");
            EnableWindow(U.aaQual, FALSE);   // AA none => quality is meaningless
            // HUD Mod sub-window values (the main Save writes these from the globals).
            g_hudEn = "0"; g_hudMin = "0"; g_hudScale = "1.0"; g_hudOpacity = "1.0";
            // Also stamp the defaults straight into the loaded ini text. Save only writes a key
            // when the form value differs from g_dsText, and basic mode skips DoF/FPS unless their
            // toggle changed; seeding g_dsText here guarantees a Save lands the full default set
            // regardless of which mode the dialog is in, while leaving unrelated keys untouched.
            g_dsText = DsfixSet(g_dsText, "renderWidth", "1920");
            g_dsText = DsfixSet(g_dsText, "renderHeight", "1080");
            g_dsText = DsfixSet(g_dsText, "presentWidth", "0");
            g_dsText = DsfixSet(g_dsText, "presentHeight", "0");
            g_dsText = DsfixSet(g_dsText, "aaType", "none");
            g_dsText = DsfixSet(g_dsText, "aaQuality", "1");
            g_dsText = DsfixSet(g_dsText, "ssaoStrength", "0");
            g_dsText = DsfixSet(g_dsText, "filteringOverride", "0");
            g_dsText = DsfixSet(g_dsText, "borderlessFullscreen", "1");
            g_dsText = DsfixSet(g_dsText, "captureCursor", "0");
            g_dsText = DsfixSet(g_dsText, "disableCursor", "1");
            g_dsText = DsfixSet(g_dsText, "skipIntro", "1");
            g_dsText = DsfixSet(g_dsText, "logLevel", "0");
            g_dsText = DsfixSet(g_dsText, "enableHudMod", "0");
            g_dsText = DsfixSet(g_dsText, "enableMinimalHud", "0");
            g_dsText = DsfixSet(g_dsText, "hudScaleFactor", "1.0");
            g_dsText = DsfixSet(g_dsText, "hudOpacity", "1.0");
            g_dsText = DsfixSet(g_dsText, "dofOverrideResolution", "540");
            g_dsText = DsfixSet(g_dsText, "disableDofScaling", "1");
            g_dsText = DsfixSet(g_dsText, "dofBlurAmount", "1");
            g_dsText = DsfixSet(g_dsText, "unlockFPS", "0");
            g_dsText = DsfixSet(g_dsText, "FPSlimit", "30");
            g_dsText = DsfixSet(g_dsText, "FPSthreshold", "28");
            g_dsText = DsfixSet(g_dsText, "enableBackups", "0");
            g_dsText = DsfixSet(g_dsText, "backupInterval", "1500");
            g_dsText = DsfixSet(g_dsText, "maxBackups", "10");
            g_dsText = DsfixSet(g_dsText, "forceWindowed", "0");
            g_dsText = DsfixSet(g_dsText, "enableVsync", "0");
            g_dsText = DsfixSet(g_dsText, "fullscreenHz", "60");
            g_dsText = DsfixSet(g_dsText, "dinput8dllWrapper", "none");
            for (int i = 1; i < 10; ++i)
                g_dsText = DsfixSet(g_dsText, ("dinputChain" + std::to_string(i)).c_str(), "none");
            // Keep the change-detection snapshots in step with the reverted checkbox states so a
            // basic-mode Save doesn't see a phantom toggle.
            g_defDofInit = CtlChecked(U.defDof);
            g_fpsStabInit = CtlChecked(U.fpsStab);
            SetAaOptIn(false);               // defaults are AA off
            LayoutDsfix(hWnd);               // collapse now-"none" chain dropdowns
            return 0;
        }
        if (id == IDC_DS_SAVE) {
            std::string t = g_dsText;
            // Preserve the user's existing dsfix.ini: only write a key when the chosen value
            // actually differs from what was loaded, so untouched settings stay exactly as-is.
            auto put = [&](const char* k, const char* def, const std::string& v) {
                if (v != DsfixGet(g_dsText, k, def)) t = DsfixSet(t, k, v);
            };
            auto chk = [&](HWND h) { return std::string(CtlChecked(h) ? "1" : "0"); };

            put("renderWidth", "1920", CtlText(U.rw));   put("renderHeight", "1080", CtlText(U.rh));
            put("presentWidth", "1920", CtlText(U.pw));  put("presentHeight", "1080", CtlText(U.ph));
            // Antialiasing is Advanced-only. In basic mode AA isn't shown and we don't touch it
            // or the opt-in flag (so a power user's prior opt-in survives a basic-mode save); the
            // launch-time force-off handles it. Only when the user is in Advanced do we write their
            // AA choice and record whether they've opted in, which the force-off then respects.
            if (g_advanced) {
                std::string aa = CtlCombo(U.aaType);
                put("aaType", "SMAA", aa);
                put("aaQuality", "4", aa == "none" ? std::string("0") : MappedValue(U.aaQual, AAQ_VAL));
                SetAaOptIn(aa != "none");
            }
            put("ssaoStrength", "0", MappedValue(U.ssao, SSAO_VAL));
            put("filteringOverride", "0", MappedValue(U.filter, FILT_VAL));
            put("borderlessFullscreen", "0", chk(U.border));
            put("captureCursor", "1", chk(U.capCur));
            put("disableCursor", "1", chk(U.disCur));
            put("skipIntro", "1", chk(U.skipIntro));
            put("logLevel", "6", MappedValue(U.logLvl, LOG_VAL));

            put("enableHudMod", "0", g_hudEn);
            put("enableMinimalHud", "0", g_hudMin);
            put("hudScaleFactor", "1.0", g_hudScale);
            put("hudOpacity", "1.0", g_hudOpacity);

            // Depth of Field
            if (g_advanced) {
                put("dofOverrideResolution", "540", CtlText(U.dofRes));
                put("disableDofScaling", "1", chk(U.disDofScale));
                put("dofBlurAmount", "1", CtlText(U.dofBlur));
            } else if (CtlChecked(U.defDof) != g_defDofInit) {   // basic: only if user toggled it
                bool orig = CtlChecked(U.defDof);                // "Original DoF" => vanilla DoF (all zero)
                t = DsfixSet(t, "dofOverrideResolution", orig ? "0" : "540");
                t = DsfixSet(t, "disableDofScaling",     orig ? "0" : "1");
                t = DsfixSet(t, "dofBlurAmount",         orig ? "0" : "1");
            }

            // Framerate
            if (g_advanced) {
                put("unlockFPS", "1", chk(U.unlockFps));
                put("FPSlimit", "30", CtlText(U.fpsLimit));
                put("FPSthreshold", "28", CtlText(U.fpsThresh));
            } else if (CtlChecked(U.fpsStab) != g_fpsStabInit) { // basic: only if user toggled it
                bool stab = CtlChecked(U.fpsStab);
                t = DsfixSet(t, "unlockFPS", stab ? "1" : "0");
                t = DsfixSet(t, "FPSlimit", "30");
            }

            // Advanced-only sections.
            if (g_advanced) {
                put("dinput8dllWrapper", "none", CtlCombo(U.dinput[0]));
                for (int i = 1; i < 10; ++i)
                    put(("dinputChain" + std::to_string(i)).c_str(), "none", CtlCombo(U.dinput[i]));
                put("enableBackups", "1", chk(U.enBackup));
                put("backupInterval", "10", CtlText(U.bkpInt));
                put("maxBackups", "10", CtlText(U.bkpMax));
                put("forceWindowed", "0", chk(U.forceWin));
                put("enableVsync", "0", chk(U.enVsync));
                put("fullscreenHz", "60", CtlText(U.fsHz));
            }

            WriteFileUtf8(DsfixIniPath(), t);
            DestroyWindow(hWnd);
            return 0;
        }
        if (id == IDC_DS_CANCEL) { DestroyWindow(hWnd); return 0; }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY:
        // Re-enable the owner. The nested loop in OpenDsfixConfig breaks on !IsWindow(dlg)
        // right after this dispatch returns -- do NOT PostQuitMessage here, or the stray
        // WM_QUIT would also tear down the main window's message loop.
        if (HWND owner = GetWindow(hWnd, GW_OWNER)) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void OpenDsfixConfig(HWND owner) {
    if (!FileExists(DsfixIniPath())) {
        MessageBoxW(owner,
            L"dsfix.ini was not found next to the launcher.\n"
            L"Install DSFix into this folder to configure it.",
            L"DSFix Config", MB_OK | MB_ICONWARNING);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = DsfixWndProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"mctdeDsfixConfig";
        RegisterClassW(&wc);
        registered = true;
    }
    RECT rc; GetWindowRect(owner, &rc);
    int x = rc.left + 40, y = rc.top + 10;
    // height is set by LayoutDsfix in WM_CREATE; start with a placeholder.
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"mctdeDsfixConfig", L"DSFix Config",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, DS_CLIENTW, 480, owner, nullptr, g_inst, nullptr);
    EnableWindow(owner, FALSE);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(dlg)) break;
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

// ============================================================ mctde Settings dialog
// A themed, scrolling panel that round-trips mctde-link.ini. Curated settings show up
// front; raw/diagnostic ones live behind an "Advanced Options" toggle. The Overlay group
// gets bespoke widgets (font dropdown, padding sliders) and a live preview that mirrors
// placement + font + scale + the show-toggles.
static const int MC_CLIENTW = 500;

// ---- narrow/wide helpers (ini keys/values are ASCII) ----
static std::wstring WideOf(const std::string& s) { return std::wstring(s.begin(), s.end()); }
static std::string  NarrowOf(const std::wstring& s) { return std::string(s.begin(), s.end()); }
static std::wstring MlPath() { return PathIn(L"mctde-link.ini"); }
static std::wstring MlGetW(const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[512] = {0};
    GetPrivateProfileStringW(sec, key, def, buf, 512, MlPath().c_str());
    return buf;
}
static int MlGetInt(const wchar_t* sec, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(sec, key, def, MlPath().c_str());
}
static void MlSet(const wchar_t* sec, const wchar_t* key, const std::wstring& val) {
    WritePrivateProfileStringW(sec, key, val.c_str(), MlPath().c_str());
}
static std::wstring CtlTextW(HWND h)  { wchar_t b[256] = {0}; GetWindowTextW(h, b, 256); return b; }
static std::wstring CtlComboW(HWND h) {
    int s = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    if (s < 0) return CtlTextW(h);
    wchar_t b[128] = {0}; SendMessageW(h, CB_GETLBTEXT, s, (LPARAM)b); return b;
}

// ---- installed-font enumeration (sorted, de-duped, no @vertical faces) ----
static int CALLBACK FontEnumCb(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM lp) {
    auto* v = reinterpret_cast<std::vector<std::wstring>*>(lp);
    if (lf->lfFaceName[0] == L'@') return 1;
    std::wstring n = lf->lfFaceName;
    for (auto& e : *v) if (_wcsicmp(e.c_str(), n.c_str()) == 0) return 1;
    v->push_back(n);
    return 1;
}
static std::vector<std::wstring> EnumInstalledFonts() {
    std::vector<std::wstring> v;
    HDC dc = GetDC(nullptr);
    LOGFONTW lf = {0}; lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, FontEnumCb, (LPARAM)&v, 0);
    ReleaseDC(nullptr, dc);
    std::sort(v.begin(), v.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return v;
}

// ---- data-driven settings table (everything except the special Overlay widgets) ----
// kind: 0 header, 1 checkbox, 2 int edit, 3 text edit, 4 combo
struct McDef { int kind; const wchar_t* label; const wchar_t* sec; const wchar_t* key;
               const wchar_t* def; bool adv; const wchar_t** items; int nItems; };
static const wchar_t* IT_MODE[]    = { L"Ask", L"On", L"Off" };
static const wchar_t* IT_BACKEND[] = { L"d3d", L"gdi" };
static McDef g_def[] = {
    {0,L"General",                       0,0,0,false,0,0},
    {1,L"Hide soul counter (entire bottom-right)", L"HideSoulCounter",L"Enabled",L"0",false,0,0},
    {1,L"Enable overlay logging",        L"Settings",L"EnableLogging",L"0",false,0,0},
    {1,L"Quit game cleanly on window close", L"Settings",L"ExitWithGame",L"1",false,0,0},
    {1,L"Require launcher to start the game", L"Launcher",L"RequireLauncher",L"1",false,0,0},
    {0,L"Multiplayer overlay",            0,0,0,false,0,0},
    {1,L"Show HP readout",               L"HP",L"Enabled",L"1",false,0,0},
    {1,L"Show ping (TruePing)",          L"TruePing",L"Enabled",L"1",false,0,0},
    {0,L"Overlay rendering",              0,0,0,false,0,0},
    {4,L"Overlay backend",               L"Render",L"Backend",L"d3d",false,IT_BACKEND,2},
    {2,L"Overlay refresh (ms)",          L"Render",L"SubmitMs",L"66",false,0,0},
    // -------- advanced --------
    {0,L"Overlay (advanced)",        0,0,0,true,0,0},
    {2,L"Line height",                   L"Overlay",L"LineHeight",L"20",true,0,0},
    {2,L"Overlay data poll (ms)",        L"Overlay",L"RefreshMs",L"1000",true,0,0},
    {1,L"Hide your own row",             L"Overlay",L"HideLocal",L"0",true,0,0},
    {1,L"Show local marker",             L"Overlay",L"ShowLocalMarker",L"1",true,0,0},
    {1,L"Show disconnected rows",        L"Overlay",L"ShowDisconnected",L"1",true,0,0},
    {3,L"Toggle modifier (VK hex)",      L"Overlay",L"ToggleModifier",L"0x10",true,0,0},
    {3,L"Toggle key (VK hex)",           L"Overlay",L"ToggleKey",L"0x72",true,0,0},
    {0,L"HP readout (advanced)",     0,0,0,true,0,0},
    {2,L"Current-HP offset",             L"HP",L"CurrentOffset",L"724",true,0,0},
    {2,L"Max-HP offset",                 L"HP",L"MaxOffset",L"728",true,0,0},
    {2,L"1-HP linger (ms)",              L"HP",L"OneHpLingerMs",L"500",true,0,0},
    {2,L"HP poll (ms)",                  L"HP",L"PollMs",L"33",true,0,0},
    {0,L"Ping / TruePing (advanced)",0,0,0,true,0,0},
    {2,L"Ping display mode",             L"TruePing",L"DisplayMode",L"2",true,0,0},
    {1,L"Prefer overlay ping source",    L"TruePing",L"PreferOverlay",L"1",true,0,0},
    {2,L"Ping channel",                  L"TruePing",L"Channel",L"63",true,0,0},
    {2,L"Ping send interval (ms)",       L"TruePing",L"SendMs",L"1000",true,0,0},
    {2,L"Ping stale timeout (ms)",       L"TruePing",L"StaleMs",L"4000",true,0,0},
    {0,L"WebSocket feed (advanced)", 0,0,0,true,0,0},
    {1,L"Enable WebSocket feed",         L"WebSocket",L"Enabled",L"0",true,0,0},
    {2,L"WebSocket port",                L"WebSocket",L"Port",L"39876",true,0,0},
    {2,L"WebSocket send (ms)",           L"WebSocket",L"SendMs",L"33",true,0,0},
    {0,L"Debug (advanced)",          0,0,0,true,0,0},
    {1,L"Dump overlay data to log",      L"Debug",L"DumpOverlayData",L"0",true,0,0},
    {1,L"Debug P2P bridge",              L"Debug",L"DebugP2PBridge",L"0",true,0,0},
    {0,L"Controller (advanced)",     0,0,0,true,0,0},
    {1,L"First-launch binding nudge",    L"Controller",L"BindingNudge",L"1",true,0,0},
    {1,L"Force binding nudge (testing)", L"Controller",L"BindingNudgeForce",L"0",true,0,0},
    {3,L"Manual nudge key (VK hex)",     L"Controller",L"BindingNudgeKey",L"0",true,0,0},
    {0,L"Compatibility (advanced)",  0,0,0,true,0,0},
    {3,L"Chainload folder",              L"Compatibility",L"ChainloadFolder",L"mctde-Link_Chainload",true,0,0},
};
static const int MC_NDEF = (int)(sizeof(g_def) / sizeof(g_def[0]));

struct McUi {
    HWND view, adv, save, cancel;
    HWND hdrOv, lblFont, font, lblSize, size, lblHpSize, hpSize, lblCorner, corner, lblPadX, padX, lblPadY, padY;
    HWND showHeader, showHp, hpBars, showPing, showName, forceTop, preview;
    HWND ctl[64], lbl[64];      // data-driven table (>= MC_NDEF)
};
static McUi M;
static int  g_mcScroll = 0;
static bool g_mcAdv = false;

// live preview state, refreshed from the controls by McSync()
struct McPv { std::wstring font; int size, hpSize, corner, padX, padY; bool hdr, hp, bars, ping, name; };
static McPv g_pv = { L"Tahoma", 24, 48, 0, 5, 5, true, true, false, true, true };

static bool McIsHeader(HWND h) {
    for (int i = 0; i < MC_NDEF; ++i)
        if (g_def[i].kind == 0 && M.ctl[i] == h) return true;
    return false;
}

static void McSync() {
    g_pv.font   = CtlComboW(M.font);
    g_pv.size   = max(6, _wtoi(CtlTextW(M.size).c_str()));
    g_pv.hpSize = max(6, (int)SendMessageW(M.hpSize, TBM_GETPOS, 0, 0));
    g_pv.corner = max(0, (int)SendMessageW(M.corner, CB_GETCURSEL, 0, 0));
    g_pv.padX   = (int)SendMessageW(M.padX, TBM_GETPOS, 0, 0);
    g_pv.padY   = (int)SendMessageW(M.padY, TBM_GETPOS, 0, 0);
    g_pv.hdr    = CtlChecked(M.showHeader);
    g_pv.hp     = CtlChecked(M.showHp);
    g_pv.bars   = CtlChecked(M.hpBars);
    g_pv.ping   = CtlChecked(M.showPing);
    g_pv.name   = CtlChecked(M.showName);
    if (M.preview) InvalidateRect(M.preview, nullptr, TRUE);
}

// ---- the live overlay preview window ----
static LRESULT CALLBACK McPreviewProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int W = rc.right, H = rc.bottom;
        HBRUSH bg = CreateSolidBrush(ThField()); FillRect(dc, &rc, bg); DeleteObject(bg);
        // a 16:9 "screen" centred inside the control
        int sw = W - 4, sh = (int)(sw * 9.0 / 16.0);
        if (sh > H - 4) { sh = H - 4; sw = (int)(sh * 16.0 / 9.0); }
        int sx = (W - sw) / 2, sy = (H - sh) / 2;
        RECT scr = { sx, sy, sx + sw, sy + sh };
        HBRUSH sb = CreateSolidBrush(RGB(44, 48, 56)); FillRect(dc, &scr, sb); DeleteObject(sb);
        HPEN pen = CreatePen(PS_SOLID, 1, ThBorder());
        HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, scr.left, scr.top, scr.right, scr.bottom);
        SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen);

        double scale = (double)sw / 1920.0;
        int fpx = max(6, (int)(g_pv.size * scale + 0.5));
        int hpFpx = max(6, (int)(g_pv.hpSize * scale + 0.5));
        HFONT f = CreateFontW(-fpx, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, g_pv.font.c_str());
        HFONT hf = CreateFontW(-hpFpx, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, g_pv.font.c_str());
        HFONT of = (HFONT)SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        int lh = max((int)(fpx * 1.3), (int)(hpFpx * 1.3)) + 1;

        struct Row { const wchar_t* ping; const wchar_t* name; };
        static const Row rows[] = { { L"42ms", L"Solaire" }, { L"88ms", L"Lautrec" }, { L"120ms", L"Siegmeyer" } };
        int nRows = (int)(sizeof(rows) / sizeof(rows[0]));
        const wchar_t* hpSample = L"1650HP";
        int hpW = 0, hpGap = 0;
        if (g_pv.hp) {
            if (g_pv.bars) hpW = (int)(hpFpx * 5.2);          // bars are 2x wide
            else { SelectObject(dc, hf); SIZE z; GetTextExtentPoint32W(dc, hpSample, 6, &z); SelectObject(dc, f); hpW = z.cx; }
            hpGap = max(3, fpx / 3);
        }

        // measure the widest row + header
        int blockW = 0;
        if (g_pv.hdr) { SIZE z; GetTextExtentPoint32W(dc, L"mctde", 5, &z); blockW = max(blockW, (int)z.cx); }
        for (int i = 0; i < nRows; ++i) {
            int rw = hpW + hpGap;
            if (g_pv.ping) { SIZE z; GetTextExtentPoint32W(dc, rows[i].ping, (int)wcslen(rows[i].ping), &z); rw += z.cx + max(3, fpx / 3); }
            if (g_pv.name) { SIZE z; GetTextExtentPoint32W(dc, rows[i].name, (int)wcslen(rows[i].name), &z); rw += z.cx; }
            blockW = max(blockW, rw);
        }
        int blockH = lh * (nRows + (g_pv.hdr ? 1 : 0));
        if (blockW < fpx) blockW = fpx;

        int px = (int)(g_pv.padX * scale + 0.5), py = (int)(g_pv.padY * scale + 0.5);
        bool right = (g_pv.corner == 1 || g_pv.corner == 3);
        bool bottom = (g_pv.corner == 2 || g_pv.corner == 3);
        int bx = right ? (scr.right - px - blockW) : (scr.left + px);
        int by = bottom ? (scr.bottom - py - blockH) : (scr.top + py);
        bx = max(scr.left + 2, min(bx, scr.right - blockW - 2));
        by = max(scr.top + 2, min(by, scr.bottom - blockH - 2));

        bool rightAlign = (g_pv.corner == 1 || g_pv.corner == 3);
        int blockRight = bx + blockW;     // right edge of the content block
        int ty = by;
        if (g_pv.hdr) {
            SetTextColor(dc, ThAccent());
            SIZE hz; GetTextExtentPoint32W(dc, L"mctde", 5, &hz);
            TextOutW(dc, rightAlign ? (blockRight - hz.cx) : bx, ty, L"mctde", 5);
            ty += lh;
        }
        for (int i = 0; i < nRows; ++i) {
            int nameW = 0, pingW = 0;
            if (g_pv.name) { SIZE z; GetTextExtentPoint32W(dc, rows[i].name, (int)wcslen(rows[i].name), &z); nameW = z.cx; }
            if (g_pv.ping) { SIZE z; GetTextExtentPoint32W(dc, rows[i].ping, (int)wcslen(rows[i].ping), &z); pingW = z.cx; }
            int gap = max(3, fpx / 3);

            auto drawHP = [&](int xx) {
                if (g_pv.bars) {
                    int barH = max(3, (int)(hpFpx * 0.7)); int barY = ty + (lh - barH) / 2;
                    RECT br = { xx, barY, xx + hpW, barY + barH };
                    HBRUSH hb = CreateSolidBrush(RGB(58, 16, 16)); FillRect(dc, &br, hb); DeleteObject(hb);
                    int curW = (hpW * (3 - i)) / 3, ghostW = (hpW * 3) / 4;
                    if (i == 1) { RECT yr = { xx + curW, barY, xx + ghostW, barY + barH }; HBRUSH yb = CreateSolidBrush(RGB(230, 205, 55)); FillRect(dc, &yr, yb); DeleteObject(yb); }
                    RECT bf = { xx, barY, xx + curW, barY + barH }; HBRUSH gb = CreateSolidBrush(RGB(95, 175, 75)); FillRect(dc, &bf, gb); DeleteObject(gb);
                } else {
                    SelectObject(dc, hf); SetTextColor(dc, RGB(220, 220, 225));
                    TextOutW(dc, xx, ty + (lh - hpFpx) / 2, hpSample, 6); SelectObject(dc, f);
                }
            };
            auto drawPing = [&](int xx) { SetTextColor(dc, RGB(170, 200, 235)); TextOutW(dc, xx, ty, rows[i].ping, (int)wcslen(rows[i].ping)); };
            auto drawName = [&](int xx) { SetTextColor(dc, RGB(232, 232, 236)); TextOutW(dc, xx, ty, rows[i].name, (int)wcslen(rows[i].name)); };

            // right-justify the row against the block's right edge on right-side corners
            int nEl = (g_pv.hp ? 1 : 0) + (g_pv.ping ? 1 : 0) + (g_pv.name ? 1 : 0);
            int rowW = (g_pv.hp ? hpW : 0) + (g_pv.ping ? pingW : 0) + (g_pv.name ? nameW : 0) + (nEl > 1 ? gap * (nEl - 1) : 0);
            int cx = rightAlign ? (blockRight - rowW) : bx;
            if (!rightAlign) {
                if (g_pv.hp)   { drawHP(cx);   cx += hpW + gap; }
                if (g_pv.ping) { drawPing(cx); cx += pingW + gap; }
                if (g_pv.name) { drawName(cx); }
            } else {
                if (g_pv.name) { drawName(cx); cx += nameW + gap; }
                if (g_pv.ping) { drawPing(cx); cx += pingW + gap; }
                if (g_pv.hp)   { drawHP(cx); }
            }
            ty += lh;
        }
        SelectObject(dc, of); DeleteObject(f); DeleteObject(hf);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static HWND McSlider(HWND parent, int id, int minv, int maxv, int val) {
    HWND h = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0, 0, 150, 26,
        parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(h, TBM_SETRANGE, TRUE, MAKELONG(minv, maxv));
    SendMessageW(h, TBM_SETPOS, TRUE, val);
    return h;
}

// Owner-drawn font picker: each entry (and the selected field) is rendered in its own face,
// so the dropdown is itself a preview of how the overlay text will look.
static HWND McFontCombo(HWND parent, int id, int w, const std::vector<std::wstring>& items,
                        const std::wstring& cur) {
    HWND h = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        0, 0, w, 320, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    int sel = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)items[i].c_str());
        if (_wcsicmp(items[i].c_str(), cur.c_str()) == 0) sel = (int)i;
    }
    SendMessageW(h, CB_SETCURSEL, sel, 0);
    return h;
}
// Draw one owner-drawn font-combo item in that font's own typeface.
static void McDrawFontItem(LPDRAWITEMSTRUCT di) {
    if ((int)di->itemID < 0) return;
    wchar_t name[128] = {0};
    SendMessageW(di->hwndItem, CB_GETLBTEXT, di->itemID, (LPARAM)name);
    bool sel = (di->itemState & ODS_SELECTED) != 0;
    HBRUSH b = CreateSolidBrush(sel ? ThAccent() : ThField());
    FillRect(di->hDC, &di->rcItem, b); DeleteObject(b);
    HFONT f = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, name);
    HFONT of = (HFONT)SelectObject(di->hDC, f);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, sel ? RGB(20, 20, 20) : ThText());
    RECT tr = di->rcItem; tr.left += 5;
    DrawTextW(di->hDC, name, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(di->hDC, of); DeleteObject(f);
}

// lay out every control at (content-Y - scroll); advance Y only for visible rows.
// returns total content height.
static int McLayout() {
    RECT vr; GetClientRect(M.view, &vr);
    const int CW = max(300, (int)vr.right);   // viewport client width (excludes the scrollbar)
    const int LX = 14, FX = 116, ROW = 26, HDRH = 24, PVX = 264, PVH = 168;
    const int PVW = max(150, CW - PVX - 10);
    int y = 8;
    int s = g_mcScroll;
    auto P = [&](HWND h, int x, int yy, int w, int ht) { SetWindowPos(h, nullptr, x, yy - s, w, ht, SWP_NOZORDER); };

    // ---- Overlay group (special widgets) + live preview on the right ----
    SetWindowPos(M.hdrOv, nullptr, LX, y - s, 240, 18, SWP_NOZORDER); y += HDRH;
    int ovTop = y;
    P(M.lblFont,   LX, y + 3, 96, 18); P(M.font,   LX + 60, y, 188, 200); y += ROW;
    P(M.lblSize,   LX, y + 3, 96, 18); P(M.size,   LX + 60, y, 56, 22);   y += ROW;
    P(M.lblHpSize, LX, y + 3, 56, 18); P(M.hpSize, LX + 60, y, 188, 26);  y += ROW;
    P(M.lblCorner, LX, y + 3, 96, 18); P(M.corner, LX + 60, y, 130, 200); y += ROW;
    P(M.lblPadX,   LX, y + 3, 56, 18); P(M.padX,   LX + 60, y, 188, 26);  y += ROW;
    P(M.lblPadY,   LX, y + 3, 56, 18); P(M.padY,   LX + 60, y, 188, 26);  y += ROW;
    P(M.showHeader, LX, y, 240, 22); y += 23;
    P(M.showHp,     LX, y, 240, 22); y += 23;
    P(M.hpBars,     LX + 18, y, 240, 22); y += 23;   // sub-option of Show HP
    P(M.showPing,   LX, y, 240, 22); y += 23;
    P(M.showName,   LX, y, 240, 22); y += 23;
    P(M.forceTop,   LX, y, 240, 22); y += 23;
    // preview pinned at the top-right of the overlay group
    SetWindowPos(M.preview, nullptr, PVX, ovTop - s, PVW, PVH, SWP_NOZORDER);
    if (y < ovTop + PVH + 4) y = ovTop + PVH + 4;
    y += 6;

    // ---- data-driven table ----
    for (int i = 0; i < MC_NDEF; ++i) {
        McDef& d = g_def[i];
        bool vis = !d.adv || g_mcAdv;
        if (!vis) { ShowWindow(M.ctl[i], SW_HIDE); if (M.lbl[i]) ShowWindow(M.lbl[i], SW_HIDE); continue; }
        ShowWindow(M.ctl[i], SW_SHOW); if (M.lbl[i]) ShowWindow(M.lbl[i], SW_SHOW);
        if (d.kind == 0) {                       // section header
            P(M.ctl[i], LX, y + 4, CW - 2 * LX, 18); y += HDRH + 2;
        } else if (d.kind == 1) {                // checkbox
            P(M.ctl[i], LX, y, CW - 2 * LX, 22); y += 24;
        } else {                                 // labelled edit/combo
            P(M.lbl[i], LX, y + 3, FX - LX, 18);
            if (d.kind == 4) P(M.ctl[i], FX, y, 150, 200);
            else             P(M.ctl[i], FX, y, 90, 22);
            y += ROW;
        }
    }
    return y + 6;
}

static void McRelayout() {
    RECT vr; GetClientRect(M.view, &vr);
    int viewH = vr.bottom;
    int contentH = McLayout();
    int maxS = max(0, contentH - viewH);
    if (g_mcScroll > maxS) { g_mcScroll = maxS; McLayout(); }
    SCROLLINFO si = { sizeof(si) }; si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0; si.nMax = contentH; si.nPage = viewH; si.nPos = g_mcScroll;
    SetScrollInfo(M.view, SB_VERT, &si, TRUE);
    RedrawWindow(M.view, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

// Scroll by shifting the already-laid-out children with ScrollWindowEx -- it blits the
// retained area and invalidates only the newly exposed strip, so there are no repaint trails.
static void McScrollTo(int pos) {
    SCROLLINFO si = { sizeof(si) }; si.fMask = SIF_RANGE | SIF_PAGE; GetScrollInfo(M.view, SB_VERT, &si);
    int maxS = max(0, (int)si.nMax - (int)si.nPage);
    pos = max(0, min(pos, maxS));
    int dy = pos - g_mcScroll;
    if (dy == 0) return;
    g_mcScroll = pos;
    SetScrollPos(M.view, SB_VERT, pos, TRUE);
    ScrollWindowEx(M.view, 0, -dy, nullptr, nullptr, nullptr, nullptr,
                   SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    UpdateWindow(M.view);
}
static void McScrollBy(int dy) { McScrollTo(g_mcScroll + dy); }

// ---- the scrolling viewport: hosts every setting control + the preview ----
static LRESULT CALLBACK McViewProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT _tr = 0;
    if (msg == WM_CTLCOLORSTATIC && McIsHeader((HWND)lParam)) {   // accent section headers
        SetTextColor((HDC)wParam, ThAccent());
        SetBkColor((HDC)wParam, ThBg());
        SetBkMode((HDC)wParam, OPAQUE);
        return (LRESULT)(g_dark ? g_brBg : GetSysColorBrush(COLOR_BTNFACE));
    }
    if (msg == WM_MEASUREITEM) {                                   // taller rows for the font list
        LPMEASUREITEMSTRUCT mi = (LPMEASUREITEMSTRUCT)lParam;
        if (mi->CtlID == IDC_MC_FONT) { mi->itemHeight = 20; return TRUE; }
    }
    if (msg == WM_DRAWITEM) {                                      // font items, each in its own face
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lParam;
        if (di->CtlType == ODT_COMBOBOX && di->CtlID == IDC_MC_FONT) { McDrawFontItem(di); return TRUE; }
    }
    if (ThemeHandle(hWnd, msg, wParam, lParam, &_tr)) return _tr;
    switch (msg) {
    case WM_COMMAND:   McSync(); return 0;                         // any change -> refresh preview
    case WM_HSCROLL:   McSync(); return 0;                         // padding sliders
    case WM_MOUSEWHEEL: McScrollBy(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -48 : 48); return 0;
    case WM_VSCROLL: {
        SCROLLINFO si = { sizeof(si) }; si.fMask = SIF_ALL; GetScrollInfo(hWnd, SB_VERT, &si);
        int pos = si.nPos, page = si.nPage;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: pos -= 24; break; case SB_LINEDOWN: pos += 24; break;
        case SB_PAGEUP: pos -= page; break; case SB_PAGEDOWN: pos += page; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        }
        McScrollTo(pos);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void McSave() {
    for (int i = 0; i < MC_NDEF; ++i) {
        McDef& d = g_def[i];
        if (d.kind == 0) continue;
        std::wstring val;
        if (d.kind == 1)      val = CtlChecked(M.ctl[i]) ? L"1" : L"0";
        else if (d.kind == 4) val = CtlComboW(M.ctl[i]);
        else                  val = CtlTextW(M.ctl[i]);
        MlSet(d.sec, d.key, val);
        // Hide-soul-counter checkbox drives its companion keys (the actual hide is HideBox=1).
        if (!wcscmp(d.sec, L"HideSoulCounter") && !wcscmp(d.key, L"Enabled")) {
            bool on = (val == L"1");
            MlSet(L"HideSoulCounter", L"VerifyOnly",    on ? L"0" : L"1");
            MlSet(L"HideSoulCounter", L"HideGainPopup", L"1");
            MlSet(L"HideSoulCounter", L"HideBox",       on ? L"1" : L"0");
        }
    }
    // overlay specials
    MlSet(L"Overlay", L"FontFace",  CtlComboW(M.font));
    MlSet(L"Overlay", L"FontHeight",CtlTextW(M.size));
    MlSet(L"Overlay", L"HpFontHeight", std::to_wstring((int)SendMessageW(M.hpSize, TBM_GETPOS, 0, 0)));
    MlSet(L"Overlay", L"Corner",    CtlComboW(M.corner));
    MlSet(L"Overlay", L"PaddingX",  std::to_wstring((int)SendMessageW(M.padX, TBM_GETPOS, 0, 0)));
    MlSet(L"Overlay", L"PaddingY",  std::to_wstring((int)SendMessageW(M.padY, TBM_GETPOS, 0, 0)));
    MlSet(L"Overlay", L"ShowHeader",CtlChecked(M.showHeader) ? L"1" : L"0");
    MlSet(L"Overlay", L"ShowHp",    CtlChecked(M.showHp)     ? L"1" : L"0");
    MlSet(L"Overlay", L"HpBars",    CtlChecked(M.hpBars)     ? L"1" : L"0");
    MlSet(L"Overlay", L"ShowPing",  CtlChecked(M.showPing)   ? L"1" : L"0");
    MlSet(L"Overlay", L"ShowName",  CtlChecked(M.showName)   ? L"1" : L"0");
    MlSet(L"Overlay", L"ForceTopmost", CtlChecked(M.forceTop) ? L"1" : L"0");
}

static LRESULT CALLBACK McDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT _tr = 0;
    if (ThemeHandle(hWnd, msg, wParam, lParam, &_tr)) return _tr;
    switch (msg) {
    case WM_CREATE: {
        g_mcScroll = 0; g_mcAdv = false;
        RECT cr; GetClientRect(hWnd, &cr);
        int bottom = cr.bottom, right = cr.right;
        M.adv = DsMakeCheck(hWnd, IDC_MC_ADV, L"Advanced Options", false);
        SetWindowPos(M.adv, nullptr, 14, 9, 200, 22, SWP_NOZORDER);
        M.save   = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
                                 right - 164, bottom - 38, 70, 28, hWnd, (HMENU)IDC_MC_SAVE, g_inst, nullptr);
        M.cancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
                                 right - 86, bottom - 38, 70, 28, hWnd, (HMENU)IDC_MC_CANCEL, g_inst, nullptr);
        SendMessageW(M.save, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        SendMessageW(M.cancel, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // scrolling viewport between the top toggle and the bottom button bar
        M.view = CreateWindowExW(0, L"mctdeSetView", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            0, 38, right, bottom - 38 - 46, hWnd, nullptr, g_inst, nullptr);

        // --- special Overlay widgets (children of the viewport) ---
        M.hdrOv     = DsMakeLabel(M.view, L"Overlay", 0, 0, 240);
        M.lblFont   = DsMakeLabel(M.view, L"Font", 0, 0, 96);
        std::vector<std::wstring> fonts = EnumInstalledFonts();
        M.font      = McFontCombo(M.view, IDC_MC_FONT, 188, fonts, MlGetW(L"Overlay", L"FontFace", L"Tahoma"));
        M.lblSize   = DsMakeLabel(M.view, L"Font size", 0, 0, 96);
        M.size      = DsMakeEdit(M.view, IDC_MC_SIZE, NarrowOf(MlGetW(L"Overlay", L"FontHeight", L"24")), 0, 0, 56);
        M.lblHpSize = DsMakeLabel(M.view, L"HP size", 0, 0, 96);
        M.hpSize    = McSlider(M.view, IDC_MC_HPSIZE, 12, 80, MlGetInt(L"Overlay", L"HpFontHeight", 48));
        M.lblCorner = DsMakeLabel(M.view, L"Screen corner", 0, 0, 96);
        M.corner    = DsMakeCombo(M.view, IDC_MC_CORNER, 0, 0, 130,
                                  { L"top_left", L"top_right", L"bottom_left", L"bottom_right" },
                                  NarrowOf(MlGetW(L"Overlay", L"Corner", L"top_left")));
        M.lblPadX   = DsMakeLabel(M.view, L"Padding X", 0, 0, 56);
        M.padX      = McSlider(M.view, IDC_MC_PADX, 0, 400, MlGetInt(L"Overlay", L"PaddingX", 5));
        M.lblPadY   = DsMakeLabel(M.view, L"Padding Y", 0, 0, 56);
        M.padY      = McSlider(M.view, IDC_MC_PADY, 0, 400, MlGetInt(L"Overlay", L"PaddingY", 5));
        M.showHeader= DsMakeCheck(M.view, IDC_MC_SHOWHDR,  L"Show \"mctde\" header", MlGetInt(L"Overlay", L"ShowHeader", 1) != 0);
        M.showHp    = DsMakeCheck(M.view, IDC_MC_SHOWHP,   L"Show HP", MlGetInt(L"Overlay", L"ShowHp", 1) != 0);
        M.hpBars    = DsMakeCheck(M.view, IDC_MC_HPBARS,   L"Render HP as a bar (not a number)", MlGetInt(L"Overlay", L"HpBars", 0) != 0);
        M.showPing  = DsMakeCheck(M.view, IDC_MC_SHOWPING, L"Show ping", MlGetInt(L"Overlay", L"ShowPing", 1) != 0);
        M.showName  = DsMakeCheck(M.view, IDC_MC_SHOWNAME, L"Show names", MlGetInt(L"Overlay", L"ShowName", 1) != 0);
        M.forceTop  = DsMakeCheck(M.view, IDC_MC_FORCETOP, L"Force overlay always-on-top", MlGetInt(L"Overlay", L"ForceTopmost", 0) != 0);
        M.preview   = CreateWindowExW(0, L"mctdePreview", L"", WS_CHILD | WS_VISIBLE,
                                      0, 0, 200, 168, M.view, (HMENU)IDC_MC_PREVIEW, g_inst, nullptr);

        // --- data-driven table controls ---
        for (int i = 0; i < MC_NDEF; ++i) {
            McDef& d = g_def[i]; int id = IDC_MC_DYN + i;
            M.lbl[i] = nullptr; M.ctl[i] = nullptr;
            if (d.kind == 0) {
                M.ctl[i] = DsMakeLabel(M.view, d.label, 0, 0, MC_CLIENTW - 28);
            } else if (d.kind == 1) {
                M.ctl[i] = DsMakeCheck(M.view, id, d.label, MlGetInt(d.sec, d.key, _wtoi(d.def)) != 0);
            } else if (d.kind == 4) {
                M.lbl[i] = DsMakeLabel(M.view, d.label, 0, 0, 150);
                std::vector<std::wstring> it; for (int k = 0; k < d.nItems; ++k) it.push_back(d.items[k]);
                M.ctl[i] = DsMakeCombo(M.view, id, 0, 0, 150, it, NarrowOf(MlGetW(d.sec, d.key, d.def)));
            } else {
                M.lbl[i] = DsMakeLabel(M.view, d.label, 0, 0, 150);
                M.ctl[i] = DsMakeEdit(M.view, id, NarrowOf(MlGetW(d.sec, d.key, d.def)), 0, 0, 90);
            }
        }
        McSync();
        McRelayout();
        return 0;
    }
    case WM_MOUSEWHEEL: McScrollBy(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -48 : 48); return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_MC_ADV:    g_mcAdv = CtlChecked(M.adv); g_mcScroll = 0; McRelayout(); return 0;
        case IDC_MC_SAVE:   McSave(); DestroyWindow(hWnd); return 0;
        case IDC_MC_CANCEL: DestroyWindow(hWnd); return 0;
        }
        return 0;
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY:
        if (HWND owner = GetWindow(hWnd, GW_OWNER)) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void OpenMctdeSettings(HWND owner) {
    if (!FileExists(MlPath())) {
        MessageBoxW(owner, L"mctde-link.ini was not found next to the launcher.",
                    L"mctde Settings", MB_OK | MB_ICONWARNING);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.hInstance = g_inst; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpfnWndProc = McDialogProc;  wc.lpszClassName = L"mctdeSettings";    RegisterClassW(&wc);
        wc.lpfnWndProc = McViewProc;    wc.lpszClassName = L"mctdeSetView";     RegisterClassW(&wc);
        wc.lpfnWndProc = McPreviewProc; wc.lpszClassName = L"mctdePreview";     RegisterClassW(&wc);
        registered = true;
    }
    RECT rc; GetWindowRect(owner, &rc);
    RECT wa = {0}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int H = min(660, wa.bottom - wa.top - 40);
    int x = rc.left + 30, yTop = rc.top + 10;
    if (yTop + H > wa.bottom) yTop = max((LONG)wa.top, wa.bottom - H);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"mctdeSettings", L"mctde Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, yTop, MC_CLIENTW, H, owner, nullptr, g_inst, nullptr);
    EnableWindow(owner, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (!IsWindow(dlg)) break;
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

// ------------------------------------------------------------ main window
// ------------------------------------------------------------ Changelog window
static std::string LoadTextResource(int id) {
    HRSRC r = FindResourceW(g_inst, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return "";
    HGLOBAL g = LoadResource(g_inst, r);
    if (!g) return "";
    const char* p = (const char*)LockResource(g);
    DWORD n = SizeofResource(g_inst, r);
    return p ? std::string(p, p + n) : std::string();
}
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// Combine the two changelogs (launcher + mctde-Link) into one CRLF-normalized wide string.
static std::wstring BuildChangelogText(const std::string& launcherMd, const std::string& linkMd) {
    std::string combined =
        "=== mctde-Launcher ===\n\n" + launcherMd +
        "\n\n\n=== mctde-Link ===\n\n" + linkMd;
    std::string norm; norm.reserve(combined.size() + 128);
    for (char c : combined) { if (c == '\r') continue; if (c == '\n') norm += "\r\n"; else norm += c; }
    return Utf8ToWide(norm);
}

// GET an HTTPS URL into a string with short timeouts. false on any failure (offline/timeout/non-200).
static bool HttpsGet(const wchar_t* host, const wchar_t* path, std::string& out) {
    out.clear();
    HINTERNET hS = WinHttpOpen(L"mctde-launcher/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return false;
    WinHttpSetTimeouts(hS, 3000, 3000, 4000, 4000);   // resolve/connect/send/receive
    bool ok = false;
    HINTERNET hC = WinHttpConnect(hS, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hC) {
        HINTERNET hR = WinHttpOpenRequest(hC, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (hR) {
            if (WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hR, NULL)) {
                DWORD status = 0, sz = sizeof(status);
                WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
                if (status == 200) {
                    DWORD avail = 0;
                    do {
                        if (!WinHttpQueryDataAvailable(hR, &avail)) break;
                        if (!avail) break;
                        std::vector<char> buf(avail);
                        DWORD got = 0;
                        if (WinHttpReadData(hR, buf.data(), avail, &got) && got > 0) out.append(buf.data(), got);
                        else break;
                    } while (avail > 0);
                    ok = !out.empty();
                }
            }
            WinHttpCloseHandle(hR);
        }
        WinHttpCloseHandle(hC);
    }
    WinHttpCloseHandle(hS);
    return ok;
}

// Fetch both live changelogs from GitHub (per-file fallback to the embedded copy), then hand
// the combined text back to the window via WM_CHANGELOG_READY. Runs off the UI thread so the
// dialog opens instantly and never blocks when offline.
static DWORD WINAPI ChangelogFetchThread(LPVOID param) {
    HWND wnd = (HWND)param;
    std::string launcherMd, linkMd;
    if (!HttpsGet(L"raw.githubusercontent.com", L"/McRoodyPoo/mctde-Launcher/main/CHANGELOG.md", launcherMd))
        launcherMd = LoadTextResource(IDR_CHANGELOG_LAUNCHER);
    if (!HttpsGet(L"raw.githubusercontent.com", L"/McRoodyPoo/mctde-Link/main/CHANGELOG.md", linkMd))
        linkMd = LoadTextResource(IDR_CHANGELOG_MCTDELINK);
    std::wstring text = BuildChangelogText(launcherMd, linkMd);
    size_t n = text.size() + 1;
    wchar_t* buf = new wchar_t[n];
    wmemcpy(buf, text.c_str(), n);
    if (!IsWindow(wnd) || !PostMessageW(wnd, WM_CHANGELOG_READY, 0, (LPARAM)buf))
        delete[] buf;   // window already closed -> drop it
    return 0;
}

static LRESULT CALLBACK ChangelogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT _tr = 0;
    if (ThemeHandle(hWnd, msg, wParam, lParam, &_tr)) return _tr;
    switch (msg) {
    case WM_CREATE: {
        RECT rc; GetClientRect(hWnd, &rc);
        HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 10, rc.right - 20, rc.bottom - 56, hWnd, (HMENU)100, g_inst, nullptr);
        SendMessageW(ed, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        // Show the embedded copy instantly (also the offline fallback)...
        SetWindowTextW(ed, BuildChangelogText(LoadTextResource(IDR_CHANGELOG_LAUNCHER),
                                              LoadTextResource(IDR_CHANGELOG_MCTDELINK)).c_str());
        // ...then refresh from GitHub in the background and swap it in if it arrives.
        HANDLE th = CreateThread(NULL, 0, ChangelogFetchThread, hWnd, 0, NULL);
        if (th) CloseHandle(th);
        HWND close = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
            rc.right - 90, rc.bottom - 38, 80, 28, hWnd, (HMENU)IDCANCEL, g_inst, nullptr);
        SendMessageW(close, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        return 0;
    }
    case WM_CHANGELOG_READY: {
        wchar_t* p = (wchar_t*)lParam;
        if (p) {
            HWND ed = GetDlgItem(hWnd, 100);
            if (ed) SetWindowTextW(ed, p);
            delete[] p;
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) { DestroyWindow(hWnd); return 0; }
        return 0;
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY:
        if (HWND owner = GetWindow(hWnd, GW_OWNER)) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void OpenChangelog(HWND owner) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = ChangelogWndProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"mctdeChangelog";
        RegisterClassW(&wc);
        registered = true;
    }
    RECT rc; GetWindowRect(owner, &rc);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"mctdeChangelog", L"Changelog",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        rc.left + 30, rc.top + 20, 580, 560, owner, nullptr, g_inst, nullptr);
    EnableWindow(owner, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (!IsWindow(dlg)) break;
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

// Themed header: title text top-left + a large Artorias filling the dead space on the right.
// Both are drawn in the theme's foreground colour (light on dark / dark on light).
static void DrawHeader(HWND hWnd, HDC hdc) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, ThText());
    HFONT old = (HFONT)SelectObject(hdc, g_titleFont);
    TextOutW(hdc, 16, 10, L"DARK SOULS", 10);
    SelectObject(hdc, g_subFont);
    TextOutW(hdc, 18, 52, L"Prepare To Die Edition", 22);
    SelectObject(hdc, g_mctdeFont);
    TextOutW(hdc, 18, 76, L"mctde", 5);
    SelectObject(hdc, old);

    if (g_artorias) {
        BITMAP bm; GetObjectW(g_artorias, sizeof(bm), &bm);
        // Fit (preserving aspect) into the right-side dead space, clear of the controls.
        const int boxL = 250, boxT = 2, boxR = 520, boxB = 248;
        double s = (double)(boxR - boxL) / bm.bmWidth;
        double s2 = (double)(boxB - boxT) / bm.bmHeight;
        if (s2 < s) s = s2;
        int w = (int)(bm.bmWidth * s), h = (int)(bm.bmHeight * s);
        int x = boxR - w, y = boxT + ((boxB - boxT) - h) / 2;
        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ o = SelectObject(mem, g_artorias);
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, nullptr);
        // white-on-black source. Dark: OR (DSo) -> figure stays white, black blends into the
        // dark bg. Light: DSna (dest AND NOT src) -> figure becomes black, bg is left untouched.
        StretchBlt(hdc, x, y, w, h, mem, 0, 0, bm.bmWidth, bm.bmHeight, g_dark ? SRCPAINT : 0x00220326u);
        SelectObject(mem, o);
        DeleteDC(mem);
    }
}

// ------------------------------------------------------------ versions + auto-update
// Installed mctde-Link version, read from d3d9.dll's file-version resource ("" if not present).
static std::string LinkInstalledVersion() {
    std::wstring dll = PathIn(L"d3d9.dll");
    DWORD ignored = 0;
    DWORD sz = GetFileVersionInfoSizeW(dll.c_str(), &ignored);
    if (!sz) return "";
    std::vector<BYTE> buf(sz);
    if (!GetFileVersionInfoW(dll.c_str(), 0, sz, buf.data())) return "";
    VS_FIXEDFILEINFO* ffi = nullptr; UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", (void**)&ffi, &len) || !ffi) return "";
    char v[64];
    sprintf_s(v, sizeof(v), "%u.%u.%u",
              HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS), HIWORD(ffi->dwFileVersionLS));
    return v;
}

static bool AutoUpdateEnabled() {
    return GetPrivateProfileIntW(L"Launcher", L"AutoUpdate", 0, PathIn(L"mctde-link.ini").c_str()) != 0;
}
static void SetAutoUpdate(bool on) {
    WritePrivateProfileStringW(L"Launcher", L"AutoUpdate", on ? L"1" : L"0", PathIn(L"mctde-link.ini").c_str());
}

static std::string TrimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b - a + 1);
}
static std::string ParseKv(const std::string& text, const std::string& key) {
    std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        size_t h = line.find('#'); if (h != std::string::npos) line = line.substr(0, h);
        size_t eq = line.find('='); if (eq == std::string::npos) continue;
        if (TrimStr(line.substr(0, eq)) == key) return TrimStr(line.substr(eq + 1));
    }
    return "";
}
// >0 if a>b, <0 if a<b, 0 equal (dotted-numeric compare)
static int CmpVer(const std::string& a, const std::string& b) {
    std::istringstream as(a), bs(b); std::string p;
    std::vector<int> av, bv;
    while (std::getline(as, p, '.')) av.push_back(atoi(p.c_str()));
    while (std::getline(bs, p, '.')) bv.push_back(atoi(p.c_str()));
    size_t n = av.size() > bv.size() ? av.size() : bv.size();
    for (size_t i = 0; i < n; ++i) {
        int x = i < av.size() ? av[i] : 0, y = i < bv.size() ? bv[i] : 0;
        if (x != y) return x - y;
    }
    return 0;
}

// HTTPS GET to a file. WinHTTP follows GitHub's release->CDN redirects automatically.
static bool DownloadToFile(const wchar_t* host, const wchar_t* path, const std::wstring& outFile, bool requirePE) {
    std::string body;
    if (!HttpsGet(host, path, body) || body.size() < 1024) return false;
    if (requirePE && (body.size() < 2 || body[0] != 'M' || body[1] != 'Z')) return false;  // must be a PE
    std::ofstream o(outFile.c_str(), std::ios::binary | std::ios::trunc);
    if (!o.is_open()) return false;
    o.write(body.data(), (std::streamsize)body.size());
    return o.good();
}

// Replace the installed d3d9.dll with the latest release build. Safe because the game (which
// loads d3d9.dll) is not running while the launcher is up, so the file isn't locked.
static bool UpdateLink() {
    std::wstring tmp = PathIn(L"d3d9.dll.new");
    if (!DownloadToFile(L"github.com", L"/McRoodyPoo/mctde-Link/releases/latest/download/d3d9.dll", tmp, true))
        return false;
    return MoveFileExW(tmp.c_str(), PathIn(L"d3d9.dll").c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// Self-update: download the new launcher, rename the running exe aside, move the new one into
// place, and relaunch. A rename failure rolls back (so a failed update never bricks the exe).
// Sets MCTDE_SKIP_UPDATE on the child so the relaunched instance can't loop.
static void UpdateLauncherAndRelaunch() {
    wchar_t self[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring selfPath = self, newPath = selfPath + L".new", oldPath = selfPath + L".old";
    if (!DownloadToFile(L"github.com", L"/McRoodyPoo/mctde-Launcher/releases/latest/download/mctde_launcher.exe",
                        newPath, true))
        return;
    DeleteFileW(oldPath.c_str());
    if (!MoveFileExW(selfPath.c_str(), oldPath.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(newPath.c_str()); return; }
    if (!MoveFileExW(newPath.c_str(), selfPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileW(oldPath.c_str(), selfPath.c_str());   // rollback
        return;
    }
    SetEnvironmentVariableW(L"MCTDE_SKIP_UPDATE", L"1");
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi = {0};
    std::wstring cmd = L"\"" + selfPath + L"\"";
    std::vector<wchar_t> cb(cmd.begin(), cmd.end()); cb.push_back(0);
    if (CreateProcessW(selfPath.c_str(), cb.data(), nullptr, nullptr, FALSE, 0, nullptr, g_dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    ExitProcess(0);
}

// Refresh the top-left date/time line from the system clock, using the user's locale formats.
static void SetClockLabel() {
    if (!g_lblClock) return;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t date[80] = {0}, time[80] = {0};
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, date, 80);
    GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, 80);
    std::wstring s = std::wstring(date) + L"    " + time;
    SetWindowTextW(g_lblClock, s.c_str());
}

static void SetVersionLabel(HWND hWnd) {
    std::string link = LinkInstalledVersion(); if (link.empty()) link = "?";
    std::string s = std::string("Launcher ") + LAUNCHER_VERSION + "      Link " + link;
    if (!g_latestLauncher.empty() && CmpVer(g_latestLauncher, LAUNCHER_VERSION) > 0) s += "   (update available)";
    else if (!g_latestLink.empty() && link != "?" && CmpVer(g_latestLink, link) > 0) s += "   (update available)";
    std::wstring w(s.begin(), s.end());
    if (g_lblVersion) SetWindowTextW(g_lblVersion, w.c_str());
}

// Background: read latest.txt, refresh the version label, and (if Auto-Update is on) update
// mctde-Link and/or the launcher when out of date.
static DWORD WINAPI UpdateThread(LPVOID param) {
    HWND main = (HWND)param;
    std::string manifest;
    if (!HttpsGet(L"raw.githubusercontent.com", L"/McRoodyPoo/mctde-Link/main/latest.txt", manifest))
        return 0;
    g_latestLink = ParseKv(manifest, "mctde-link");
    g_latestLauncher = ParseKv(manifest, "mctde-launcher");
    if (IsWindow(main)) PostMessageW(main, WM_VERSIONS_READY, 0, 0);

    wchar_t skip[8] = {0};
    bool justRelaunched = GetEnvironmentVariableW(L"MCTDE_SKIP_UPDATE", skip, 8) > 0;
    if (!AutoUpdateEnabled() || justRelaunched) return 0;

    std::string instLink = LinkInstalledVersion();
    bool linkOut = !g_latestLink.empty() && !instLink.empty() && CmpVer(g_latestLink, instLink) > 0;
    bool launcherOut = !g_latestLauncher.empty() && CmpVer(g_latestLauncher, LAUNCHER_VERSION) > 0;

    if (linkOut && UpdateLink() && IsWindow(main)) PostMessageW(main, WM_VERSIONS_READY, 0, 0);
    if (launcherOut) UpdateLauncherAndRelaunch();   // may relaunch + exit
    return 0;
}

// Update mctde-Link to its latest release in the background, used when the Auto-Update box is
// ticked at runtime (so checking the box updates Link right away, like the Auto-Launch DSCM box
// acts on tick). Fetches latest.txt if the startup check hasn't populated it yet, then swaps
// d3d9.dll only when the installed Link is actually older. The launcher's own self-update stays
// on the startup path -- replacing the running exe mid-session would force an immediate relaunch.
static DWORD WINAPI LinkUpdateThread(LPVOID param) {
    HWND main = (HWND)param;
    if (g_latestLink.empty()) {
        std::string manifest;
        if (HttpsGet(L"raw.githubusercontent.com", L"/McRoodyPoo/mctde-Link/main/latest.txt", manifest)) {
            g_latestLink = ParseKv(manifest, "mctde-link");
            g_latestLauncher = ParseKv(manifest, "mctde-launcher");
        }
    }
    std::string instLink = LinkInstalledVersion();
    bool linkOut = !g_latestLink.empty() && !instLink.empty() && CmpVer(g_latestLink, instLink) > 0;
    if (linkOut && UpdateLink() && IsWindow(main)) PostMessageW(main, WM_VERSIONS_READY, 0, 0);
    return 0;
}

// ------------------------------------------------------------ DSCM (Dark Souls Connectivity Mod / DaS-PC-MPChan)
// DSCM.exe is the connectivity mod's launcher, and it self-updates in place. So we never copy
// it into the DATA folder (a stale copy would just run the un-updated base build); instead we
// launch it WHERE IT LIVES, with its own folder as the working directory, so its updater keeps
// working. We learn that path from a running instance the first time DSCM is up, remember it in
// mctde-link.ini, and reuse it on later starts. Until a path is known the row stays greyed as
// "Searching for DSCM..." and a timer keeps re-scanning so starting DSCM by hand lights it up.
static const wchar_t* DSCM_EXE = L"DSCM.exe";

static bool AutoLaunchDscmEnabled() {
    return GetPrivateProfileIntW(L"Launcher", L"AutoLaunchDSCM", 0, PathIn(L"mctde-link.ini").c_str()) != 0;
}
static void SetAutoLaunchDscm(bool on) {
    WritePrivateProfileStringW(L"Launcher", L"AutoLaunchDSCM", on ? L"1" : L"0", PathIn(L"mctde-link.ini").c_str());
}
// Remembered full path to the user's real DSCM.exe (where it self-updates).
static std::wstring DscmSavedPath() {
    wchar_t buf[MAX_PATH] = {0};
    GetPrivateProfileStringW(L"Launcher", L"DscmPath", L"", buf, MAX_PATH, PathIn(L"mctde-link.ini").c_str());
    return buf;
}
static void SetDscmSavedPath(const std::wstring& p) {
    WritePrivateProfileStringW(L"Launcher", L"DscmPath", p.c_str(), PathIn(L"mctde-link.ini").c_str());
}

// Find a running DSCM.exe and return its full image path. false if none is running.
static bool FindDscmProcess(std::wstring& outPath) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, DSCM_EXE) != 0) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!h) continue;
            wchar_t buf[MAX_PATH] = {0}; DWORD n = MAX_PATH;
            if (QueryFullProcessImageNameW(h, 0, buf, &n)) { outPath = buf; found = true; }
            CloseHandle(h);
            if (found) break;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}
static bool DscmRunning() { std::wstring p; return FindDscmProcess(p); }

// The path we'd launch DSCM from, in priority order:
//   1. a running instance (also captured as the saved path),
//   2. the remembered path if it still exists,
//   3. a DSCM.exe sitting in the DATA folder next to the launcher.
// Empty if we still don't know where DSCM is. We never copy/move DSCM into the
// DATA folder (it self-updates in place), but if the user keeps it there we can
// still find and launch it from there when nothing is running.
static std::wstring DscmLaunchPath() {
    std::wstring running;
    if (FindDscmProcess(running) && !running.empty()) {
        if (_wcsicmp(running.c_str(), DscmSavedPath().c_str()) != 0) SetDscmSavedPath(running);
        return running;
    }
    std::wstring saved = DscmSavedPath();
    if (!saved.empty() && FileExists(saved)) return saved;
    std::wstring inData = PathIn(DSCM_EXE);
    if (FileExists(inData)) return inData;
    return L"";
}

// Place a launched DSCM's top-level window directly behind the launcher in z-order.
static BOOL CALLBACK SinkDscmBehindProc(HWND hwnd, LPARAM) {
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    if (pid == g_dscmPid && GetWindow(hwnd, GW_OWNER) == nullptr && IsWindowVisible(hwnd))
        SetWindowPos(hwnd, g_hMain, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return TRUE;
}

// Launch DSCM from its own folder, at normal size but WITHOUT activating it (DSCM mis-renders if
// created minimized, so it must come up normal). Its window appears a beat later and lands on
// top, so the caller starts IDT_DSCM_FRONT to sink it behind the launcher for a few seconds.
static void LaunchDscmInPlace(const std::wstring& exe) {
    g_dscmPid = 0;
    if (exe.empty() || !FileExists(exe)) return;
    std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/") + 1);
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNOACTIVATE;          // normal size + layout; don't steal focus
    PROCESS_INFORMATION pi = {0};
    std::wstring cmd = L"\"" + exe + L"\"";
    std::vector<wchar_t> cb(cmd.begin(), cmd.end()); cb.push_back(0);
    if (CreateProcessW(exe.c_str(), cb.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       dir.empty() ? nullptr : dir.c_str(), &si, &pi)) {
        g_dscmPid = pi.dwProcessId;
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
}

// Refresh the row's enabled state + label from the current detection result.
static void RefreshDscmRow(HWND hWnd) {
    bool was = g_dscmReady;
    g_dscmReady = !DscmLaunchPath().empty();
    EnableWindow(g_chkDscm, g_dscmReady ? TRUE : FALSE);
    SetWindowTextW(g_chkDscm, g_dscmReady ? L"Auto-Launch DSCM" : L"Searching for DSCM...");
    SendMessageW(g_chkDscm, BM_SETCHECK,
        (g_dscmReady && AutoLaunchDscmEnabled()) ? BST_CHECKED : BST_UNCHECKED, 0);
    if (g_dscmReady != was) RepaintOverBg(g_chkDscm);
}

static void ApplyAnd(HWND hWnd, bool launch) {
    SetPhantom(SendMessageW(g_chkPhantom, BM_GETCHECK, 0, 0) == BST_CHECKED);
    SetAutoUpdate(SendMessageW(g_chkAutoUpdate, BM_GETCHECK, 0, 0) == BST_CHECKED);
    SetDarkMode(g_dark);
    if (IsWindowEnabled(g_chkDsfix))
        ApplyDsfix(SendMessageW(g_chkDsfix, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (IsWindowEnabled(g_chkPractice))
        ApplyPractice(SendMessageW(g_chkPractice, BM_GETCHECK, 0, 0) == BST_CHECKED);
    // Only persist DSCM when detected -- don't clobber a saved "on" while it's undetected.
    if (IsWindowEnabled(g_chkDscm))
        SetAutoLaunchDscm(SendMessageW(g_chkDscm, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (launch) {
        if (LaunchGame()) DestroyWindow(hWnd);
    } else {
        DestroyWindow(hWnd);
    }
}

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT _tr = 0;
    if (ThemeHandle(hWnd, msg, wParam, lParam, &_tr)) return _tr;
    switch (msg) {
    case WM_CREATE: {
        g_hMain = hWnd;
        // "mctde Settings" button -- opens the mctde-link.ini panel (overlay placement/font/preview,
        // Hide Soul Counter, and the rest of mctde-Link's config). Sits in the bottom button row,
        // between Changelog (x 16..116) and Exit (x 300..380).
        HWND btnMctde = CreateWindowW(L"BUTTON", L"mctde Settings",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 130, 300, 150, 30,
            hWnd, (HMENU)IDC_BTN_MCTDE, g_inst, nullptr);
        // Widths are sized to the text so the controls' opaque background doesn't paint over
        // the Artorias silhouette on the right.
        g_chkPhantom = CreateWindowW(L"BUTTON", L"PhantomUnleashed (uncommon)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, 120, 288, 22,
            hWnd, (HMENU)IDC_CHK_PHANTOM, g_inst, nullptr);
        g_chkPractice = CreateWindowW(L"BUTTON", L"Eloise's PTDE Practice Tool",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, 160, 200, 22,
            hWnd, (HMENU)IDC_CHK_PRACTICE, g_inst, nullptr);
        // DSFix + Config sit at the bottom of the list.
        g_chkDsfix = CreateWindowW(L"BUTTON", L"DSFix",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, 200, 64, 22,
            hWnd, (HMENU)IDC_CHK_DSFIX, g_inst, nullptr);
        g_btnConfig = CreateWindowW(L"BUTTON", L"Config",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 84, 198, 72, 26,
            hWnd, (HMENU)IDC_BTN_CONFIG, g_inst, nullptr);
        // Auto-Launch DSCM (Dark Souls Connectivity Mod). Greyed as "Searching for DSCM..."
        // until DSCM.exe is found in the DATA folder (or copied in from a running instance).
        g_chkDscm = CreateWindowW(L"BUTTON", L"Searching for DSCM...",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, 238, 288, 22,
            hWnd, (HMENU)IDC_CHK_DSCM, g_inst, nullptr);
        // Live date/time in the empty top-left corner (the banner art sits top-right).
        g_lblClock = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 14, 10, 300, 16,
            hWnd, nullptr, g_inst, nullptr);
        // Version line, just under the date/time. Paints transparently over the banner
        // with the theme text colour, same as the date/time line.
        g_lblVersion = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 14, 28, 300, 16,
            hWnd, nullptr, g_inst, nullptr);
        // Auto-Update checkbox, left-aligned with the PLAY button below it.
        g_chkAutoUpdate = CreateWindowW(L"BUTTON", L"Auto-Update",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 404, 272, 110, 20,
            hWnd, (HMENU)IDC_CHK_AUTOUPDATE, g_inst, nullptr);
        g_chkDark = CreateWindowW(L"BUTTON", L"Dark Mode",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, 274, 120, 20,
            hWnd, (HMENU)IDC_CHK_DARK, g_inst, nullptr);
        HWND chlog = CreateWindowW(L"BUTTON", L"Changelog",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 16, 300, 100, 30,
            hWnd, (HMENU)IDC_BTN_CHANGELOG, g_inst, nullptr);
        HWND exit = CreateWindowW(L"BUTTON", L"Exit",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 300, 300, 80, 30,
            hWnd, (HMENU)IDC_BTN_EXIT, g_inst, nullptr);
        HWND play = CreateWindowW(L"BUTTON", L"PLAY",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW, 404, 296, 100, 40,
            hWnd, (HMENU)IDC_BTN_PLAY, g_inst, nullptr);

        for (HWND h : { btnMctde, g_chkPhantom, g_chkDsfix, g_btnConfig, g_chkPractice, g_chkDscm,
                        g_chkAutoUpdate, g_chkDark, g_lblVersion, g_lblClock, chlog, exit, play })
            SendMessageW(h, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        SendMessageW(g_chkAutoUpdate, BM_SETCHECK, AutoUpdateEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_chkDark, BM_SETCHECK, g_dark ? BST_CHECKED : BST_UNCHECKED, 0);
        SetVersionLabel(hWnd);   // installed versions immediately; refreshed when latest.txt lands
        SetClockLabel();         // show the time at once, then tick it every second
        SetTimer(hWnd, IDT_CLOCK, 1000, nullptr);
        if (HANDLE ut = CreateThread(NULL, 0, UpdateThread, hWnd, 0, NULL)) CloseHandle(ut);

        // initial state from disk
        SendMessageW(g_chkPhantom, BM_SETCHECK, PhantomEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
        if (DsfixInstalled()) {
            SendMessageW(g_chkDsfix, BM_SETCHECK, DsfixEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
        } else {
            // DSFix not detected in this folder -- nothing to toggle or configure, so grey it out.
            // (The launcher is standalone and never ships DSFix; the user installs it separately.)
            SendMessageW(g_chkDsfix, BM_SETCHECK, BST_UNCHECKED, 0);
            EnableWindow(g_chkDsfix, FALSE);
            EnableWindow(g_btnConfig, FALSE);
        }
        if (PracticeInstalled()) {
            // Reflect real state; unchecked by default because the tool ships parked (.off).
            SendMessageW(g_chkPractice, BM_SETCHECK, PracticeEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
        } else {
            // Practice tool not bundled -- keep a greyed (disabled) checkbox, but turn the row's
            // text into a link to the latest release so the user can go download it.
            SendMessageW(g_chkPractice, BM_SETCHECK, BST_UNCHECKED, 0);
            SetWindowTextW(g_chkPractice, L"");                       // box only, no label
            SetWindowPos(g_chkPractice, nullptr, 0, 0, 18, 22, SWP_NOMOVE | SWP_NOZORDER);
            EnableWindow(g_chkPractice, FALSE);

            g_lnkPractice = CreateWindowW(L"STATIC", L"Eloise's PTDE Practice Tool",
                WS_CHILD | WS_VISIBLE | SS_NOTIFY, 38, 161, 248, 20,
                hWnd, (HMENU)IDC_LNK_PRACTICE, g_inst, nullptr);
            SendMessageW(g_lnkPractice, WM_SETFONT, (WPARAM)g_linkFont, TRUE);
            g_origStaticProc = (WNDPROC)SetWindowLongPtrW(g_lnkPractice, GWLP_WNDPROC, (LONG_PTR)LinkProc);
        }

        // DSCM: detect its real location, then auto-start it minimized in place if enabled and
        // not already up. If we don't know where it is yet, keep polling for a running instance.
        RefreshDscmRow(hWnd);
        if (g_dscmReady && AutoLaunchDscmEnabled() && !DscmRunning()) {
            LaunchDscmInPlace(DscmLaunchPath());
            if (g_dscmPid) { g_dscmFrontTicks = 0; SetTimer(hWnd, IDT_DSCM_FRONT, 300, nullptr); }
        }
        if (!g_dscmReady)
            SetTimer(hWnd, IDT_DSCM_POLL, 2000, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (wParam == IDT_DSCM_POLL) {
            if (!g_dscmReady) RefreshDscmRow(hWnd);
            if (g_dscmReady) KillTimer(hWnd, IDT_DSCM_POLL);
            return 0;
        }
        if (wParam == IDT_CLOCK) {
            SetClockLabel();
            RepaintOverBg(g_lblClock);   // clear any ghost of the previous time over the banner
            return 0;
        }
        if (wParam == IDT_DSCM_FRONT) {
            // DSCM's window can appear (and re-raise itself) during startup -- keep sinking it
            // behind the launcher for ~3s, then stop.
            if (g_dscmPid) EnumWindows(SinkDscmBehindProc, 0);
            if (++g_dscmFrontTicks >= 10) KillTimer(hWnd, IDT_DSCM_FRONT);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        // In dark mode the banner background already carries the title + figure, so the
        // programmatic header would only double it up -- draw it in light mode only.
        if (!BgActive(hWnd)) DrawHeader(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_VERSIONS_READY:
        SetVersionLabel(hWnd);
        RepaintOverBg(g_lblVersion);   // clear any ghost of the old (shorter) version text
        return 0;
    case WM_COMMAND: {
        // Checkboxes paint transparently over the banner; clear ghosts behind the toggled box.
        if (BgActive(hWnd) && lParam && HIWORD(wParam) == BN_CLICKED)
            RepaintOverBg((HWND)lParam);
        switch (LOWORD(wParam)) {
        case IDC_BTN_CONFIG:    OpenDsfixConfig(hWnd); return 0;
        case IDC_BTN_MCTDE:     OpenMctdeSettings(hWnd); return 0;
        case IDC_BTN_CHANGELOG: OpenChangelog(hWnd);   return 0;
        case IDC_BTN_PLAY:      ApplyAnd(hWnd, true);  return 0;
        case IDC_BTN_EXIT:      ApplyAnd(hWnd, false); return 0;
        case IDC_CHK_AUTOUPDATE:
            // Persist the toggle immediately, and on enable kick a background mctde-Link update so
            // ticking the box updates Link right away instead of waiting for the next launch.
            SetAutoUpdate(CtlChecked(g_chkAutoUpdate));
            if (CtlChecked(g_chkAutoUpdate)) {
                HANDLE t = CreateThread(nullptr, 0, LinkUpdateThread, hWnd, 0, nullptr);
                if (t) CloseHandle(t);
            }
            return 0;
        case IDC_CHK_DSCM:
            // Ticking the box starts DSCM right away (if it isn't already running).
            if (CtlChecked(g_chkDscm) && !DscmRunning()) {
                LaunchDscmInPlace(DscmLaunchPath());
                if (g_dscmPid) { g_dscmFrontTicks = 0; SetTimer(hWnd, IDT_DSCM_FRONT, 300, nullptr); }
            }
            return 0;
        case IDC_CHK_DARK:
            g_dark = (SendMessageW(g_chkDark, BM_GETCHECK, 0, 0) == BST_CHECKED);
            ApplyTheme();
            RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        case IDC_LNK_PRACTICE:
            if (HIWORD(wParam) == STN_CLICKED)
                ShellExecuteW(hWnd, L"open", PRACTICE_RELEASE_URL, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:   DestroyWindow(hWnd); return 0;
    case WM_DESTROY: PostQuitMessage(0);  return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    g_inst = hInst;
    g_dir = ModuleDir();

    // Regenerate a full, documented mctde-link.ini if it's missing, BEFORE anything reads or
    // writes a key (a key-at-a-time write would otherwise leave a bare-bones file).
    EnsureIniSeeded();

    // Trackbar (slider) class for the mctde Settings overlay-padding sliders.
    { INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES }; InitCommonControlsEx(&icc); }

    // Clean up the previous exe left behind by a self-update.
    {
        wchar_t self[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        DeleteFileW((std::wstring(self) + L".old").c_str());
    }

    g_uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    g_dark = DarkModeEnabled();
    ApplyTheme();
    g_artorias = (HBITMAP)LoadImageW(hInst, MAKEINTRESOURCEW(IDB_ARTORIAS), IMAGE_BITMAP,
                                     0, 0, LR_CREATEDIBSECTION);
    g_bgDark = (HBITMAP)LoadImageW(hInst, MAKEINTRESOURCEW(IDB_BG_DARK), IMAGE_BITMAP,
                                   0, 0, LR_CREATEDIBSECTION);
    g_bgLight = (HBITMAP)LoadImageW(hInst, MAKEINTRESOURCEW(IDB_BG_LIGHT), IMAGE_BITMAP,
                                    0, 0, LR_CREATEDIBSECTION);
    // "DARK SOULS" title in OptimusPrincepsSemiBold -- the actual Dark Souls logo typeface.
    // NOTE: not a stock Windows font; must be installed on the system (or bundled + registered
    // via AddFontResourceEx) or GDI silently substitutes another serif.
    g_titleFont = CreateFontW(34, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_ROMAN, L"OptimusPrincepsSemiBold");
    g_subFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_ROMAN, L"Times New Roman");
    g_mctdeFont = CreateFontW(26, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_ROMAN, L"Times New Roman");
    // Underlined variant of the GUI font for the download link.
    LOGFONTW lf = {0};
    GetObjectW(g_uiFont, sizeof(lf), &lf);
    lf.lfUnderline = TRUE;
    g_linkFont = CreateFontIndirectW(&lf);

    HICON appIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hIcon = appIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"mctdeLauncher";
    RegisterClassW(&wc);

    // Client is 524x346 -- the size both pre-composed banners are authored/letterboxed to, so
    // each fills the background 1:1 with no stretch. The dark banner keeps its art in the
    // top-right dead space; the controls sit over the clear left/bottom.
    RECT rc = { 0, 0, 524, 346 };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;

    HWND hWnd = CreateWindowW(L"mctdeLauncher", L"mctde Launcher", style,
        sx, sy, ww, wh, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (g_artorias)  DeleteObject(g_artorias);
    if (g_bgDark)    DeleteObject(g_bgDark);
    if (g_bgLight)   DeleteObject(g_bgLight);
    if (g_titleFont) DeleteObject(g_titleFont);
    if (g_subFont)   DeleteObject(g_subFont);
    if (g_mctdeFont) DeleteObject(g_mctdeFont);
    if (g_linkFont)  DeleteObject(g_linkFont);
    if (g_brBg)      DeleteObject(g_brBg);
    if (g_brField)   DeleteObject(g_brField);
    return (int)msg.wParam;
}
