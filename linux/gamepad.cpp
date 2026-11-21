// linux/gamepad.cpp -- see gamepad.h.
#include "gamepad.h"

#include "theme.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QWidget>

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace pad {

// Buttons, XInput-style bits (as the Windows launcher uses them).
enum : uint16_t { UP = 0x0001, DOWN = 0x0002, LEFT = 0x0004, RIGHT = 0x0008, START = 0x0010, BACK = 0x0020,
                  LB = 0x0100, RB = 0x0200, A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000 };

struct State {
    bool connected = false;
    uint16_t buttons = 0;
    int lx = 0, ly = 0;   // left stick, -32768..32767, ly positive = up (XInput convention)
};

struct Backend {
    virtual ~Backend() = default;
    virtual bool Poll(State& s) = 0;     // false = no pad
    virtual bool Cheap() const { return false; }
};

// ---------------------------------------------------------------- SDL2 via dlopen
class SdlBackend : public Backend {
public:
    static std::unique_ptr<Backend> Load() {
        auto b = std::make_unique<SdlBackend>();
        return b->Init() ? std::move(b) : nullptr;
    }
    ~SdlBackend() override {
        CloseAll();
        if (QuitSub) QuitSub(kInitGameController);
        if (lib_) dlclose(lib_);
    }
    // Every connected controller drives the UI (buttons OR'd, the strongest stick deflection wins),
    // so it doesn't matter which pad SDL happens to list first.
    bool Poll(State& s) override {
        Pump();                                     // hot-plug detection needs the event pump
        Update();
        Flush(0, 0xFFFF);                           // nobody reads SDL's event queue; keep it empty
        int want = 0;
        for (int i = 0, n = NumJoysticks(); i < n; ++i) if (IsController(i)) ++want;
        bool lost = false;
        for (void* c : ctl_) if (!Attached(c)) lost = true;
        if (lost || int(ctl_.size()) != want) {     // plugged / unplugged: reopen the current set
            CloseAll();
            for (int i = 0, n = NumJoysticks(); i < n; ++i)
                if (IsController(i)) if (void* c = Open(i)) ctl_.push_back(c);
        }
        if (ctl_.empty()) return false;
        s.connected = true;
        for (void* c : ctl_) {
            auto btn = [&](int b) { return GetButton(c, b) != 0; };
            s.buttons |= (btn(11) ? UP : 0) | (btn(12) ? DOWN : 0) | (btn(13) ? LEFT : 0) | (btn(14) ? RIGHT : 0) |
                         (btn(6) ? START : 0) | (btn(4) ? BACK : 0) | (btn(9) ? LB : 0) | (btn(10) ? RB : 0) |
                         (btn(0) ? A : 0) | (btn(1) ? B : 0) | (btn(2) ? X : 0) | (btn(3) ? Y : 0);
            int lx = GetAxis(c, 0), ly = -GetAxis(c, 1) - 1;   // SDL's Y grows downward; flip to XInput's
            if (std::abs(lx) > std::abs(s.lx)) s.lx = lx;
            if (std::abs(ly) > std::abs(s.ly)) s.ly = ly;
        }
        return true;
    }

private:
    static constexpr uint32_t kInitGameController = 0x00002000u;
    template <class F> bool Sym(F& f, const char* name) { f = reinterpret_cast<F>(dlsym(lib_, name)); return f != nullptr; }
    bool Init() {
        for (const char* n : { "libSDL2-2.0.so.0", "libSDL2-2.0.so", "libSDL2.so" })
            if ((lib_ = dlopen(n, RTLD_NOW | RTLD_LOCAL))) break;
        if (!lib_) return false;
        bool ok = Sym(SetHint, "SDL_SetHint") && Sym(InitSub, "SDL_InitSubSystem") && Sym(QuitSub, "SDL_QuitSubSystem") &&
                  Sym(NumJoysticks, "SDL_NumJoysticks") && Sym(IsController, "SDL_IsGameController") &&
                  Sym(Open, "SDL_GameControllerOpen") && Sym(Close, "SDL_GameControllerClose") &&
                  Sym(Attached, "SDL_GameControllerGetAttached") && Sym(Update, "SDL_GameControllerUpdate") &&
                  Sym(GetButton, "SDL_GameControllerGetButton") && Sym(GetAxis, "SDL_GameControllerGetAxis") &&
                  Sym(Flush, "SDL_FlushEvents") && Sym(Pump, "SDL_PumpEvents");
        if (!ok) return false;
        SetHint("SDL_NO_SIGNAL_HANDLERS", "1");                 // leave Ctrl+C etc. to Qt
        SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");   // we have no SDL window to focus
        SetHint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "0");   // A = bottom face button, like XInput
        return InitSub(kInitGameController) == 0;
    }
    void CloseAll() { if (Close) for (void* c : ctl_) Close(c); ctl_.clear(); }
    void* lib_ = nullptr;
    std::vector<void*> ctl_;
    void (*Pump)() = nullptr;
    int (*SetHint)(const char*, const char*) = nullptr;
    int (*InitSub)(uint32_t) = nullptr;
    void (*QuitSub)(uint32_t) = nullptr;
    int (*NumJoysticks)() = nullptr;
    int (*IsController)(int) = nullptr;
    void* (*Open)(int) = nullptr;
    void (*Close)(void*) = nullptr;
    int (*Attached)(void*) = nullptr;
    void (*Update)() = nullptr;
    uint8_t (*GetButton)(void*, int) = nullptr;
    int16_t (*GetAxis)(void*, int) = nullptr;
    void (*Flush)(uint32_t, uint32_t) = nullptr;
};

// ---------------------------------------------------------------- scripted fake (testing)
// MCTDE_FAKE_PAD="down,down,a,wait,start" presses one input per two polls (press, release).
// MCTDE_FAKE_PAD_SHOT=<file.png> grabs the active window once the script ends, then quits.
class FakeBackend : public Backend {
public:
    explicit FakeBackend(const char* script) {
        std::string s = script, tok;
        for (size_t i = 0; i <= s.size(); ++i) {
            if (i == s.size() || s[i] == ',') { if (!tok.empty()) steps_.push_back(tok); tok.clear(); }
            else tok += char(tolower(s[i]));
        }
    }
    bool Cheap() const override { return true; }
    bool Poll(State& s) override {
        s.connected = true;
        size_t idx = tick_ / 2;
        bool press = tick_ % 2 == 0;
        ++tick_;
        if (idx >= steps_.size()) {
            if (!done_) {
                done_ = true;
                if (const char* shot = getenv("MCTDE_FAKE_PAD_SHOT"))
                    QTimer::singleShot(400, [shot] {
                        QWidget* w = QApplication::activePopupWidget() ? QApplication::activePopupWidget()
                                                                       : QApplication::activeWindow();
                        if (w) w->window()->grab().save(shot);
                        QApplication::exit(0);
                    });
            }
            return true;
        }
        if (!press) return true;
        const std::string& t = steps_[idx];
        if (t == "up") s.buttons = UP;       else if (t == "down") s.buttons = DOWN;
        else if (t == "left") s.buttons = LEFT; else if (t == "right") s.buttons = RIGHT;
        else if (t == "a") s.buttons = A;    else if (t == "b") s.buttons = B;
        else if (t == "start") s.buttons = START;
        else if (t == "lb") s.buttons = LB;  else if (t == "rb") s.buttons = RB;
        return true;
    }
private:
    std::vector<std::string> steps_;
    size_t tick_ = 0;
    bool done_ = false;
};

// ---------------------------------------------------------------- focus helpers
static bool Focusable(QWidget* w) {
    if (!w->isVisible() || !w->isEnabled() || w->focusPolicy() == Qt::NoFocus) return false;
    return qobject_cast<QAbstractButton*>(w) || qobject_cast<QComboBox*>(w) ||
           qobject_cast<QLineEdit*>(w) || qobject_cast<QSlider*>(w);
}
static std::vector<QWidget*> Collect(QWidget* top) {
    std::vector<QWidget*> v;
    for (QWidget* w : top->findChildren<QWidget*>()) if (Focusable(w)) v.push_back(w);
    return v;
}
static QPoint Center(QWidget* w, QWidget* top) { return w->mapTo(top, w->rect().center()); }

// Scroll a Settings page so a newly focused control is fully on screen.
static void EnsureVisible(QWidget* w) {
    for (QWidget* p = w->parentWidget(); p; p = p->parentWidget())
        if (auto* area = qobject_cast<QScrollArea*>(p)) { area->ensureWidgetVisible(w, 0, 14); return; }
}
static void Focus(QWidget* w) {
    w->setFocus(Qt::TabFocusReason);
    EnsureVisible(w);
}

void FocusFirstIn(QWidget* panel) {
    if (!panel) return;
    std::vector<QWidget*> v = Collect(panel);
    if (v.empty()) return;
    QWidget* best = v[0];
    QPoint bp = best->mapTo(panel, QPoint(0, 0));
    for (QWidget* w : v) {
        QPoint p = w->mapTo(panel, QPoint(0, 0));
        if (p.y() < bp.y() - 8 || (std::abs(p.y() - bp.y()) <= 8 && p.x() < bp.x())) { best = w; bp = p; }
    }
    best->setFocus(Qt::OtherFocusReason);
    EnsureVisible(best);
}

// Press a button AFTER this poll returns. A click can open a modal dialog (Settings) whose event
// loop would otherwise run inside the poll timer's callback, and Qt never re-fires a timer that is
// still inside its callback -- the controller would go dead for as long as the dialog is open.
static void ClickLater(QAbstractButton* b) { QMetaObject::invokeMethod(b, "click", Qt::QueuedConnection); }

// ---------------------------------------------------------------- Navigator
Navigator::Navigator(QWidget* mainWindow) : QObject(mainWindow), main_(mainWindow) {
    if (const char* script = getenv("MCTDE_FAKE_PAD")) backend_ = std::make_unique<FakeBackend>(script);
    else backend_ = SdlBackend::Load();
    if (!backend_) return;   // no SDL2: no controller support, nothing else changes
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] { Poll(); });
    timer_->start(33);
}
Navigator::~Navigator() = default;

static void SetConnected(bool on) {
    if (ui::g_padConnected == on) return;
    ui::g_padConnected = on;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        w->update();
        for (QWidget* c : w->findChildren<QWidget*>()) c->update();
    }
}

void Navigator::Poll() {
    // Probing for a pad that isn't there costs more than reading one, so with none connected we
    // only look about once a second; with one connected, every tick.
    if (!ui::g_padConnected && !backend_->Cheap() && (++idleTick_ % 30) != 0) return;
    State st;
    if (!backend_->Poll(st)) { SetConnected(false); prevButtons_ = 0; navDir_ = 0; return; }
    SetConnected(true);

    if (getenv("MCTDE_PAD_DEBUG") && st.buttons)
        fprintf(stderr, "pad 0x%04x active=%s popup=%d focus=%s\n", st.buttons,
                QApplication::activeWindow() ? QApplication::activeWindow()->metaObject()->className() : "-",
                QApplication::activePopupWidget() != nullptr,
                QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className() : "-");
    // Only drive our own UI while one of our windows is active.
    if (!QApplication::activeWindow() && !QApplication::activePopupWidget()) { prevButtons_ = st.buttons; navDir_ = 0; return; }

    const uint16_t b = st.buttons, pressed = uint16_t(b & ~prevButtons_);
    const int DZ = 16000;
    const bool stick = st.lx > DZ || st.lx < -DZ || st.ly > DZ || st.ly < -DZ;
    if ((b || stick) && onAnyInput && onAnyInput()) { prevButtons_ = b; navDir_ = 1 + 4; return; }   // swallowed

    if (comboOpen_ && !comboOpen_->view()->isVisible()) comboOpen_ = nullptr;   // closed elsewhere

    // Directions from the d-pad or stick. Latch only a clean single-axis press (a d-pad press often
    // smears into a diagonal), repeat it while held, and release the latch at neutral.
    bool dir[4] = { (b & UP) || st.ly > DZ, (b & DOWN) || st.ly < -DZ, (b & LEFT) || st.lx < -DZ, (b & RIGHT) || st.lx > DZ };
    int active = dir[0] + dir[1] + dir[2] + dir[3];
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (active == 0) {
        navDir_ = 0;
    } else if (navDir_ == 0) {
        if (active == 1) {
            int d = dir[0] ? 0 : dir[1] ? 1 : dir[2] ? 2 : 3;
            navDir_ = d + 1; Direction(d); repeatAt_ = now + 420;
        }
    } else if (navDir_ <= 4 && dir[navDir_ - 1] && now >= repeatAt_) {
        Direction(navDir_ - 1); repeatAt_ = now + 170;
    }

    if (pressed & A) PressA();
    if (pressed & B) PressB();
    if (!comboOpen_) {   // Start / shoulders wait while a dropdown is open
        QWidget* top = QApplication::activeWindow();
        if ((pressed & START) && top) {
            if (top == main_) { if (onStart) QTimer::singleShot(0, this, [this] { onStart(); }); }
            else for (QPushButton* p : top->findChildren<QPushButton*>()) if (p->isDefault() && p->isVisible()) { ClickLater(p); break; }
        }
        if ((pressed & (LB | RB)) && top) {
            // Cycle LB/RB through whichever Settings tabs exist (DSFix, mctde, Vulkan). The active
            // tab carries a dynamic "cur" property set by SettingsDialog::SelectTab.
            const char* names[] = { "tabDs", "tabMc", "tabVk" };
            QPushButton* tabs[3] = { nullptr, nullptr, nullptr };
            int n = 0, cur = 0;
            for (const char* nm : names) {
                if (auto* t = top->findChild<QPushButton*>(nm)) {
                    if (t->property("cur").toBool()) cur = n;
                    tabs[n++] = t;
                }
            }
            if (n > 0) {
                int next = (pressed & LB) ? (cur + n - 1) % n : (cur + 1) % n;
                if (tabs[next]) ClickLater(tabs[next]);
            }
        }
    }
    prevButtons_ = b;
}

void Navigator::ComboKey(int key) {
    if (!comboOpen_) return;
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier), release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(comboOpen_->view(), &press);
    QApplication::sendEvent(comboOpen_->view(), &release);
}

void Navigator::Direction(int d) {
    if (comboOpen_) { if (d <= 1) ComboKey(d == 0 ? Qt::Key_Up : Qt::Key_Down); return; }
    QWidget* top = QApplication::activeWindow();
    if (!top) return;
    if (top == main_ && mainNav) { mainNav(d); return; }
    if (d >= 2) {   // Left/Right on a slider nudges it
        if (auto* s = qobject_cast<QSlider*>(QApplication::focusWidget())) {
            int step = std::max(1, (s->maximum() - s->minimum()) / 20);
            s->setValue(s->value() + (d == 3 ? step : -step));
            return;
        }
    }
    GridNav(top, d);
}

// Reading-order grid navigation (Settings and any dialog): controls group into rows by vertical
// centre; Up/Down go to the nearest control in the adjacent row, Left/Right along the same row.
void Navigator::GridNav(QWidget* top, int dir) {
    std::vector<QWidget*> v = Collect(top);
    if (v.empty()) return;
    struct Item { QWidget* w; int cx, cy; };
    std::vector<Item> it;
    for (QWidget* w : v) { QPoint c = Center(w, top); it.push_back({ w, c.x(), c.y() }); }
    QWidget* cur = QApplication::focusWidget();
    int me = -1;
    for (int i = 0; i < int(it.size()); ++i) if (it[i].w == cur) me = i;
    if (me < 0) {   // nothing focused yet: the top-left-most control
        int bst = 0;
        for (int i = 1; i < int(it.size()); ++i)
            if (it[i].cy < it[bst].cy - 8 || (std::abs(it[i].cy - it[bst].cy) <= 8 && it[i].cx < it[bst].cx)) bst = i;
        Focus(it[bst].w);
        return;
    }
    const int ROWTOL = 14, mcx = it[me].cx, mcy = it[me].cy;
    int best = -1;
    if (dir >= 2) {
        for (int i = 0; i < int(it.size()); ++i) {
            if (i == me || std::abs(it[i].cy - mcy) > ROWTOL) continue;
            bool side = dir == 3 ? it[i].cx > mcx : it[i].cx < mcx;
            if (side && (best < 0 || std::abs(it[i].cx - mcx) < std::abs(it[best].cx - mcx))) best = i;
        }
    } else {
        int tcy = 0; bool found = false;
        for (auto& x : it) {
            if (dir == 1 && x.cy > mcy + ROWTOL && (!found || x.cy < tcy)) { tcy = x.cy; found = true; }
            if (dir == 0 && x.cy < mcy - ROWTOL && (!found || x.cy > tcy)) { tcy = x.cy; found = true; }
        }
        if (found)
            for (int i = 0; i < int(it.size()); ++i)
                if (std::abs(it[i].cy - tcy) <= ROWTOL && (best < 0 || std::abs(it[i].cx - mcx) < std::abs(it[best].cx - mcx))) best = i;
    }
    if (best >= 0) Focus(it[best].w);
}

void Navigator::PressA() {
    if (comboOpen_) { ComboKey(Qt::Key_Return); comboOpen_ = nullptr; return; }
    QWidget* f = QApplication::focusWidget();
    if (!f) return;
    if (auto* btn = qobject_cast<QAbstractButton*>(f)) { ClickLater(btn); return; }
    if (auto* cb = qobject_cast<QComboBox*>(f)) { comboOpen_ = cb; cb->showPopup(); }
}

void Navigator::PressB() {
    if (comboOpen_) { ComboKey(Qt::Key_Escape); comboOpen_ = nullptr; return; }
    QWidget* top = QApplication::activeWindow();
    if (top && top != main_) QMetaObject::invokeMethod(top, "close", Qt::QueuedConnection);
}

} // namespace pad
