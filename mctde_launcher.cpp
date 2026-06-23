// mctde_launcher.cpp
//
// Standalone launcher for the mctde-Link mod. Native Win32 (no .NET / no runtime) so it
// runs cleanly under Proton/Wine in the SAME prefix as the game -- it spawns DARKSOULS.exe
// directly. Point your Steam shortcut at this exe.
//
// It sits in the DATA folder next to d3d9.dll, dinput8.dll (DSFix), dsfix.ini, mctde-link.ini
// and DARKSOULS.exe, and exposes:
//   * "Increased Phantom Limit"  -> writes [PhantomUnleashed] Mode=On/Off in mctde-link.ini.
//        On Proton the in-game Ask prompt is suppressed (PhantomUnleashed.cpp), so this is the
//        only way Linux players can opt in without hand-editing the ini.
//   * "DSFix" (+ Config)         -> enables/disables DSFix by renaming its wrapper
//        dinput8.dll <-> dinput8.dll.off, and opens a settings panel that round-trips dsfix.ini.
//   * PLAY / Exit.
//
// Build: launcher\build_launcher.bat  (32-bit, /MT, no external deps).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

// ------------------------------------------------------------ control IDs
#define IDC_CHK_PHANTOM  1001
#define IDC_CHK_DSFIX    1002
#define IDC_BTN_CONFIG   1003
#define IDC_BTN_PLAY     1004
#define IDC_BTN_EXIT     1005
#define IDC_CHK_PRACTICE 1006
#define IDC_LNK_PRACTICE 1007
#define IDC_BTN_CHANGELOG 1008

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
// HUD Mod sub-window
#define IDC_HUD_EN       2060
#define IDC_HUD_MIN      2061
#define IDC_HUD_SCALE    2062
#define IDC_HUD_OP       2063
#define IDC_HUD_SAVE     2064
#define IDC_HUD_CANCEL   2065
#define IDC_HUD_LBL_SCALE 2066
#define IDC_HUD_LBL_OP    2067

static HINSTANCE g_inst = nullptr;
static HFONT     g_uiFont = nullptr;     // standard control font
static HBITMAP   g_banner = nullptr;     // pre-composed banner (title text + Artorias)

static HWND g_chkPhantom  = nullptr;
static HWND g_chkDsfix    = nullptr;
static HWND g_btnConfig   = nullptr;
static HWND g_chkPractice = nullptr;
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

// ------------------------------------------------------------ mctde-link.ini (standard INI)
static bool PhantomEnabled() {
    wchar_t buf[16] = {0};
    GetPrivateProfileStringW(L"PhantomUnleashed", L"Mode", L"Off", buf, 16, PathIn(L"mctde-link.ini").c_str());
    return _wcsicmp(buf, L"On") == 0;
}
static void SetPhantom(bool on) {
    WritePrivateProfileStringW(L"PhantomUnleashed", L"Mode", on ? L"On" : L"Off",
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
static bool LaunchGame() {
    // Mark the game process as launcher-spawned so mctde-Link's launcher guard lets it run
    // (the child inherits this env var). Without it, the mod relaunches the launcher and quits.
    SetEnvironmentVariableW(L"MCTDE_VIA_LAUNCHER", L"1");
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

static std::string g_dsText;        // loaded dsfix.ini contents, edited on save
static bool g_advanced = false;     // Advanced Options toggle (per-open, not persisted)
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
    HWND hdrOther, skipIntro, logLvl;
    HWND hdrDinput, dinput[10];
    HWND hdrDof, lblDofRes, dofRes, disDofScale, lblDofBlur, dofBlur, defDof;
    HWND hdrFps, unlockFps, lblFpsLimit, fpsLimit, lblFpsThresh, fpsThresh, fpsStab;
    HWND hdrBackup, enBackup, lblBkpInt, bkpInt, lblBkpMax, bkpMax;
    HWND hdrNotReady, forceWin, enVsync, lblFsHz, fsHz;
    HWND credit, save, cancel;
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
        HWND s = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                               fx, y, 70, 28, hWnd, (HMENU)IDC_HUD_SAVE, g_inst, nullptr);
        HWND c = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
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
    mv(U.lblAA, LX, y + 3, 28); mv(U.aaType, 48, y, 96, 200); mv(U.lblAAQ, 150, y + 3, 66); mv(U.aaQual, 220, y, 80, 200); y += ROW;
    mv(U.lblSSAO, LX, y + 3, 40); mv(U.ssao, 58, y, 78, 200); mv(U.lblFilter, 150, y + 3, 110); mv(U.filter, 262, y, 110, 200); y += ROW;
    mv(U.border, LX, y, 250, 22); y += ROW;        // normal left checkbox
    mv(U.hudMod, LX, y, 250, 22); y += ROW;

    // --- Cursor ---
    mv(U.hdrCursor, LX, y, 250, 18); y += HDR;
    mv(U.capCur, LX, y, 180, 22); mv(U.disCur, 200, y, 180, 22); y += ROW;

    // --- Other ---
    mv(U.hdrOther, LX, y, 250, 18); y += HDR;
    mv(U.skipIntro, LX, y, 180, 22); mv(U.logLvl, 200, y, 190, 22); y += ROW;

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

        U.lblAA = DsMakeLabel(hWnd, L"AA", 0, 0, 28);
        U.aaType = DsMakeCombo(hWnd, IDC_DS_AATYPE, 0, 0, 96, { L"none", L"SMAA", L"FXAA" }, G("aaType", "SMAA"));
        U.lblAAQ = DsMakeLabel(hWnd, L"AA Quality", 0, 0, 66);
        U.aaQual = MakeMappedCombo(hWnd, IDC_DS_AAQUAL, 80, AAQ_LBL, AAQ_VAL, G("aaQuality", "4"));

        U.lblSSAO = DsMakeLabel(hWnd, L"SSAO", 0, 0, 40);
        U.ssao = MakeMappedCombo(hWnd, IDC_DS_SSAO, 80, SSAO_LBL, SSAO_VAL, G("ssaoStrength", "0"));
        U.lblFilter = DsMakeLabel(hWnd, L"Texture Filtering", 0, 0, 110);
        U.filter = MakeMappedCombo(hWnd, IDC_DS_FILTER, 110, FILT_LBL, FILT_VAL, G("filteringOverride", "0"));

        U.border = DsMakeCheck(hWnd, IDC_DS_BORDER, L"Borderless Fullscreen", G("borderlessFullscreen", "0") == "1");
        U.hudMod = DsMakeCheck(hWnd, IDC_DS_HUDMOD, L"HUD Mod (opens options)", g_hudEn == "1");
        // AA Quality is meaningless when antialiasing is off.
        if (G("aaType", "SMAA") == "none") EnableWindow(U.aaQual, FALSE);

        U.hdrOther = DsMakeLabel(hWnd, L"--- Other ---", 0, 0, 250);
        U.skipIntro = DsMakeCheck(hWnd, IDC_DS_SKIP, L"Skip intro", G("skipIntro", "1") == "1");
        U.logLvl = DsMakeCheck(hWnd, IDC_DS_LOG, L"Logging", G("logLevel", "6") != "6");

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
        U.fpsStab = DsMakeCheck(hWnd, IDC_DS_FPSSTAB, L"FPS Stabilizer", true);

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
        U.save = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                               0, 0, 70, 28, hWnd, (HMENU)IDC_DS_SAVE, g_inst, nullptr);
        U.cancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
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
        if (id == IDC_DS_SAVE) {
            std::string t = g_dsText;
            auto setT = [&](const char* k, HWND h) { t = DsfixSet(t, k, CtlText(h)); };
            auto setC = [&](const char* k, HWND h) { t = DsfixSet(t, k, CtlCombo(h)); };
            auto setB = [&](const char* k, HWND h) { t = DsfixSet(t, k, CtlChecked(h) ? "1" : "0"); };

            setT("renderWidth", U.rw);   setT("renderHeight", U.rh);
            setT("presentWidth", U.pw);  setT("presentHeight", U.ph);
            setC("aaType", U.aaType);
            t = DsfixSet(t, "aaQuality", CtlCombo(U.aaType) == "none" ? "0" : MappedValue(U.aaQual, AAQ_VAL));
            t = DsfixSet(t, "ssaoStrength", MappedValue(U.ssao, SSAO_VAL));
            t = DsfixSet(t, "filteringOverride", MappedValue(U.filter, FILT_VAL));
            setB("borderlessFullscreen", U.border);
            setB("captureCursor", U.capCur);
            setB("disableCursor", U.disCur);
            setB("skipIntro", U.skipIntro);
            t = DsfixSet(t, "logLevel", CtlChecked(U.logLvl) ? "3" : "6");

            // HUD values (edited via the sub-window)
            t = DsfixSet(t, "enableHudMod", g_hudEn);
            t = DsfixSet(t, "enableMinimalHud", g_hudMin);
            t = DsfixSet(t, "hudScaleFactor", g_hudScale);
            t = DsfixSet(t, "hudOpacity", g_hudOpacity);

            // Depth of Field
            if (g_advanced) {
                setT("dofOverrideResolution", U.dofRes);
                setB("disableDofScaling", U.disDofScale);
                setT("dofBlurAmount", U.dofBlur);
            } else {
                bool orig = CtlChecked(U.defDof);         // "Original DoF" checked => vanilla DoF (all zero)
                t = DsfixSet(t, "dofOverrideResolution", orig ? "0" : "540");
                t = DsfixSet(t, "disableDofScaling",     orig ? "0" : "1");
                t = DsfixSet(t, "dofBlurAmount",         orig ? "0" : "1");
            }

            // Framerate
            if (g_advanced) {
                setB("unlockFPS", U.unlockFps);
                setT("FPSlimit", U.fpsLimit);
                setT("FPSthreshold", U.fpsThresh);
            } else {
                bool stab = CtlChecked(U.fpsStab);
                t = DsfixSet(t, "unlockFPS", stab ? "1" : "0");
                t = DsfixSet(t, "FPSlimit", "30");        // FPSthreshold left untouched in basic mode
            }

            // Advanced-only sections: only written when shown, so basic mode preserves the ini.
            if (g_advanced) {
                t = DsfixSet(t, "dinput8dllWrapper", CtlCombo(U.dinput[0]));
                for (int i = 1; i < 10; ++i)
                    t = DsfixSet(t, "dinputChain" + std::to_string(i), CtlCombo(U.dinput[i]));
                setB("enableBackups", U.enBackup);
                setT("backupInterval", U.bkpInt);
                setT("maxBackups", U.bkpMax);
                setB("forceWindowed", U.forceWin);
                setB("enableVsync", U.enVsync);
                setT("fullscreenHz", U.fsHz);
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

static LRESULT CALLBACK ChangelogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        RECT rc; GetClientRect(hWnd, &rc);
        HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 10, rc.right - 20, rc.bottom - 56, hWnd, (HMENU)100, g_inst, nullptr);
        SendMessageW(ed, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        std::string combined =
            "=== mctde-Launcher ===\n\n" + LoadTextResource(IDR_CHANGELOG_LAUNCHER) +
            "\n\n\n=== mctde-Link ===\n\n" + LoadTextResource(IDR_CHANGELOG_MCTDELINK);
        // EDIT controls need CRLF line breaks; normalize first.
        std::string norm; norm.reserve(combined.size() + 128);
        for (char c : combined) { if (c == '\r') continue; if (c == '\n') norm += "\r\n"; else norm += c; }
        SetWindowTextW(ed, Utf8ToWide(norm).c_str());
        HWND close = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            rc.right - 90, rc.bottom - 38, 80, 28, hWnd, (HMENU)IDCANCEL, g_inst, nullptr);
        SendMessageW(close, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
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

// Banner area: full client width, flush to the top.
static const int BANNER_W = 524, BANNER_H = 115;
static void DrawBanner(HWND hWnd, HDC hdc) {
    if (!g_banner) return;
    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, g_banner);
    BITMAP bm; GetObjectW(g_banner, sizeof(bm), &bm);
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchBlt(hdc, 0, 0, BANNER_W, BANNER_H, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void ApplyAnd(HWND hWnd, bool launch) {
    SetPhantom(SendMessageW(g_chkPhantom, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (IsWindowEnabled(g_chkDsfix))
        ApplyDsfix(SendMessageW(g_chkDsfix, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (IsWindowEnabled(g_chkPractice))
        ApplyPractice(SendMessageW(g_chkPractice, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (launch) {
        if (LaunchGame()) DestroyWindow(hWnd);
    } else {
        DestroyWindow(hWnd);
    }
}

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_chkPhantom = CreateWindowW(L"BUTTON", L"Increased Phantom Limit (not commonly used)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 120, 440, 22,
            hWnd, (HMENU)IDC_CHK_PHANTOM, g_inst, nullptr);
        g_chkPractice = CreateWindowW(L"BUTTON", L"Eloise's PTDE Practice Tool",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 160, 440, 22,
            hWnd, (HMENU)IDC_CHK_PRACTICE, g_inst, nullptr);
        // DSFix + Config sit at the bottom of the list.
        g_chkDsfix = CreateWindowW(L"BUTTON", L"DSFix",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 200, 64, 22,
            hWnd, (HMENU)IDC_CHK_DSFIX, g_inst, nullptr);
        g_btnConfig = CreateWindowW(L"BUTTON", L"Config",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 92, 198, 72, 26,
            hWnd, (HMENU)IDC_BTN_CONFIG, g_inst, nullptr);
        HWND chlog = CreateWindowW(L"BUTTON", L"Changelog",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 16, 250, 100, 30,
            hWnd, (HMENU)IDC_BTN_CHANGELOG, g_inst, nullptr);
        HWND exit = CreateWindowW(L"BUTTON", L"Exit",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 320, 250, 80, 30,
            hWnd, (HMENU)IDC_BTN_EXIT, g_inst, nullptr);
        HWND play = CreateWindowW(L"BUTTON", L"PLAY",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 412, 244, 100, 40,
            hWnd, (HMENU)IDC_BTN_PLAY, g_inst, nullptr);

        for (HWND h : { g_chkPhantom, g_chkDsfix, g_btnConfig, g_chkPractice, chlog, exit, play })
            SendMessageW(h, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

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

            g_lnkPractice = CreateWindowW(L"STATIC", L"Eloise's PTDE Practice Tool (download)",
                WS_CHILD | WS_VISIBLE | SS_NOTIFY, 46, 161, 420, 20,
                hWnd, (HMENU)IDC_LNK_PRACTICE, g_inst, nullptr);
            SendMessageW(g_lnkPractice, WM_SETFONT, (WPARAM)g_linkFont, TRUE);
            g_origStaticProc = (WNDPROC)SetWindowLongPtrW(g_lnkPractice, GWLP_WNDPROC, (LONG_PTR)LinkProc);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        DrawBanner(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam == g_lnkPractice) {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, RGB(0, 90, 200));   // link blue
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDC_BTN_CONFIG:    OpenDsfixConfig(hWnd); return 0;
        case IDC_BTN_CHANGELOG: OpenChangelog(hWnd);   return 0;
        case IDC_BTN_PLAY:      ApplyAnd(hWnd, true);  return 0;
        case IDC_BTN_EXIT:      ApplyAnd(hWnd, false); return 0;
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

    g_uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    g_banner = (HBITMAP)LoadImageW(hInst, MAKEINTRESOURCEW(IDB_BANNER), IMAGE_BITMAP,
                                   0, 0, LR_CREATEDIBSECTION);
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

    RECT rc = { 0, 0, 524, 300 };
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
    if (g_banner)   DeleteObject(g_banner);
    if (g_linkFont) DeleteObject(g_linkFont);
    return (int)msg.wParam;
}
