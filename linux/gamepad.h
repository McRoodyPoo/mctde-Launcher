// linux/gamepad.h -- controller navigation for the launcher (the Windows launcher's XInput block).
//
// SDL2's game-controller API is loaded at runtime (dlopen), the way the Windows launcher loads
// XInput: no SDL, no controller support, and nothing else changes. A pad is polled ~30x/sec while
// one is connected (about once a second otherwise) and only drives the UI while one of our windows
// is active, so holding the stick never leaks into the game or another app.
//
//   D-pad / left stick  move focus (main window: a hand-made map; Settings and dialogs: reading-
//                       order grid), with keyboard-style auto-repeat; Left/Right nudge a slider
//   A                   press the focused button / toggle, open a dropdown, commit an open one
//   B                   cancel an open dropdown, else close the dialog (Settings, message boxes)
//   Start               PLAY on the main window; the default button (Save) in a dialog
//   LB / RB             Settings: DSFix / mctde tab
//
// Any input while Auto-Play counts down cancels it (and is swallowed), like on Windows.
#pragma once

#include <QObject>
#include <QPointer>

#include <cstdint>
#include <functional>
#include <memory>

class QTimer;
class QWidget;
class QComboBox;

namespace pad {

struct Backend;   // SDL2, or a scripted fake for testing (MCTDE_FAKE_PAD)

class Navigator : public QObject {
public:
    explicit Navigator(QWidget* mainWindow);
    ~Navigator() override;

    // Main-window D-pad map: move focus one step in dir (0 up, 1 down, 2 left, 3 right).
    std::function<void(int dir)> mainNav;
    // Called on any input; return true to swallow it (Auto-Play cancel).
    std::function<bool()> onAnyInput;
    // Start on the main window.
    std::function<void()> onStart;

private:
    void Poll();
    void Direction(int dir);
    void GridNav(QWidget* top, int dir);
    void PressA();
    void PressB();
    void ComboKey(int qtKey);

    QWidget* main_;
    std::unique_ptr<Backend> backend_;
    QTimer* timer_ = nullptr;
    uint16_t prevButtons_ = 0;
    int navDir_ = 0;              // latched direction + 1 (0 = none)
    qint64 repeatAt_ = 0;
    int idleTick_ = 0;
    QPointer<QComboBox> comboOpen_;
};

// Put focus on the top-left-most focusable control inside w (a Settings tab opening).
void FocusFirstIn(QWidget* w);

} // namespace pad
