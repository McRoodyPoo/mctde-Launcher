// linux/gui.cpp -- the native Linux launcher window (Qt 6 Widgets).
//
// Mirrors the Windows launcher's main window: the same 524x346 banner art, the same slide-switch
// toggles in the same places, the rounded gold PLAY button, Auto-Play, and PLAY / Exit. Settings write the same mctde-link.ini keys and DLL renames (core/),
// so a folder behaves the same whichever launcher touched it last. PLAY runs the game through
// Proton in PTDE's prefix (linux/launch.h). Run with a command (e.g. `play`, `status`) for the CLI.
#include "../core/install.h"
#include "../core/platform.h"
#include "cli.h"
#include "exe_opener.h"
#include "gamepad.h"
#include "kwin_behind.h"
#include "launch.h"
#include "pe_version.h"
#include "settings.h"
#include "theme.h"
#include "steam.h"
#include "update.h"
#include "version.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QFile>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>

namespace {

using namespace ui;

// Run fn on a worker thread, then done(result) back on the GUI thread (if the window still exists).
template <class R>
void RunAsync(QWidget* owner, std::function<R()> fn, std::function<void(R)> done) {
    QPointer<QWidget> guard(owner);
    std::thread([guard, fn, done]() {
        auto result = std::make_shared<R>(fn());
        if (!guard) return;
        QMetaObject::invokeMethod(guard.data(), [guard, done, result]() { if (guard) done(*result); },
                                  Qt::QueuedConnection);
    }).detach();
}

class LauncherWindow : public QWidget {
public:
    explicit LauncherWindow(const std::string& dir) : inst_(dir) {
        setWindowTitle("mctde Launcher");
        setWindowIcon(QIcon(":/mctde.ico"));
        setFixedSize(524, 346);
        bgDark_.load(":/mctde_bg_dark.bmp");
        bgLight_.load(":/mctde_bg_light.bmp");
        dark_ = inst_.DarkMode();

        auto check = [this](const char* text, int x, int y, int w, int h = 22) {
            auto* c = new Toggle(text, this);
            c->setGeometry(x, y, w, h);
            return c;
        };
        auto button = [this](const char* text, int x, int y, int w, int h, bool primary = false) {
            auto* b = new RoundButton(text, this, primary);
            b->setGeometry(x, y, w, h);
            return b;
        };

        clock_ = new QLabel(this);   clock_->setGeometry(14, 10, 360, 16);
        version_ = new QLabel(this); version_->setGeometry(14, 28, 360, 16);
        phantom_  = check("PhantomUnleashed (uncommon)", 16, 120, 288);
        practice_ = check("Eloise's PTDE Practice Tool", 16, 160, 260);
        dsfix_    = check("DSFix", 16, 200, 130);
        dscm_     = check("Searching for DSCM...", 16, 238, 288);
        darkBox_  = check("Dark Mode", 16, 274, 160, 20);
        // Auto-Play: when on, the launcher counts down from 5 and starts the game by itself.
        autoPlay_ = check("Auto-Play", 300, 272, 100, 20);
        autoUpdate_ = check("Auto-Update", 404, 272, 120, 20);
        static_cast<Toggle*>(autoUpdate_)->contrastLabel = true;   // sits on the banner's inverse-shaded half
        auto* chlog    = button("Changelog", 16, 300, 100, 30);
        auto* settings = button("Settings", 130, 300, 150, 30);
        exit_ = button("Exit", 300, 300, 80, 30);
        play_ = button("PLAY", 404, 296, 100, 40, /*primary=*/true);
        play_->setDefault(true);
        status_ = new QLabel(this);
        status_->setGeometry(310, 238, 204, 26);
        status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        status_->setWordWrap(true);

        // Initial state from disk.
        phantom_->setChecked(inst_.PhantomEnabled());
        if (inst_.DsfixInstalled()) dsfix_->setChecked(inst_.DsfixEnabled());
        else dsfix_->setEnabled(false);   // the launcher never ships DSFix; nothing to toggle
        if (inst_.PracticeInstalled()) {
            practice_->setChecked(inst_.PracticeEnabled());
        } else {
            // Not bundled: greyed switch, and the label becomes a link to the release page.
            practice_->setText(""); practice_->setEnabled(false); practice_->resize(34, 22);
            practiceLink_ = new QLabel(this);
            practiceLink_->setGeometry(54, 161, 230, 20);
            practiceLink_->setOpenExternalLinks(true);
        }
        darkBox_->setChecked(dark_);
        autoUpdate_->setChecked(inst_.AutoUpdate());
        autoPlay_->setChecked(inst_.AutoPlay());
        // Persisted right away; takes effect on the next launch (like Windows).
        connect(autoPlay_, &QCheckBox::toggled, this, [this](bool on) { inst_.SetAutoPlay(on); });
        // Ticking Auto-Update at runtime updates mctde-Link right away (like Windows). The launcher's
        // own self-update stays on the startup path, so a running window never swaps itself mid-use.
        connect(autoUpdate_, &QCheckBox::toggled, this, [this](bool on) {
            inst_.SetAutoUpdate(on);
            if (on && !busy_ && !updating_ && check_.LinkOutdated()) UpdateLinkNow();
        });

        connect(darkBox_, &QCheckBox::toggled, this, [this](bool on) { dark_ = on; ApplyTheme(); });
        connect(play_, &QPushButton::pressed, this, [this] { playWasStop_ = autoActive_; CancelAutoPlay(); });
        connect(chlog, &QPushButton::clicked, this, [this] { ShowChangelog(); });
        connect(settings, &QPushButton::clicked, this, [this] {
            CancelAutoPlay();
            if (OpenSettings(this, inst_, 0)) {
                SyncExeOpener();   // "Require launcher" may have changed
                g_glyphStyle = GlyphStyleFrom(inst_.Get("Launcher", "ControllerIcons", "Xbox"));   // and the icon set
                update();
            }
        });
        connect(exit_, &QPushButton::clicked, this, [this] { SaveSettings(); close(); });
        connect(play_, &QPushButton::clicked, this, [this] {
            if (playWasStop_) { playWasStop_ = false; return; }   // it read STOP: cancelled, don't launch
            Play();
        });

        RefreshVersion();
        Tick();
        auto* clockTimer = new QTimer(this);
        connect(clockTimer, &QTimer::timeout, this, [this] { Tick(); });
        clockTimer->start(1000);

        ApplyTheme();

        // Steam/Proton discovery is file reads only -- fast enough to do up front. Without it
        // PLAY can't work, so say so right away instead of on click.
        envOk_ = steam::Discover(env_, envErr_);
        if (!envOk_) SetStatus("Can't play yet", true);

        // DSCM: find it (running instance, remembered path, or DATA folder); keep polling while
        // unknown, and auto-start it in the game's prefix when enabled -- like the Windows launcher.
        dscmPoll_ = new QTimer(this);
        connect(dscmPoll_, &QTimer::timeout, this, [this] { RefreshDscm(); });
        RefreshDscm();
        if (!dscmReady_) dscmPoll_->start(2000);
        else if (envOk_ && inst_.AutoLaunchDscm() && launch::FindRunningDscm().empty()) StartDscm();
        connect(dscm_, &QCheckBox::toggled, this, [this](bool on) {
            if (on && dscmReady_ && envOk_ && !busy_ && launch::FindRunningDscm().empty()) StartDscm();
        });

        StartUpdateCheck();
        SyncExeOpener();
        if (getenv("MCTDE_TEST_DSCM_BEHIND")) KeepDscmBehind();   // dev aid: test without a real DSCM
        play_->setFocus();
        SetupController(chlog, settings);
        autoTimer_ = new QTimer(this);
        connect(autoTimer_, &QTimer::timeout, this, [this] { AutoPlayTick(); });
        qApp->installEventFilter(this);
    }

    void ClickPlay() { play_->click(); }

    // ---- Auto-Play: count down from 5 on launch, then PLAY. While counting down the PLAY button
    // reads "STOP (n)"; pressing it, or any other key or click, stops the countdown and leaves the
    // launcher open (no countdown again until the next start). Skipped when the game is already
    // running, so it can never stack a second instance.
    void StartAutoPlay() {
        if (!inst_.AutoPlay() || launch::GameRunning()) return;
        autoActive_ = true; autoCount_ = 5;
        play_->setText(QString("STOP (%1)").arg(autoCount_));
        autoTimer_->start(1000);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const QPixmap& bg = dark_ ? bgDark_ : bgLight_;
        if (!bg.isNull()) p.drawPixmap(0, 0, bg);
        else p.fillRect(rect(), ThemeFor(dark_).bg);
    }
    bool event(QEvent* e) override {
        if (e->type() == QEvent::WindowDeactivate && dscmGuardUntil_ &&
            QDateTime::currentMSecsSinceEpoch() < dscmGuardUntil_ && !busy_)
            QTimer::singleShot(0, this, [this] { ReclaimFocus(); });
        return QWidget::event(e);
    }
    bool eventFilter(QObject* obj, QEvent* e) override {
        // Any key or click stops the countdown. A press on PLAY itself is handled by its pressed()
        // signal (so the release doesn't launch); keys are swallowed so Enter/Space can't launch.
        if (autoActive_) {
            if (e->type() == QEvent::KeyPress) { CancelAutoPlay(); return true; }
            if (e->type() == QEvent::MouseButtonPress && obj != play_) CancelAutoPlay();
        }
        return QWidget::eventFilter(obj, e);
    }
    void closeEvent(QCloseEvent* e) override {
        if (busy_) { e->ignore(); return; }   // mid-launch: let PLAY finish or report first
        e->accept();
    }

private:
    void ApplyTheme() {
        g_dark = dark_;
        Theme t = ThemeFor(dark_);
        auto hex = [](const QColor& c) { return c.name(); };
        // Toggles and buttons paint themselves (Toggle / RoundButton); this covers labels and menus.
        setStyleSheet(QString(
            "QLabel { color: %1; background: transparent; }"
            "QLabel:disabled { color: %6; }"
            "QMenu { color: %1; background: %4; border: 1px solid %3; }"
            "QMenu::item:selected { background: %2; }"
            "QMenu::item:disabled { color: %6; }")
            .arg(hex(t.text), hex(t.btn), hex(t.border), hex(t.field), hex(t.accent), hex(t.disabled)));
        if (practiceLink_)   // the download link stays blue, like the Windows launcher's
            practiceLink_->setText(QString("<a style=\"color:%1\" href=\"%2\">Eloise's PTDE Practice Tool</a>")
                                       .arg(dark_ ? "#5aa0fa" : "#005ac8", mctde::PRACTICE_RELEASE_URL));
        for (QWidget* w : findChildren<QWidget*>()) w->update();
        update();
    }

    void CancelAutoPlay() {
        if (!autoActive_) return;
        autoActive_ = false;
        autoTimer_->stop();
        play_->setText("PLAY");
    }
    void AutoPlayTick() {
        if (!autoActive_) return;
        if (--autoCount_ <= 0) {
            autoActive_ = false;
            autoTimer_->stop();
            play_->setText("PLAY");
            Play();   // save toggles + launch (waits for DSCM / an update still in flight)
            return;
        }
        play_->setText(QString("STOP (%1)").arg(autoCount_));
    }

    void Tick() {
        QLocale loc;
        QDateTime now = QDateTime::currentDateTime();
        clock_->setText(loc.toString(now.date(), QLocale::LongFormat) + "    " +
                        loc.toString(now.time(), QLocale::ShortFormat));
    }

    void RefreshVersion() {
        std::string link = PeFileVersion(inst_.Path("d3d9.dll"));
        if (link.empty()) link = "?";
        QString s = QString("Launcher %1 (Linux preview)      Link %2")
                        .arg(MCTDE_LINUX_VERSION, QString::fromStdString(link));
        if (check_.LauncherOutdated() || check_.LinkOutdated()) s += "   (update available)";
        version_->setText(s);
    }

    // ---- auto-update (linux/update.h): mctde-Link + this launcher, in the background at startup
    void StartUpdateCheck() {
        updating_ = true;
        const bool autoUpdate = inst_.AutoUpdate();
        RunAsync<update::Outcome>(this,
            [this, autoUpdate] { return update::Run(inst_, ExeDir(), autoUpdate); },
            [this](update::Outcome o) {
                updating_ = false;
                check_ = o.check;
                RefreshVersion();
                if (!o.error.empty()) { SetStatus("Update failed", true); status_->setToolTip(QString::fromStdString(o.error)); }
                if (o.launcherUpdated) {
                    if (!busy_) {
                        // New binaries are in place: start the new version and step aside.
                        SaveSettings();
                        if (update::Relaunch(ExeDir() + "/mctde-launcher", inst_.Dir())) { QApplication::quit(); return; }
                    }
                    SetStatus("Updated: restart the launcher");
                }
                ResumePendingPlay();
            });
    }

    void UpdateLinkNow() {
        updating_ = true;
        SetStatus("Updating mctde-Link...");
        RunAsync<std::string>(this,
            [this] { std::string err; return update::UpdateLink(inst_, err) ? std::string() : err; },
            [this](std::string err) {
                updating_ = false;
                check_.installedLink = PeFileVersion(inst_.Path("d3d9.dll"));
                RefreshVersion();
                SetStatus(err.empty() ? "" : "Update failed", !err.empty());
                if (!err.empty()) status_->setToolTip(QString::fromStdString(err));
                ResumePendingPlay();
            });
    }

    // PLAY waits for anything that's still touching the install (DSCM start-up, an update writing
    // d3d9.dll) and runs once the last of them finishes.
    bool Blocked() const { return dscmStarting_ || updating_; }
    void ResumePendingPlay() { if (pendingPlay_ && !Blocked()) { pendingPlay_ = false; Play(); } }

    void SetStatus(const QString& s, bool warn = false) {
        status_->setText(s);
        status_->setStyleSheet(warn ? "color: #d05050;" : "");
    }

    void RefreshDscm() {
        bool was = dscmReady_;
        dscmReady_ = envOk_ && !launch::DscmLaunchPath(env_, inst_).empty();
        dscm_->setEnabled(dscmReady_);
        dscm_->setText(dscmReady_ ? "Auto-Launch DSCM" : "Searching for DSCM...");
        QSignalBlocker block(dscm_);
        dscm_->setChecked(dscmReady_ && inst_.AutoLaunchDscm());
        if (dscmReady_ && !was && dscmPoll_) dscmPoll_->stop();
    }

    void StartDscm() {
        std::string path = launch::DscmLaunchPath(env_, inst_);
        if (path.empty()) return;
        dscmStarting_ = true;
        SetStatus("Starting DSCM...");
        KeepDscmBehind();
        RunAsync<launch::Result>(this,
            [this, path] { return launch::LaunchDscm(env_, inst_, path); },
            [this](launch::Result r) {
                dscmStarting_ = false;
                if (r.started && !kdeBehind_) ArmDscmBehind();
                SetStatus(r.started ? "" : "DSCM didn't start", !r.started);
                if (!r.started) QMessageBox::warning(this, "mctde Launcher", QString::fromStdString(r.error));
                ResumePendingPlay();
            });
    }

    // DSCM opens behind the launcher, like on Windows. On KDE a short-lived KWin script does it
    // (linux/kwin_behind.h), loaded before DSCM starts so it sees DSCM's window appear.
    void KeepDscmBehind() {
        kdeBehind_ = getenv("XDG_CURRENT_DESKTOP") && strstr(getenv("XDG_CURRENT_DESKTOP"), "KDE");
        if (kdeBehind_) std::thread([] { kwin::KeepDscmBehindLauncher(60); }).detach();
    }
    // Elsewhere, best effort: for a while after DSCM starts, the FIRST time the launcher loses focus
    // (DSCM's window appearing), it asks for focus back, retrying for ~1.5 s. Only once, so clicking
    // DSCM later just works. (Compositors may refuse this without fresh input; KWin does.)
    void ArmDscmBehind() {
        dscmGuardUntil_ = QDateTime::currentMSecsSinceEpoch() + 20000;
        if (!isActiveWindow()) ReclaimFocus();   // DSCM already grabbed it while we waited
    }
    void ReclaimFocus() {
        dscmGuardUntil_ = 0;
        auto ticks = std::make_shared<int>(0);
        auto* t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this, t, ticks] {
            if (busy_ || ++*ticks > 10) { t->deleteLater(); return; }   // PLAY took over, or done
            if (!isActiveWindow()) { raise(); activateWindow(); }
        });
        t->start(150);
        raise(); activateWindow();
    }

    // Persist every toggle (same set the Windows launcher writes on PLAY / Exit).
    void SaveSettings() {
        inst_.SetPhantom(phantom_->isChecked());
        inst_.SetDarkMode(dark_);
        if (dsfix_->isEnabled()) inst_.ApplyDsfix(dsfix_->isChecked());
        if (inst_.PracticeInstalled()) inst_.ApplyPractice(practice_->isChecked());
        // Only persist DSCM when detected -- don't clobber a saved "on" while it's undetected.
        if (dscm_->isEnabled()) inst_.SetAutoLaunchDscm(dscm_->isChecked());
        inst_.SetAutoUpdate(autoUpdate_->isChecked());
        inst_.SetAutoPlay(autoPlay_->isChecked());
    }

    void SetBusy(bool b) {
        busy_ = b;
        for (QWidget* w : findChildren<QWidget*>())
            if (w != clock_ && w != version_ && w != status_) w->setEnabled(!b);
        if (!b) { // restore the not-installed greys
            if (!inst_.DsfixInstalled()) dsfix_->setEnabled(false);
            if (!inst_.PracticeInstalled()) practice_->setEnabled(false);
            dscm_->setEnabled(dscmReady_);
        }
    }

    void Play() {
        SaveSettings();
        if (!envOk_) {
            QMessageBox::critical(this, "mctde Launcher", QString::fromStdString(envErr_));
            return;
        }
        SetBusy(true);
        play_->setText("...");
        if (Blocked()) {
            pendingPlay_ = true;
            SetStatus(dscmStarting_ ? "Waiting for DSCM..." : "Checking for updates...");
            return;
        }
        SetStatus(steam::ClientRunning() ? "Starting Dark Souls..." : "Starting Steam...");
        RunAsync<launch::Result>(this,
            [this] { return launch::PlayGame(env_, inst_, 15); },
            [this](launch::Result r) {
                if (r.started && !(r.exitedEarly && r.exitCode != 0)) { busy_ = false; close(); return; }
                SetBusy(false);
                play_->setText("PLAY");
                SetStatus("Launch failed", true);
                QMessageBox::critical(this, "mctde Launcher", QString::fromStdString(r.error));
            });
    }

    // ---- controller (linux/gamepad.h): the Windows launcher's explicit D-pad map for this window
    // -- the left column of toggles into the Changelog / Settings / Exit row, then the Auto-Play /
    // Auto-Update / PLAY / Exit loop -- skipping anything hidden or disabled (DSFix / Practice when
    // not installed). Start presses PLAY; any input during the Auto-Play countdown cancels it.
    void SetupController(QPushButton* chlog, QPushButton* settings) {
        g_glyphStyle = GlyphStyleFrom(inst_.Get("Launcher", "ControllerIcons", "Xbox"));
        struct Node { QWidget *self, *up, *down, *left, *right; };
        auto nodes = std::make_shared<std::vector<Node>>(std::vector<Node>{
            { phantom_,    nullptr,     practice_,  nullptr,     nullptr     },
            { practice_,   phantom_,    dsfix_,     nullptr,     nullptr     },
            { dsfix_,      practice_,   dscm_,      nullptr,     nullptr     },
            { dscm_,       dsfix_,      darkBox_,   nullptr,     nullptr     },
            { darkBox_,    dscm_,       chlog,      nullptr,     nullptr     },
            { chlog,       darkBox_,    nullptr,    nullptr,     settings    },
            { settings,    nullptr,     nullptr,    chlog,       exit_       },
            { exit_,       autoPlay_,   nullptr,    settings,    play_       },
            { autoPlay_,   nullptr,     exit_,      nullptr,     autoUpdate_ },
            { autoUpdate_, nullptr,     play_,      autoPlay_,   nullptr     },
            { play_,       autoUpdate_, nullptr,    exit_,       nullptr     },
        });
        auto* nav = new pad::Navigator(this);
        nav->mainNav = [this, nodes](int dir) {
            QWidget* cur = QApplication::focusWidget();
            for (int guard = 0; guard < 16; ++guard) {
                const Node* n = nullptr;
                for (const Node& x : *nodes) if (x.self == cur) n = &x;
                if (!n) { play_->setFocus(Qt::TabFocusReason); return; }   // off the map: back to PLAY
                QWidget* next = dir == 0 ? n->up : dir == 1 ? n->down : dir == 2 ? n->left : n->right;
                if (!next) return;                                          // edge of the map
                if (next->isVisible() && next->isEnabled()) { next->setFocus(Qt::TabFocusReason); return; }
                cur = next;                                                 // skip a dead control
            }
        };
        nav->onAnyInput = [this] { if (!autoActive_) return false; CancelAutoPlay(); return true; };
        nav->onStart = [this] { if (!busy_) play_->click(); };
    }

    // Keep the desktop's .exe opener (linux/exe_opener.h) in step with [Launcher] RequireLauncher,
    // so double-clicking DARKSOULS.exe opens this launcher -- the Linux side of the Windows
    // launcher guard. Runs in the background: xdg-mime and the KDE cache rebuild take a moment.
    void SyncExeOpener() {
        if (getenv("MCTDE_NO_INTEGRATE")) return;
        const bool want = inst_.GetInt("Launcher", "RequireLauncher", 1) != 0;
        RunAsync<std::string>(this, [want] {
            std::string err;
            if (want != opener::Installed()) (want ? opener::Install(err) : opener::Uninstall(err));
            return err;
        }, [this](std::string err) {
            if (!err.empty()) status_->setToolTip(QString::fromStdString("Couldn't set up the .exe opener: " + err));
        });
    }

    // Actual on-disk name of a file in the DATA folder (installs mix cases, e.g. DSfix.ini).
    std::string ResolvedIn(const char* name) const {
        QDir d(QString::fromStdString(inst_.Dir()));
        for (const QString& f : d.entryList(QDir::Files))
            if (f.compare(name, Qt::CaseInsensitive) == 0) return d.filePath(f).toStdString();
        return "";
    }

    void ShowChangelog() {
        QDialog dlg(this);
        dlg.setWindowTitle("Changelog");
        dlg.resize(640, 520);
        auto* tabs = new QTabWidget(&dlg);
        auto addTab = [tabs](const char* res, const char* title) {
            QFile f(res);
            auto* tb = new QTextBrowser;
            tb->setOpenExternalLinks(true);
            if (f.open(QIODevice::ReadOnly)) tb->setMarkdown(QString::fromUtf8(f.readAll()));
            tabs->addTab(tb, title);
        };
        addTab(":/CHANGELOG-LINUX.md", "Launcher");   // this (Linux) launcher's own history
        addTab(":/mctde-Link.CHANGELOG.md", "mctde-Link");
        auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
        connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        auto* lay = new QVBoxLayout(&dlg);
        lay->addWidget(tabs);
        lay->addWidget(box);
        dlg.exec();
    }

    mctde::Install inst_;
    steam::Env env_;
    bool envOk_ = false;
    std::string envErr_;
    bool dark_ = false;
    std::atomic<bool> busy_{false};
    bool dscmReady_ = false, dscmStarting_ = false, pendingPlay_ = false, updating_ = false;
    update::Check check_;
    QPixmap bgDark_, bgLight_;
    QLabel *clock_ = nullptr, *version_ = nullptr, *status_ = nullptr, *practiceLink_ = nullptr;
    QCheckBox *phantom_ = nullptr, *practice_ = nullptr, *dsfix_ = nullptr, *dscm_ = nullptr, *darkBox_ = nullptr,
              *autoUpdate_ = nullptr, *autoPlay_ = nullptr;
    QPushButton *play_ = nullptr, *exit_ = nullptr;
    QTimer *dscmPoll_ = nullptr, *autoTimer_ = nullptr;
    bool autoActive_ = false, playWasStop_ = false;
    qint64 dscmGuardUntil_ = 0;   // ArmDscmBehind window (ms since epoch), 0 = off
    bool kdeBehind_ = false;      // the KWin script handles DSCM-behind
    int autoCount_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    // Any argument other than --dir <folder> means the command line.
    bool cli = argc > 1 && !(argc == 3 && std::string(argv[1]) == "--dir");
    if (cli) return RunCli(argc, argv);

    QApplication app(argc, argv);
    QApplication::setApplicationName("mctde Launcher");
    QApplication::setDesktopFileName("mctde-launcher");
    std::string dir = argc == 3 ? argv[2] : ExeDir();

    mctde::Install inst(dir);
    bool wasReset = inst.EnsureIniSeeded();   // before anything reads or writes a key

    if (!plat::FileExists(inst.Path("DARKSOULS.exe")) && !plat::FileExists(inst.Path("DATA.exe")))
        QMessageBox::warning(nullptr, "mctde Launcher",
            "DARKSOULS.exe isn't in this folder:\n" + QString::fromStdString(inst.Dir()) +
            "\n\nPut mctde-launcher in your Dark Souls PTDE DATA folder.");

    LauncherWindow w(dir);
    w.show();
    // Developer aid: MCTDE_SCREENSHOT=<file.png> renders the window to a PNG and exits (works
    // with QT_QPA_PLATFORM=offscreen, so the UI can be checked without a desktop session).
    // MCTDE_SCREENSHOT_MS sets the delay (default 300); Auto-Play runs, so keep it under 5000.
    if (const char* shot = getenv("MCTDE_SCREENSHOT")) {
        const char* ms = getenv("MCTDE_SCREENSHOT_MS");
        w.StartAutoPlay();
        QTimer::singleShot(ms ? atoi(ms) : 300, &w, [&w, shot] { w.grab().save(shot); QApplication::quit(); });
        return app.exec();
    }
    // Developer aid: MCTDE_SETTINGS_SHOT=<file.png> opens Settings, renders it and exits
    // (MCTDE_SETTINGS_TAB=0|1 picks the tab, MCTDE_SETTINGS_ADV=1 turns on Advanced Options).
    if (getenv("MCTDE_SETTINGS_SHOT")) {
        QTimer::singleShot(200, &w, [&w, &inst] { OpenSettings(&w, inst, 0); QApplication::quit(); });
        return app.exec();
    }
    // Developer aid: MCTDE_AUTOCLICK=play presses PLAY once the window is up (scripted testing).
    if (const char* click = getenv("MCTDE_AUTOCLICK"))
        if (std::string(click) == "play") QTimer::singleShot(500, &w, [&w] { w.ClickPlay(); });
    if (wasReset)
        QMessageBox::information(&w, "Settings reset", "This update reset your settings to new defaults.");
    w.StartAutoPlay();   // if Auto-Play is on, begin the 5s countdown (any input cancels it)
    return app.exec();
}
