// linux/settings.cpp -- the tabbed Settings window. See settings.h.
//
// A straight port of the Windows launcher's Settings host (mctde_launcher.cpp: OpenSettings,
// DsfixWndProc / DsfixApplySave, McDialogProc / McSave / McPreviewProc): same fields, defaults,
// basic/advanced split, save rules and pixel layout, drawn with the same palette and controls.
#include "settings.h"

#include "../core/platform.h"
#include "gamepad.h"
#include "theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace ui;
using mctde::DsfixGet;
using mctde::DsfixSet;

namespace {

const int SET_CW = 500;                                   // client width of the window and panels
const int SET_PANELY = 42, SET_BTNBAR = 44, SET_TOPCAP = 680;

QString Q(const std::string& s) { return QString::fromStdString(s); }
std::string S(const QString& s) { return s.toStdString(); }

// Actual on-disk name of a file in the DATA folder (installs mix cases, e.g. DSfix.ini).
QString ResolvedIn(const std::string& dir, const char* name) {
    QDir d(Q(dir));
    for (const QString& f : d.entryList(QDir::Files))
        if (f.compare(name, Qt::CaseInsensitive) == 0) return d.filePath(f);
    return d.filePath(name);
}
void OpenInEditor(const QString& path) { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }

QString StyleSheet() {
    Theme t = ThemeFor(g_dark);
    QColor border = Alpha(t.border, 170);
    auto c = [](const QColor& col) {
        return QString("rgba(%1,%2,%3,%4)").arg(col.red()).arg(col.green()).arg(col.blue()).arg(col.alpha());
    };
    QColor selText = g_dark ? QColor(28, 22, 10) : QColor(255, 250, 242);
    return QString(
        "QDialog { background: %1; }"
        "QLabel { color: %2; background: transparent; }"
        "QLabel:disabled { color: %7; }"
        "QLabel#hdr { color: %5; font-weight: 600; }"
        "QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }"
        "QLineEdit { color: %2; background: %3; border: 1px solid %4; border-radius: 5px; padding: 1px 6px;"
        "            selection-background-color: %5; selection-color: %8; }"
        "QLineEdit:focus { border: 2px solid %5; padding: 0px 5px; }"
        "QLineEdit:disabled { color: %7; background: %6; }"
        "QComboBox QAbstractItemView { color: %2; background: %3; border: 1px solid %4; outline: 0;"
        "            selection-background-color: %5; selection-color: %8; }"
        "QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: %4; }"
        "QSlider::sub-page:horizontal { height: 4px; border-radius: 2px; background: %5; }"
        "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -6px 0; border-radius: 7px;"
        "            background: #ffffff; border: 1px solid %4; }"
        "QSlider::handle:horizontal:focus { border: 2px solid %5; }"
        "QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: %5; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }")
        .arg(t.bg.name(), t.text.name(), t.field.name(), c(border), t.accent.name(), t.btn.name(),
             t.disabled.name(), selText.name());
}

// The flat dropdown (the Windows launcher's ComboProc): field fill, text, chevron, rounded border.
// The closed field shows the selected item in its own font when it has one (the font picker).
class FlatCombo : public QComboBox {
public:
    explicit FlatCombo(QWidget* parent) : QComboBox(parent) { setFocusPolicy(Qt::StrongFocus); setMaxVisibleItems(15); }
protected:
    void paintEvent(QPaintEvent*) override {
        Theme t = ThemeFor(g_dark);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool en = isEnabled(), focused = hasFocus();
        QRectF r(rect());
        p.setPen(Qt::NoPen);
        p.setBrush(en ? t.field : t.btn);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        QVariant fv = currentIndex() >= 0 ? itemData(currentIndex(), Qt::FontRole) : QVariant();
        QFont f = fv.isValid() ? fv.value<QFont>() : font();
        if (fv.isValid()) f.setPixelSize(std::min(f.pixelSize() > 0 ? f.pixelSize() : 15, height() - 6));
        p.setFont(f);
        p.setPen(en ? t.text : t.disabled);
        QRect tr = rect().adjusted(9, 0, -22, 0);
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(f).elidedText(currentText(), Qt::ElideRight, tr.width()));
        QPen cp(Alpha(t.text, 190), 1.6);
        cp.setCapStyle(Qt::RoundCap);
        p.setPen(cp);
        qreal cx = width() - 13.0, cy = height() / 2.0 - 1.0;
        p.drawLine(QPointF(cx - 4, cy - 1), QPointF(cx, cy + 3));
        p.drawLine(QPointF(cx, cy + 3), QPointF(cx + 4, cy - 1));
        StrokeRound(p, r, 5, focused ? t.accent : Alpha(t.border, 170), focused ? 2 : 1);
    }
};

// A pill tab button (the Windows launcher's SettingsDrawTab).
class TabButton : public QPushButton {
public:
    TabButton(const QString& text, QWidget* parent) : QPushButton(text, parent) { setCursor(Qt::PointingHandCursor); }
    bool active = false;
protected:
    void paintEvent(QPaintEvent*) override {
        Theme t = ThemeFor(g_dark);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(Qt::NoPen);
        p.setBrush(active ? t.accent : t.btn);
        p.drawRoundedRect(r, 7, 7);
        if (!active) StrokeRound(p, QRectF(rect()), 7, Alpha(t.border, 150), 1);
        bool focused = hasFocus();
        if (focused) StrokeRound(p, QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 6,
                                 active ? (g_dark ? QColor(255, 235, 170) : QColor(255, 255, 255)) : t.accent, 1.8);
        p.setPen(active ? (g_dark ? QColor(28, 22, 10) : QColor(255, 250, 242)) : (focused ? t.accent : t.text));
        p.drawText(rect(), Qt::AlignCenter, text());
        if (focused && g_padConnected) DrawGlyph(p, QRectF(width() - 13 - 9, (height() - 18) / 2.0, 18, 18));
    }
};

// Dropdowns and sliders never take the mouse wheel (it scrolls the page instead), so scrolling
// down a long page can't silently change a value. An open dropdown list still scrolls itself.
class WheelToPage : public QObject {
public:
    WheelToPage(QScrollArea* area, QObject* parent) : QObject(parent), area_(area) {}
protected:
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() != QEvent::Wheel || !area_) return false;
        QApplication::sendEvent(area_->verticalScrollBar(), e);
        return true;
    }
private:
    QPointer<QScrollArea> area_;
};

// ---- small widget factories (parent = a page's scrolling content widget)
QLabel* Label(QWidget* p, const QString& text) { return new QLabel(text, p); }
QLabel* Header(QWidget* p, const QString& text) {
    auto* l = new QLabel(text, p);
    l->setObjectName("hdr");
    return l;
}
QLineEdit* Edit(QWidget* p, const std::string& v) { return new QLineEdit(Q(v), p); }
Toggle* Check(QWidget* p, const QString& text, bool on) {
    auto* c = new Toggle(text, p);
    c->setChecked(on);
    return c;
}
// A dropdown whose item text is also the stored value.
FlatCombo* Combo(QWidget* p, const std::vector<QString>& items, const QString& cur) {
    auto* c = new FlatCombo(p);
    int sel = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        c->addItem(items[i], items[i]);
        if (items[i].compare(cur, Qt::CaseInsensitive) == 0) sel = int(i);
    }
    c->setCurrentIndex(sel);
    return c;
}
// A dropdown whose visible label differs from the value stored in the ini.
FlatCombo* Mapped(QWidget* p, const std::vector<QString>& labels, const std::vector<QString>& values, const QString& cur) {
    auto* c = new FlatCombo(p);
    int sel = 0;
    for (size_t i = 0; i < labels.size(); ++i) {
        c->addItem(labels[i], values[i]);
        if (values[i] == cur) sel = int(i);
    }
    c->setCurrentIndex(sel);
    return c;
}
std::string Value(QComboBox* c) { return S(c->currentData().toString()); }
void SelectLabel(QComboBox* c, const QString& label) {
    for (int i = 0; i < c->count(); ++i)
        if (c->itemText(i).compare(label, Qt::CaseInsensitive) == 0) { c->setCurrentIndex(i); return; }
}
QSlider* Slider(QWidget* p, int lo, int hi, int v) {
    auto* s = new QSlider(Qt::Horizontal, p);
    s->setRange(lo, hi);
    s->setValue(std::clamp(v, lo, hi));
    s->setFocusPolicy(Qt::StrongFocus);
    return s;
}
std::string On(QCheckBox* c) { return c->isChecked() ? "1" : "0"; }

// A page's scrolling body: absolute-positioned content inside a QScrollArea, re-laid out by a
// callback that places every control for a given width and returns the content height.
struct Scroller {
    QScrollArea* area = nullptr;
    QWidget* content = nullptr;
    std::function<int(int)> layout;

    void Build(QWidget* parent) {
        area = new QScrollArea(parent);
        area->setFrameShape(QFrame::NoFrame);
        area->setWidgetResizable(false);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        content = new QWidget;
        area->setWidget(content);
        area->verticalScrollBar()->setSingleStep(24);
    }
    // Full-width content height (what the window sizes itself to).
    int ContentHeight() const { return layout(SET_CW); }
    // Fit the content to the viewport, showing the scrollbar only when it overflows.
    void Relayout() {
        int w = area->width(), h = layout(w);
        bool overflow = h > area->height();
        if (overflow) h = layout(w - area->verticalScrollBar()->sizeHint().width());
        area->setVerticalScrollBarPolicy(overflow ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
        content->resize(overflow ? w - area->verticalScrollBar()->sizeHint().width() : w, h);
    }
    void GuardWheel(QWidget* w) { w->installEventFilter(new WheelToPage(area, w)); }
};

// Show/hide + place in one call (y is content-relative).
void Put(QWidget* w, int x, int y, int wd, int ht = 18) { w->setGeometry(x, y, wd, ht); w->show(); }

// ============================================================ DSFix tab
const std::vector<QString> AAQ_LBL  = { "Low", "Medium", "High", "Ultra" };
const std::vector<QString> AAQ_VAL  = { "1", "2", "3", "4" };
const std::vector<QString> SSAO_LBL = { "Off", "Medium", "High", "Ultra" };
const std::vector<QString> SSAO_VAL = { "0", "1", "2", "3" };
const std::vector<QString> FILT_LBL = { "None", "Bilinear", "Anisotropic" };
const std::vector<QString> FILT_VAL = { "0", "1", "2" };
// DSFix logLevel 0-11 (higher = more per-frame disk I/O). 0 = off, recommended for play.
const std::vector<QString> LOG_LBL = { "Off (0)", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11" };
const std::vector<QString> LOG_VAL = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11" };

// DLLs in the DATA folder that DSFix's dinput8dllWrapper could chainload ("none" leads): skips
// DSFix's own dinput8.dll, the d3d9.dll proxy and DLLs core to the vanilla game.
std::vector<QString> DetectChainDlls(const std::string& dir) {
    static const char* core[] = { "dinput8.dll", "d3d9.dll", "fmodex.dll", "fmod_event.dll", "steam_api.dll", "steam_api64.dll" };
    std::vector<QString> out = { "none" };
    QStringList files = QDir(Q(dir)).entryList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString& f : files) {
        if (!f.endsWith(".dll", Qt::CaseInsensitive)) continue;
        bool isCore = false;
        for (const char* c : core) if (f.compare(c, Qt::CaseInsensitive) == 0) isCore = true;
        if (!isCore) out.push_back(f);
    }
    return out;
}

class DsfixPage : public QWidget {
public:
    DsfixPage(QWidget* parent, const mctde::Install& inst, std::function<void()> refit)
        : QWidget(parent), inst_(inst), refit_(std::move(refit)) {
        sc_.Build(this);
        QWidget* c = sc_.content;
        text_ = plat::ReadText(inst_.DsfixIniPath());
        auto G = [&](const char* k, const char* d) { return DsfixGet(text_, k, d); };
        hudEn_ = G("enableHudMod", "0"); hudMin_ = G("enableMinimalHud", "0");
        hudScale_ = G("hudScaleFactor", "1.0"); hudOpacity_ = G("hudOpacity", "1.0");

        adv = Check(c, "Enable Advanced Options", false);
        editIni = new RoundButton("Edit dsfix.ini", c);
        hdrVideo = Header(c, "Video");
        lblRes = Label(c, "Internal resolution (W x H)"); rw = Edit(c, G("renderWidth", "1920"));
        resX = Label(c, "x");                             rh = Edit(c, G("renderHeight", "1080"));
        lblOut = Label(c, "Output resolution (W x H)");   pw = Edit(c, G("presentWidth", "1920"));
        outX = Label(c, "x");                             ph = Edit(c, G("presentHeight", "1080"));
        // Show the effective AA state: without the opt-in the launch-time force-off keeps it none.
        std::string aaInit = inst_.AaOptIn() ? G("aaType", "SMAA") : std::string("none");
        lblAA = Label(c, "AA");          aaType = Combo(c, { "none", "SMAA", "FXAA" }, Q(aaInit));
        lblAAQ = Label(c, "AA Quality"); aaQual = Mapped(c, AAQ_LBL, AAQ_VAL, Q(G("aaQuality", "4")));
        aaQual->setEnabled(aaInit != "none");
        lblSSAO = Label(c, "SSAO");      ssao = Mapped(c, SSAO_LBL, SSAO_VAL, Q(G("ssaoStrength", "0")));
        lblFilter = Label(c, "Texture Filtering"); filter = Mapped(c, FILT_LBL, FILT_VAL, Q(G("filteringOverride", "0")));
        border = Check(c, "Borderless Fullscreen", G("borderlessFullscreen", "0") == "1");
        hudMod = Check(c, "HUD Mod (opens options)", hudEn_ == "1");
        hdrOther = Header(c, "Other");
        skipIntro = Check(c, "Skip intro", G("skipIntro", "1") == "1");
        lblLog = Label(c, "Log Level"); logLvl = Mapped(c, LOG_LBL, LOG_VAL, Q(G("logLevel", "6")));
        hdrCursor = Header(c, "Cursor");
        capCur = Check(c, "Capture cursor", G("captureCursor", "1") == "1");
        disCur = Check(c, "Disable cursor", G("disableCursor", "1") == "1");
        // Dinput DLL chaining: the first holds DSFix's dinput8dllWrapper; extras are dinputChain1..9.
        hdrDinput = Header(c, "Dinput DLL Chaining");
        std::vector<QString> dlls = DetectChainDlls(inst_.Dir());
        for (int i = 0; i < 10; ++i) {
            std::string cur = i == 0 ? G("dinput8dllWrapper", "none") : G(("dinputChain" + std::to_string(i)).c_str(), "none");
            std::vector<QString> items = dlls;   // keep a configured-but-absent DLL selectable
            bool present = false;
            for (auto& s : items) if (s.compare(Q(cur), Qt::CaseInsensitive) == 0) present = true;
            if (!present && !cur.empty()) items.push_back(Q(cur));
            dinput[i] = Combo(c, items, Q(cur));
        }
        hdrDof = Header(c, "Depth of Field");
        lblDofRes = Label(c, "DOF override resolution"); dofRes = Edit(c, G("dofOverrideResolution", "540"));
        disDofScale = Check(c, "Disable DOF scaling", G("disableDofScaling", "1") == "1");
        lblDofBlur = Label(c, "DOF additional blur"); dofBlur = Edit(c, G("dofBlurAmount", "1"));
        defDof = Check(c, "Original DoF", false);
        hdrFps = Header(c, "Framerate");
        unlockFps = Check(c, "Unlock FPS", G("unlockFPS", "1") == "1");
        lblFpsLimit = Label(c, "FPS limit"); fpsLimit = Edit(c, G("FPSlimit", "30"));
        lblFpsThresh = Label(c, "FPS threshold"); fpsThresh = Edit(c, G("FPSthreshold", "28"));
        fpsStab = Check(c, "FPS Stabilizer (causes microstutters)", G("unlockFPS", "1") != "0");
        fpsStabInit_ = fpsStab->isChecked();
        defDofInit_ = defDof->isChecked();
        hdrBackup = Header(c, "Save Game Backup Options");
        enBackup = Check(c, "Enable backups", G("enableBackups", "1") == "1");
        lblBkpInt = Label(c, "Backup interval"); bkpInt = Edit(c, G("backupInterval", "10"));
        lblBkpMax = Label(c, "Max backups");     bkpMax = Edit(c, G("maxBackups", "10"));
        hdrNotReady = Header(c, "Not ready to use");
        forceWin = Check(c, "ForceWindowed/ForceFullscreen", G("forceWindowed", "0") == "1");
        enVsync = Check(c, "Enable Vsync", G("enableVsync", "0") == "1");
        lblFsHz = Label(c, "Fullscreen Hz"); fsHz = Edit(c, G("fullscreenHz", "60"));
        credit = Label(c, "DSFix by Durante - GPL-3.0. Source: github.com/PeterTh/dsfix");
        revert = new RoundButton("Revert to Defaults", c);

        for (QComboBox* cb : { (QComboBox*)aaType, (QComboBox*)aaQual, (QComboBox*)ssao, (QComboBox*)filter, (QComboBox*)logLvl })
            sc_.GuardWheel(cb);
        for (auto* cb : dinput) sc_.GuardWheel(cb);
        sc_.layout = [this](int w) { return Layout(w); };

        connect(adv, &QCheckBox::clicked, this, [this](bool on) {
            if (on && !adv_) {
                if (QMessageBox::warning(this, "Enable Advanced Options",
                        "Experimenting with advanced features could result in unpredictable behavior.",
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
                    adv->setChecked(false);
                    return;
                }
                adv_ = true;
            } else if (!on) {
                adv_ = false;
            }
            sc_.area->verticalScrollBar()->setValue(0);
            refit_();
        });
        connect(editIni, &QPushButton::clicked, this, [this] { OpenInEditor(ResolvedIn(inst_.Dir(), "dsfix.ini")); });
        connect(aaType, QOverload<int>::of(&QComboBox::activated), this, [this] { aaQual->setEnabled(aaType->currentText() != "none"); });
        for (auto* cb : dinput) connect(cb, QOverload<int>::of(&QComboBox::activated), this, [this] { refit_(); });
        connect(hudMod, &QCheckBox::clicked, this, [this](bool on) {
            if (on) { OpenHudConfig(); hudMod->setChecked(hudEn_ == "1"); }
            else hudEn_ = "0";
        });
        // The basic "FPS Stabilizer" and advanced "Unlock FPS" are two faces of unlockFPS: keep them
        // in lockstep so neither shows a stale value and silently wins on save.
        connect(fpsStab, &QCheckBox::clicked, this, [this](bool on) { unlockFps->setChecked(on); fpsLimit->setText("30"); });
        connect(unlockFps, &QCheckBox::clicked, this, [this](bool on) { fpsStab->setChecked(on); });
        connect(revert, &QPushButton::clicked, this, [this] { Revert(); });
    }

    int ContentHeight() const { return sc_.ContentHeight(); }
    void Relayout() { sc_.area->setGeometry(rect()); sc_.Relayout(); }
    void ForceAdvanced() { adv_ = true; adv->setChecked(true); }   // dev aid (screenshots)

    // Write the form back to dsfix.ini. Only keys whose value actually changed are rewritten, so a
    // hand-tuned dsfix.ini keeps everything the player didn't touch.
    void Save() {
        std::string t = text_;
        auto put = [&](const char* k, const char* def, const std::string& v) {
            if (v != DsfixGet(text_, k, def)) t = DsfixSet(t, k, v);
        };
        put("renderWidth", "1920", S(rw->text()));   put("renderHeight", "1080", S(rh->text()));
        put("presentWidth", "1920", S(pw->text()));  put("presentHeight", "1080", S(ph->text()));
        // AA is Advanced-only: basic mode leaves AA and the opt-in alone (the launch-time force-off
        // handles it); in Advanced the choice is written and the opt-in recorded.
        if (adv_) {
            std::string aa = S(aaType->currentText());
            put("aaType", "SMAA", aa);
            put("aaQuality", "4", aa == "none" ? std::string("0") : Value(aaQual));
            inst_.SetAaOptIn(aa != "none");
        } else if (revertedAa_) {
            inst_.SetAaOptIn(false);   // Revert to Defaults = AA off
        }
        put("ssaoStrength", "0", Value(ssao));
        put("filteringOverride", "0", Value(filter));
        put("borderlessFullscreen", "0", On(border));
        put("captureCursor", "1", On(capCur));
        put("disableCursor", "1", On(disCur));
        put("skipIntro", "1", On(skipIntro));
        put("logLevel", "6", Value(logLvl));
        put("enableHudMod", "0", hudEn_);
        put("enableMinimalHud", "0", hudMin_);
        put("hudScaleFactor", "1.0", hudScale_);
        put("hudOpacity", "1.0", hudOpacity_);
        if (adv_) {
            put("dofOverrideResolution", "540", S(dofRes->text()));
            put("disableDofScaling", "1", On(disDofScale));
            put("dofBlurAmount", "1", S(dofBlur->text()));
        } else if (defDof->isChecked() != defDofInit_) {   // basic: only when toggled
            bool orig = defDof->isChecked();                // "Original DoF" = vanilla DoF (all zero)
            t = DsfixSet(t, "dofOverrideResolution", orig ? "0" : "540");
            t = DsfixSet(t, "disableDofScaling", orig ? "0" : "1");
            t = DsfixSet(t, "dofBlurAmount", orig ? "0" : "1");
        }
        if (adv_) {
            put("unlockFPS", "1", On(unlockFps));
            put("FPSlimit", "30", S(fpsLimit->text()));
            put("FPSthreshold", "28", S(fpsThresh->text()));
        } else if (fpsStab->isChecked() != fpsStabInit_) {  // basic: only when toggled
            t = DsfixSet(t, "unlockFPS", fpsStab->isChecked() ? "1" : "0");
            t = DsfixSet(t, "FPSlimit", "30");
        }
        if (adv_) {
            put("dinput8dllWrapper", "none", S(dinput[0]->currentText()));
            for (int i = 1; i < 10; ++i)
                put(("dinputChain" + std::to_string(i)).c_str(), "none", S(dinput[i]->currentText()));
            put("enableBackups", "1", On(enBackup));
            put("backupInterval", "10", S(bkpInt->text()));
            put("maxBackups", "10", S(bkpMax->text()));
            put("forceWindowed", "0", On(forceWin));
            put("enableVsync", "0", On(enVsync));
            put("fullscreenHz", "60", S(fsHz->text()));
        }
        plat::WriteText(inst_.DsfixIniPath(), t);
    }

protected:
    void resizeEvent(QResizeEvent*) override { Relayout(); }

private:
    int Layout(int W) {
        const bool a = adv_;
        const int LX = 16, FX = 178, ROW = 30, HDR = 28;
        const int CWp = std::max(360, W);
        int y = 16;
        auto vis = [](std::initializer_list<QWidget*> ws, bool v) { for (QWidget* w : ws) w->setVisible(v); };

        Put(adv, LX, y, 250, 22);
        Put(editIni, CWp - 126, y, 110, 24);
        y += 30;
        Put(hdrVideo, LX, y, 250); y += HDR;
        Put(lblRes, LX, y + 3, 160); Put(rw, FX, y, 56, 22); Put(resX, FX + 60, y + 3, 10); Put(rh, FX + 74, y, 56, 22); y += ROW;
        Put(lblOut, LX, y + 3, 160); Put(pw, FX, y, 56, 22); Put(outX, FX + 60, y + 3, 10); Put(ph, FX + 74, y, 56, 22); y += ROW;
        // AA is Advanced-only: the launcher forces it off before every launch (overlay collision).
        vis({ lblAA, aaType, lblAAQ, aaQual }, a);
        if (a) { Put(lblAA, LX, y + 3, 28); Put(aaType, 48, y, 96, 22); Put(lblAAQ, 150, y + 3, 66); Put(aaQual, 220, y, 80, 22); y += ROW; }
        Put(lblSSAO, LX, y + 3, 40); Put(ssao, 58, y, 78, 22); Put(lblFilter, 150, y + 3, 110); Put(filter, 262, y, 110, 22); y += ROW;
        Put(border, LX, y, 250, 22); y += ROW;
        // HUD Mod is Advanced-only: it recomposites the whole HUD (niche, glitch-prone).
        hudMod->setVisible(a);
        if (a) { Put(hudMod, LX, y, 250, 22); y += ROW; }

        Put(hdrCursor, LX, y, 250); y += HDR;
        Put(capCur, LX, y, 180, 22); Put(disCur, 200, y, 180, 22); y += ROW;

        Put(hdrOther, LX, y, 250); y += HDR;
        Put(skipIntro, LX, y, 180, 22); Put(lblLog, 200, y + 3, 66); Put(logLvl, 270, y, 96, 22); y += ROW;

        Put(hdrDof, LX, y, 250); y += HDR;
        vis({ lblDofRes, dofRes, disDofScale, lblDofBlur, dofBlur }, a);
        defDof->setVisible(!a);
        if (a) {
            Put(lblDofRes, LX, y + 3, 160); Put(dofRes, FX, y, 60, 22); y += ROW;
            Put(disDofScale, LX, y, 250, 22); y += ROW;
            Put(lblDofBlur, LX, y + 3, 160); Put(dofBlur, FX, y, 60, 22); y += ROW;
        } else {
            Put(defDof, LX, y, 250, 22); y += ROW;
        }

        Put(hdrFps, LX, y, 250); y += HDR;
        vis({ unlockFps, lblFpsLimit, fpsLimit, lblFpsThresh, fpsThresh }, a);
        fpsStab->setVisible(!a);
        if (a) {
            Put(unlockFps, LX, y, 250, 22); y += ROW;
            Put(lblFpsLimit, LX, y + 3, 104); Put(fpsLimit, 122, y, 56, 22);
            Put(lblFpsThresh, 196, y + 3, 92); Put(fpsThresh, 292, y, 56, 22); y += ROW;
        } else {
            Put(fpsStab, LX, y, 300, 22); y += ROW;
        }

        vis({ hdrBackup, enBackup, lblBkpInt, bkpInt, lblBkpMax, bkpMax }, a);
        if (a) {
            Put(hdrBackup, LX, y, 280); y += HDR;
            Put(enBackup, LX, y, 250, 22); y += ROW;
            Put(lblBkpInt, LX, y + 3, 160); Put(bkpInt, FX, y, 60, 22); y += ROW;
            Put(lblBkpMax, LX, y + 3, 160); Put(bkpMax, FX, y, 60, 22); y += ROW;
        }
        vis({ hdrNotReady, forceWin, enVsync, lblFsHz, fsHz }, a);
        if (a) {
            Put(hdrNotReady, LX, y, 360); y += HDR;
            Put(forceWin, LX, y, 230, 22); Put(enVsync, 250, y, 150, 22); y += ROW;
            Put(lblFsHz, LX, y + 3, 160); Put(fsHz, FX, y, 60, 22); y += ROW;
        }
        // Dinput DLL chaining: cascading dropdowns, each next appears once the prior is in use.
        hdrDinput->setVisible(a);
        if (a) {
            Put(hdrDinput, LX, y, 280); y += HDR;
            bool prevInUse = true;
            for (auto* cb : dinput) {
                cb->setVisible(prevInUse);
                if (prevInUse) { Put(cb, LX, y, 240, 22); y += ROW; }
                QString v = cb->currentText();
                prevInUse = prevInUse && v != "none" && !v.isEmpty();
            }
        } else {
            for (auto* cb : dinput) cb->hide();
        }
        Put(credit, LX, y + 4, 400, 16); y += 26;
        Put(revert, LX, y, 140, 28); y += 40;
        return y;
    }

    void OpenHudConfig() {
        QDialog d(this);
        d.setWindowTitle("HUD Mod");
        d.setFixedSize(360, 186);
        auto* en = Check(&d, "Enable HUD Mod", hudEn_ == "1");
        auto* mn = Check(&d, "Minimal HUD", hudMin_ == "1");
        auto* ls = Label(&d, "HUD scale factor"); auto* sc = Edit(&d, hudScale_);
        auto* lo = Label(&d, "Opacity (0-1)");    auto* op = Edit(&d, hudOpacity_);
        int lx = 16, fx = 180, y = 16, dy = 30;
        en->setGeometry(lx, y, 250, 22); y += dy;
        mn->setGeometry(lx, y, 250, 22); y += dy;
        ls->setGeometry(lx, y + 3, 150, 18); sc->setGeometry(fx, y, 70, 22); y += dy;
        lo->setGeometry(lx, y + 3, 150, 18); op->setGeometry(fx, y, 70, 22); y += dy + 6;
        auto* save = new RoundButton("Save", &d, true);  save->setGeometry(fx, y, 70, 28);
        auto* cancel = new RoundButton("Cancel", &d);    cancel->setGeometry(fx + 80, y, 70, 28);
        save->setDefault(true);
        auto group = [=](bool on) { for (QWidget* w : { (QWidget*)mn, (QWidget*)ls, (QWidget*)sc, (QWidget*)lo, (QWidget*)op }) w->setEnabled(on); };
        group(en->isChecked());
        connect(en, &QCheckBox::toggled, &d, group);
        connect(save, &QPushButton::clicked, &d, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
        if (d.exec() == QDialog::Accepted) {
            hudEn_ = On(en); hudMin_ = On(mn);
            hudScale_ = S(sc->text()); hudOpacity_ = S(op->text());
        }
    }

    void Revert() {
        if (QMessageBox::question(this, "Revert to Defaults",
                "Reset all DSFix settings to the launcher defaults?\nNothing is written to disk until you click Save.")
            != QMessageBox::Yes)
            return;
        rw->setText("1920"); rh->setText("1080"); pw->setText("0"); ph->setText("0");
        SelectLabel(aaType, "none"); SelectLabel(aaQual, "Low");
        SelectLabel(ssao, "Off");    SelectLabel(filter, "None");
        border->setChecked(true);    hudMod->setChecked(false);
        capCur->setChecked(false);   disCur->setChecked(true);
        skipIntro->setChecked(true); SelectLabel(logLvl, "Off (0)");
        dofRes->setText("540"); disDofScale->setChecked(true); dofBlur->setText("1");
        defDof->setChecked(false);
        unlockFps->setChecked(false); fpsLimit->setText("30"); fpsThresh->setText("28");
        fpsStab->setChecked(false);
        enBackup->setChecked(false); bkpInt->setText("1500"); bkpMax->setText("10");
        forceWin->setChecked(false); enVsync->setChecked(false); fsHz->setText("60");
        for (auto* cb : dinput) SelectLabel(cb, "none");
        aaQual->setEnabled(false);
        hudEn_ = "0"; hudMin_ = "0"; hudScale_ = "1.0"; hudOpacity_ = "1.0";
        // Stamp the defaults into the loaded text too: Save only writes keys that differ from it,
        // and basic mode skips DoF/FPS unless toggled, so this guarantees the full default set lands.
        static const char* kv[][2] = {
            {"renderWidth","1920"}, {"renderHeight","1080"}, {"presentWidth","0"}, {"presentHeight","0"},
            {"aaType","none"}, {"aaQuality","1"}, {"ssaoStrength","0"}, {"filteringOverride","0"},
            {"borderlessFullscreen","1"}, {"captureCursor","0"}, {"disableCursor","1"}, {"skipIntro","1"},
            {"logLevel","0"}, {"enableHudMod","0"}, {"enableMinimalHud","0"}, {"hudScaleFactor","1.0"},
            {"hudOpacity","1.0"}, {"dofOverrideResolution","540"}, {"disableDofScaling","1"},
            {"dofBlurAmount","1"}, {"unlockFPS","0"}, {"FPSlimit","30"}, {"FPSthreshold","28"},
            {"enableBackups","0"}, {"backupInterval","1500"}, {"maxBackups","10"}, {"forceWindowed","0"},
            {"enableVsync","0"}, {"fullscreenHz","60"}, {"dinput8dllWrapper","none"} };
        for (auto& p : kv) text_ = DsfixSet(text_, p[0], p[1]);
        for (int i = 1; i < 10; ++i) text_ = DsfixSet(text_, ("dinputChain" + std::to_string(i)).c_str(), "none");
        defDofInit_ = defDof->isChecked();
        fpsStabInit_ = fpsStab->isChecked();
        revertedAa_ = true;
        refit_();
    }

    const mctde::Install& inst_;
    std::function<void()> refit_;
    Scroller sc_;
    std::string text_, hudEn_, hudMin_, hudScale_, hudOpacity_;
    bool adv_ = false, fpsStabInit_ = false, defDofInit_ = false, revertedAa_ = false;

    Toggle *adv, *border, *hudMod, *skipIntro, *capCur, *disCur, *disDofScale, *defDof, *unlockFps, *fpsStab,
           *enBackup, *forceWin, *enVsync;
    RoundButton *editIni, *revert;
    QLabel *hdrVideo, *lblRes, *resX, *lblOut, *outX, *lblAA, *lblAAQ, *lblSSAO, *lblFilter, *hdrOther, *lblLog,
           *hdrCursor, *hdrDinput, *hdrDof, *lblDofRes, *lblDofBlur, *hdrFps, *lblFpsLimit, *lblFpsThresh,
           *hdrBackup, *lblBkpInt, *lblBkpMax, *hdrNotReady, *lblFsHz, *credit;
    QLineEdit *rw, *rh, *pw, *ph, *dofRes, *dofBlur, *fpsLimit, *fpsThresh, *bkpInt, *bkpMax, *fsHz;
    FlatCombo *aaType, *aaQual, *ssao, *filter, *logLvl, *dinput[10];
};

// ============================================================ mctde tab
// kind: 0 header, 1 checkbox, 2 int edit, 3 text edit, 4 combo
struct McDef { int kind; const char* label; const char* sec; const char* key; const char* def; bool adv;
               std::vector<QString> items; };
const std::vector<McDef>& Defs() {
    static const std::vector<McDef> d = {
        {0,"General",                       0,0,0,false,{}},
        {4,"Controller button icons",       "Launcher","ControllerIcons","Xbox",false,{"Xbox","PlayStation","Switch"}},
        {1,"Hide soul counter (entire bottom-right)", "HideSoulCounter","Enabled","1",false,{}},
        {1,"Enable overlay logging",        "Settings","EnableLogging","0",false,{}},
        {1,"Quit game cleanly on window close", "Settings","ExitWithGame","1",false,{}},
        {1,"Require launcher to start the game", "Launcher","RequireLauncher","1",false,{}},
        {0,"Multiplayer overlay",           0,0,0,false,{}},
        {1,"Show HP readout",               "HP","Enabled","1",false,{}},
        {0,"Overlay rendering",             0,0,0,true,{}},
        {4,"Overlay backend",               "Render","Backend","d3d",true,{"d3d","gdi"}},
        {2,"Overlay refresh (ms)",          "Render","SubmitMs","66",true,{}},
        {0,"Overlay (advanced)",            0,0,0,true,{}},
        {2,"Line height",                   "Overlay","LineHeight","20",true,{}},
        {2,"Overlay data poll (ms)",        "Overlay","RefreshMs","1000",true,{}},
        {1,"Hide your own row",             "Overlay","HideLocal","0",true,{}},
        {1,"Show local marker",             "Overlay","ShowLocalMarker","1",true,{}},
        {1,"Show disconnected rows",        "Overlay","ShowDisconnected","1",true,{}},
        {3,"Toggle modifier (VK hex)",      "Overlay","ToggleModifier","0x10",true,{}},
        {3,"Toggle key (VK hex)",           "Overlay","ToggleKey","0x72",true,{}},
        {0,"HP readout (advanced)",         0,0,0,true,{}},
        {2,"Current-HP offset",             "HP","CurrentOffset","724",true,{}},
        {2,"Max-HP offset",                 "HP","MaxOffset","728",true,{}},
        {2,"1-HP linger (ms)",              "HP","OneHpLingerMs","500",true,{}},
        {2,"HP poll (ms)",                  "HP","PollMs","33",true,{}},
        {0,"Ping / TruePing (advanced)",    0,0,0,true,{}},
        {1,"Enable true ping measurement",  "TruePing","Enabled","1",true,{}},
        {2,"Ping display mode",             "TruePing","DisplayMode","2",true,{}},
        {1,"Prefer overlay ping source",    "TruePing","PreferOverlay","1",true,{}},
        {2,"Ping channel",                  "TruePing","Channel","63",true,{}},
        {2,"Ping send interval (ms)",       "TruePing","SendMs","1000",true,{}},
        {2,"Ping stale timeout (ms)",       "TruePing","StaleMs","4000",true,{}},
        {0,"WebSocket feed (advanced)",     0,0,0,true,{}},
        {1,"Enable WebSocket feed",         "WebSocket","Enabled","0",true,{}},
        {2,"WebSocket port",                "WebSocket","Port","39876",true,{}},
        {2,"WebSocket send (ms)",           "WebSocket","SendMs","33",true,{}},
        {0,"Debug (advanced)",              0,0,0,true,{}},
        {1,"Debug P2P bridge",              "Debug","DebugP2PBridge","0",true,{}},
        {0,"Controller (advanced)",         0,0,0,true,{}},
        {1,"First-launch binding nudge",    "Controller","BindingNudge","1",true,{}},
        {1,"Force binding nudge (testing)", "Controller","BindingNudgeForce","0",true,{}},
        {3,"Manual nudge key (VK hex)",     "Controller","BindingNudgeKey","0",true,{}},
        {0,"Compatibility (advanced)",      0,0,0,true,{}},
        {3,"Chainload folder",              "Compatibility","ChainloadFolder","mctde-Link_Chainload",true,{}},
    };
    return d;
}

// HP Display dropdown: index 0 = numeric readout; 1..3 = bar with sub-style minimal/damage/detailed.
const std::vector<QString> kHpDisplay = { "Numeric Value", "Health Bar (minimal)", "Health Bar (damage)", "Health Bar (detailed)" };

// Live overlay preview: a 16:9 "screen" with three sample rows placed by corner + padding.
struct Pv { QString font = "Tahoma"; int size = 24, hpSize = 48, corner = 0, padX = 5, padY = 5;
            bool bars = false, ping = true, name = true; int barStyle = 0; };

class OverlayPreview : public QWidget {
public:
    explicit OverlayPreview(QWidget* parent) : QWidget(parent) {}
    Pv pv;
protected:
    void paintEvent(QPaintEvent*) override {
        Theme t = ThemeFor(g_dark);
        QPainter p(this);
        const int W = width(), H = height();
        p.fillRect(rect(), t.field);
        int sw = W - 4, sh = int(sw * 9.0 / 16.0);
        if (sh > H - 4) { sh = H - 4; sw = int(sh * 16.0 / 9.0); }
        QRect scr((W - sw) / 2, (H - sh) / 2, sw, sh);
        p.fillRect(scr, QColor(44, 48, 56));
        p.setPen(t.border);
        p.drawRect(scr.adjusted(0, 0, -1, -1));
        p.setRenderHint(QPainter::TextAntialiasing);

        double scale = sw / 1920.0;
        int fpx = std::max(6, int(pv.size * scale + 0.5)), hpFpx = std::max(6, int(pv.hpSize * scale + 0.5));
        QFont f(pv.font); f.setPixelSize(fpx);
        QFont hf(pv.font); hf.setPixelSize(hpFpx);
        QFontMetrics fm(f), hfm(hf);
        int lh = std::max(int(fpx * 1.3), int(hpFpx * 1.3)) + 1;
        auto text = [&](int x, int y, const QString& s, const QFont& fo, QColor col) {
            p.setFont(fo); p.setPen(col);
            p.drawText(QPoint(x, y + QFontMetrics(fo).ascent()), s);
        };

        struct Row { const char* ping; const char* name; };
        static const Row rows[] = { { "42ms", "Solaire" }, { "88ms", "Lautrec" }, { "120ms", "Siegmeyer" } };
        static const char* hpNums[] = { "1650", "1100", "550" };
        const QString hpSample = "1650HP", dmgSample = "240";
        int hpW = pv.bars ? int(hpFpx * 5.2) : hfm.horizontalAdvance(hpSample);
        int gap = std::max(3, fpx / 3);
        int blockW = 0;
        for (const Row& r : rows) {
            int rw = hpW + gap;
            if (pv.ping) rw += fm.horizontalAdvance(r.ping) + gap;
            if (pv.name) rw += fm.horizontalAdvance(r.name);
            blockW = std::max(blockW, rw);
        }
        int blockH = lh * 3;
        blockW = std::max(blockW, fpx);
        int px = int(pv.padX * scale + 0.5), py = int(pv.padY * scale + 0.5);
        bool right = pv.corner == 1 || pv.corner == 3, bottom = pv.corner == 2 || pv.corner == 3;
        int bx = right ? scr.right() + 1 - px - blockW : scr.left() + px;
        int by = bottom ? scr.bottom() + 1 - py - blockH : scr.top() + py;
        bx = std::max(scr.left() + 2, std::min(bx, scr.right() + 1 - blockW - 2));
        by = std::max(scr.top() + 2, std::min(by, scr.bottom() + 1 - blockH - 2));
        int blockRight = bx + blockW, ty = by;

        for (int i = 0; i < 3; ++i) {
            int nameW = pv.name ? fm.horizontalAdvance(rows[i].name) : 0;
            int pingW = pv.ping ? fm.horizontalAdvance(rows[i].ping) : 0;
            auto drawHP = [&](int xx) {
                if (pv.bars) {
                    int barH = std::max(3, int(hpFpx * 0.7)), barY = ty + (lh - barH) / 2;
                    p.fillRect(xx, barY, hpW, barH, QColor(58, 16, 16));
                    int curW = hpW * (3 - i) / 3, ghostW = hpW * 3 / 4;
                    if (i == 1) p.fillRect(xx + curW, barY, ghostW - curW, barH, QColor(230, 205, 55));
                    p.fillRect(xx, barY, curW, barH, QColor(95, 175, 75));
                    bool lowHp = (3 - i) * 2 < 3;
                    if ((pv.barStyle == 1 || pv.barStyle == 2) && i == 1) {   // the damage taken, in the bar
                        int tw = fm.horizontalAdvance(dmgSample);
                        int tx = lowHp ? xx + hpW - tw - 3 : xx + 3, tyv = barY + (barH - fm.height()) / 2;
                        text(tx + 1, tyv + 1, dmgSample, f, Qt::black);
                        text(tx, tyv, dmgSample, f, QColor(230, 205, 55));
                    }
                } else {
                    text(xx, ty + (lh - hpFpx) / 2, hpSample, hf, QColor(220, 220, 225));
                }
            };
            int nameX = -1;
            int nEl = 1 + (pv.ping ? 1 : 0) + (pv.name ? 1 : 0);
            int rowW = hpW + pingW + nameW + gap * (nEl - 1);
            int cx = right ? blockRight - rowW : bx;
            auto drawPing = [&](int xx) { text(xx, ty, rows[i].ping, f, QColor(170, 200, 235)); };
            auto drawName = [&](int xx) { nameX = xx; text(xx, ty, rows[i].name, f, QColor(232, 232, 236)); };
            if (!right) {
                drawHP(cx); cx += hpW + gap;
                if (pv.ping) { drawPing(cx); cx += pingW + gap; }
                if (pv.name) drawName(cx);
            } else {
                if (pv.name) { drawName(cx); cx += nameW + gap; }
                if (pv.ping) { drawPing(cx); cx += pingW + gap; }
                drawHP(cx);
            }
            // Detailed style: the HP number in white on the outer side of the name, every row.
            if (pv.bars && pv.barStyle == 2 && pv.name && nameX >= 0) {
                QString s = hpNums[i];
                int dx = right ? nameX - gap - fm.horizontalAdvance(s) : nameX + nameW + gap;
                text(dx + 1, ty + 1, s, f, Qt::black);
                text(dx, ty, s, f, Qt::white);
            }
            ty += lh;
        }
    }
};

class McPage : public QWidget {
public:
    McPage(QWidget* parent, const mctde::Install& inst, std::function<void()> refit)
        : QWidget(parent), inst_(inst), refit_(std::move(refit)) {
        adv = Check(this, "Advanced Options", false);
        editIni = new RoundButton("Edit mctde-link.ini", this);
        sc_.Build(this);
        QWidget* c = sc_.content;
        auto Get = [&](const char* s, const char* k, const char* d) { return Q(inst_.Get(s, k, d)); };
        auto GetI = [&](const char* s, const char* k, int d) { return inst_.GetInt(s, k, d); };

        hdrOv = Header(c, "Overlay");
        classicFont = Check(c, "Classic Font (Dark Souls in-game font)", GetI("Overlay", "UseDsFont", 0) != 0);
        lblFont = Label(c, "Font");
        font = new FlatCombo(c);
        {   // installed fonts (Wine sees the desktop's fonts), each item drawn in its own face
            QStringList fams = QFontDatabase::families();
            QString cur = Get("Overlay", "FontFace", "Tahoma");
            bool found = false;
            for (const QString& fam : fams) if (fam.compare(cur, Qt::CaseInsensitive) == 0) found = true;
            if (!found) fams.append(cur);   // keep the saved face even if this desktop lacks it
            fams.sort(Qt::CaseInsensitive);
            int sel = 0;
            for (const QString& fam : fams) {
                if (fam.startsWith('@')) continue;
                QFont ff(fam); ff.setPixelSize(15);
                font->addItem(fam, fam);
                font->setItemData(font->count() - 1, ff, Qt::FontRole);
                if (fam.compare(cur, Qt::CaseInsensitive) == 0) sel = font->count() - 1;
            }
            font->setCurrentIndex(sel);
        }
        lblSize = Label(c, "Font size");   size = Edit(c, S(Get("Overlay", "FontHeight", "24")));
        lblHpSize = Label(c, "HP size");   hpSize = Slider(c, 12, 80, GetI("Overlay", "HpFontHeight", 48));
        lblCorner = Label(c, "Screen corner");
        corner = Combo(c, { "top_left", "top_right", "bottom_left", "bottom_right" }, Get("Overlay", "Corner", "top_left"));
        lblPadX = Label(c, "Padding X");   padX = Slider(c, 0, 400, GetI("Overlay", "PaddingX", 5));
        lblPadY = Label(c, "Padding Y");   padY = Slider(c, 0, 400, GetI("Overlay", "PaddingY", 5));
        lblHpDisp = Label(c, "HP Display");
        {
            bool bars = GetI("Overlay", "HpBars", 0) != 0;
            int style = std::clamp(GetI("Overlay", "HpBarStyle", 0), 0, 2);
            hpDisp = Combo(c, kHpDisplay, kHpDisplay[bars ? 1 + style : 0]);
        }
        showPing = Check(c, "Show ping", GetI("Overlay", "ShowPing", 1) != 0);
        showName = Check(c, "Show names", GetI("Overlay", "ShowName", 1) != 0);
        forceTop = Check(c, "Force overlay always-on-top", GetI("Overlay", "ForceTopmost", 0) != 0);
        preview = new OverlayPreview(c);

        const auto& defs = Defs();
        ctl.resize(defs.size(), nullptr);
        lbl.resize(defs.size(), nullptr);
        for (size_t i = 0; i < defs.size(); ++i) {
            const McDef& d = defs[i];
            if (d.kind == 0) ctl[i] = Header(c, d.label);
            else if (d.kind == 1) ctl[i] = Check(c, d.label, inst_.GetInt(d.sec, d.key, atoi(d.def)) != 0);
            else {
                lbl[i] = Label(c, d.label);
                if (d.kind == 4) { ctl[i] = Combo(c, d.items, Get(d.sec, d.key, d.def)); sc_.GuardWheel(ctl[i]); }
                else ctl[i] = Edit(c, inst_.Get(d.sec, d.key, d.def));
            }
        }
        for (QWidget* w : { (QWidget*)font, (QWidget*)corner, (QWidget*)hpDisp, (QWidget*)hpSize, (QWidget*)padX, (QWidget*)padY })
            sc_.GuardWheel(w);
        sc_.layout = [this](int w) { return Layout(w); };

        Sync();
        auto sync = [this] { Sync(); };
        connect(classicFont, &QCheckBox::toggled, this, [this] { refit_(); Sync(); });
        connect(font, QOverload<int>::of(&QComboBox::currentIndexChanged), this, sync);
        connect(corner, QOverload<int>::of(&QComboBox::currentIndexChanged), this, sync);
        connect(hpDisp, QOverload<int>::of(&QComboBox::currentIndexChanged), this, sync);
        connect(size, &QLineEdit::textChanged, this, sync);
        for (QSlider* s : { hpSize, padX, padY }) connect(s, &QSlider::valueChanged, this, sync);
        for (QCheckBox* cb : { showPing, showName }) connect(cb, &QCheckBox::toggled, this, sync);
        connect(adv, &QCheckBox::toggled, this, [this](bool on) {
            adv_ = on;
            sc_.area->verticalScrollBar()->setValue(0);
            refit_();
        });
        connect(editIni, &QPushButton::clicked, this, [this] { OpenInEditor(ResolvedIn(inst_.Dir(), "mctde-link.ini")); });
    }

    int ContentHeight() const { return sc_.ContentHeight() + 38; }   // + the fixed top bar
    void ForceAdvanced() { adv->setChecked(true); }                    // dev aid (screenshots)
    void Relayout() {
        adv->setGeometry(14, 9, 200, 22);
        editIni->setGeometry(width() - 164, 7, 150, 24);
        sc_.area->setGeometry(0, 38, width(), std::max(40, height() - 38));
        sc_.Relayout();
    }

    void Save() {
        const auto& defs = Defs();
        for (size_t i = 0; i < defs.size(); ++i) {
            const McDef& d = defs[i];
            if (d.kind == 0) continue;
            std::string v = d.kind == 1 ? On(static_cast<QCheckBox*>(ctl[i]))
                          : d.kind == 4 ? S(static_cast<QComboBox*>(ctl[i])->currentText())
                                        : S(static_cast<QLineEdit*>(ctl[i])->text());
            inst_.Set(d.sec, d.key, v);
            // Hide soul counter drives its companion keys (the actual hide is HideBox=1).
            if (!strcmp(d.sec, "HideSoulCounter") && !strcmp(d.key, "Enabled")) {
                bool on = v == "1";
                inst_.Set("HideSoulCounter", "VerifyOnly", on ? "0" : "1");
                inst_.Set("HideSoulCounter", "HideGainPopup", "1");
                inst_.Set("HideSoulCounter", "HideBox", on ? "1" : "0");
            }
        }
        inst_.Set("Overlay", "FontFace", S(font->currentText()));
        inst_.Set("Overlay", "UseDsFont", On(classicFont));
        inst_.Set("Overlay", "FontHeight", S(size->text()));
        inst_.Set("Overlay", "HpFontHeight", std::to_string(hpSize->value()));
        inst_.Set("Overlay", "Corner", S(corner->currentText()));
        inst_.Set("Overlay", "PaddingX", std::to_string(padX->value()));
        inst_.Set("Overlay", "PaddingY", std::to_string(padY->value()));
        // HP Display -> the three underlying keys; every mode shows HP, so ShowHp stays 1.
        int hd = std::max(0, hpDisp->currentIndex());
        inst_.Set("Overlay", "ShowHp", "1");
        inst_.Set("Overlay", "HpBars", hd == 0 ? "0" : "1");
        inst_.Set("Overlay", "HpBarStyle", std::to_string(hd <= 1 ? 0 : hd - 1));
        inst_.Set("Overlay", "ShowPing", On(showPing));
        inst_.Set("Overlay", "ShowName", On(showName));
        inst_.Set("Overlay", "ForceTopmost", On(forceTop));
    }

protected:
    void resizeEvent(QResizeEvent*) override { Relayout(); }

private:
    void Sync() {
        Pv& v = preview->pv;
        v.font = font->currentText();
        v.size = std::max(6, size->text().toInt());
        v.hpSize = std::max(6, hpSize->value());
        v.corner = std::max(0, corner->currentIndex());
        v.padX = padX->value();
        v.padY = padY->value();
        int hd = std::max(0, hpDisp->currentIndex());
        v.bars = hd != 0;
        v.barStyle = hd <= 1 ? 0 : hd - 1;
        v.ping = showPing->isChecked();
        v.name = showName->isChecked();
        preview->update();
    }

    int Layout(int W) {
        const int CW = std::max(300, W);
        const int LX = 14, FX = 116, ROW = 30, HDRH = 28, PVX = 264, PVH = 168;
        const int PVW = std::max(150, CW - PVX - 10);
        const int CX = FX, LBLW = CX - LX - 4, WIDE = std::max(120, PVX - CX - 6);
        int y = 8;
        Put(hdrOv, LX, y, 240); y += HDRH;
        int ovTop = y;
        Put(classicFont, LX, y, 260, 22); y += 27;
        // Classic Font uses the baked DS bitmap font at a fixed size: font / size / HP size don't apply.
        bool classic = classicFont->isChecked();
        for (QWidget* w : { (QWidget*)lblFont, (QWidget*)font, (QWidget*)lblSize, (QWidget*)size, (QWidget*)lblHpSize, (QWidget*)hpSize })
            w->setVisible(!classic);
        if (!classic) {
            Put(lblFont, LX, y + 3, LBLW); Put(font, CX, y, WIDE, 24); y += ROW;
            Put(lblSize, LX, y + 3, LBLW); Put(size, CX, y, 56, 22);   y += ROW;
            Put(lblHpSize, LX, y + 3, LBLW); Put(hpSize, CX, y, WIDE, 24); y += ROW;
        }
        Put(lblCorner, LX, y + 3, LBLW); Put(corner, CX, y, 130, 22);  y += ROW;
        Put(lblPadX, LX, y + 3, LBLW);   Put(padX, CX, y, WIDE, 24);   y += ROW;
        Put(lblPadY, LX, y + 3, LBLW);   Put(padY, CX, y, WIDE, 24);   y += ROW;
        Put(lblHpDisp, LX, y + 3, LBLW); Put(hpDisp, CX, y, WIDE, 22); y += ROW;
        Put(showPing, LX, y, 240, 22); y += 27;
        Put(showName, LX, y, 240, 22); y += 27;
        Put(forceTop, LX, y, 240, 22); y += 27;
        Put(preview, PVX, ovTop, PVW, PVH);
        y = std::max(y, ovTop + PVH + 4) + 6;

        const auto& defs = Defs();
        for (size_t i = 0; i < defs.size(); ++i) {
            const McDef& d = defs[i];
            bool vis = !d.adv || adv_;
            ctl[i]->setVisible(vis);
            if (lbl[i]) lbl[i]->setVisible(vis);
            if (!vis) continue;
            if (d.kind == 0)      { Put(ctl[i], LX, y + 4, CW - 2 * LX); y += HDRH + 2; }
            else if (d.kind == 1) { Put(ctl[i], LX, y, CW - 2 * LX, 22); y += 28; }
            else {
                const int TFX = 170;   // the table's labels run wider than the overlay group's
                Put(lbl[i], LX, y + 3, TFX - LX - 6);
                Put(ctl[i], TFX, y, d.kind == 4 ? 150 : 90, 22);
                y += ROW;
            }
        }
        return y + 6;
    }

    const mctde::Install& inst_;
    std::function<void()> refit_;
    Scroller sc_;
    bool adv_ = false;
    Toggle *adv, *classicFont, *showPing, *showName, *forceTop;
    RoundButton* editIni;
    QLabel *hdrOv, *lblFont, *lblSize, *lblHpSize, *lblCorner, *lblPadX, *lblPadY, *lblHpDisp;
    FlatCombo *font, *corner, *hpDisp;
    QLineEdit* size;
    QSlider *hpSize, *padX, *padY;
    OverlayPreview* preview;
    std::vector<QWidget*> ctl;
    std::vector<QLabel*> lbl;
};

// ============================================================ Vulkan (DXVK) tab
// On Linux the game always renders on Vulkan through Proton's own DXVK, so the on/off switch
// is shown but greyed out (it can't change that). The dxvk.conf tuning rows DO apply -- Proton's
// DXVK reads dxvk.conf from the game's working directory -- so they stay editable. Mirrors the
// Windows launcher's Vulkan tab (mctde_launcher.cpp).
const std::vector<QString> VK_PRESENT_LBL = { "App-controlled", "Off", "On (vsync)", "Half-rate (30 fps)" };
const std::vector<QString> VK_PRESENT_VAL = { "-1", "0", "1", "2" };
const std::vector<QString> VK_MAXLAT_LBL  = { "Default", "1 (lowest latency)", "2", "3" };
const std::vector<QString> VK_MAXLAT_VAL  = { "0", "1", "2", "3" };

static const char* const kDxvkConfDefault =
    "# DXVK config for PTDE (read from the game's working directory).\n"
    "# The mctde launcher's \"Vulkan\" tab edits this file for you.\n\n"
    "d3d9.presentInterval = 2\n";

// Active (uncommented) value of a dxvk.conf key ("key = value"); "" if absent/commented.
static std::string DxvkConfGet(const std::string& text, const std::string& key) {
    std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        std::string s = line.substr(a);
        if (s[0] == '#') continue;
        size_t eq = s.find('='); if (eq == std::string::npos) continue;
        std::string k = s.substr(0, eq); size_t ke = k.find_last_not_of(" \t");
        k = (ke == std::string::npos) ? "" : k.substr(0, ke + 1);
        if (k != key) continue;
        std::string v = s.substr(eq + 1);
        size_t va = v.find_first_not_of(" \t"); size_t ve = v.find_last_not_of(" \t\r\n");
        return (va == std::string::npos) ? "" : v.substr(va, ve - va + 1);
    }
    return "";
}
// Set a dxvk.conf key: non-empty -> uncommented `key = value` (replacing any line for the key,
// else appending); empty -> comment out any active line (DXVK falls back to its default).
static std::string DxvkConfSet(const std::string& text, const std::string& key, const std::string& value) {
    std::vector<std::string> lines;
    { std::istringstream in(text); std::string l;
      while (std::getline(in, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); lines.push_back(l); } }
    auto keyOf = [](const std::string& raw, bool& commented) -> std::string {
        size_t a = raw.find_first_not_of(" \t"); if (a == std::string::npos) return "";
        std::string s = raw.substr(a); commented = false;
        if (s[0] == '#') { commented = true; size_t b = s.find_first_not_of(" \t", 1);
                           if (b == std::string::npos) return ""; s = s.substr(b); }
        size_t eq = s.find('='); if (eq == std::string::npos) return "";
        std::string k = s.substr(0, eq); size_t ke = k.find_last_not_of(" \t");
        return (ke == std::string::npos) ? "" : k.substr(0, ke + 1);
    };
    const std::string newline = key + " = " + value; bool done = false;
    for (auto& l : lines) {
        bool commented = false;
        if (keyOf(l, commented) != key) continue;
        if (!value.empty()) { if (!done) { l = newline; done = true; } else if (!commented) l = "# " + l; }
        else if (!commented) l = "# " + l;
    }
    if (!value.empty() && !done) { if (!lines.empty() && !lines.back().empty()) lines.push_back(""); lines.push_back(newline); }
    std::string out; for (const auto& l : lines) { out += l; out += "\n"; } return out;
}

class VulkanPage : public QWidget {
public:
    VulkanPage(QWidget* parent, const mctde::Install& inst, std::function<void()> refit)
        : QWidget(parent), inst_(inst), refit_(std::move(refit)) {
        sc_.Build(this);
        QWidget* c = sc_.content;
        confPath_ = S(ResolvedIn(inst_.Dir(), "dxvk.conf"));
        text_ = plat::ReadText(confPath_);
        auto G = [&](const char* k, const char* d) { std::string v = DxvkConfGet(text_, k); return v.empty() ? std::string(d) : v; };

        hdr = Header(c, "Vulkan Renderer (DXVK)");
        enable = Check(c, "Enable Vulkan (DXVK)", true);
        enable->setEnabled(false);   // Linux: Proton's own DXVK is always in play
        note = Label(c, "Linux/Proton already renders on Vulkan via Proton's own DXVK, so this "
                        "switch isn't needed here. The settings below still apply.");
        note->setWordWrap(true);
        lblPresent = Label(c, "Vsync / present interval");
        present = Mapped(c, VK_PRESENT_LBL, VK_PRESENT_VAL, Q(G("d3d9.presentInterval", "-1")));
        lblMaxLat = Label(c, "Max frame latency");
        maxLat = Mapped(c, VK_MAXLAT_LBL, VK_MAXLAT_VAL, Q(G("d3d9.maxFrameLatency", "0")));
        hud = Check(c, "Show DXVK HUD (fps + frametimes)", !DxvkConfGet(text_, "dxvk.hud").empty());
        editConf = new RoundButton("Edit dxvk.conf", c);
        credit = Label(c, "DXVK by Philip Rebohle and contributors - zlib/libpng license. github.com/doitsujin/dxvk");

        for (QComboBox* cb : { present, maxLat }) sc_.GuardWheel(cb);
        sc_.layout = [this](int w) { return Layout(w); };
        connect(editConf, &QPushButton::clicked, this, [this] { OpenInEditor(ResolvedIn(inst_.Dir(), "dxvk.conf")); });
    }
    int ContentHeight() const { return sc_.ContentHeight(); }
    void Relayout() { sc_.area->setGeometry(rect()); sc_.Relayout(); }
    void ForceAdvanced() {}
    void Save() {
        // Linux: the on/off switch is greyed (Proton owns the renderer), so only dxvk.conf is written.
        std::string t = text_.empty() ? std::string(kDxvkConfDefault) : text_;
        t = DxvkConfSet(t, "d3d9.presentInterval", Value(present));
        t = DxvkConfSet(t, "d3d9.maxFrameLatency", Value(maxLat));
        t = DxvkConfSet(t, "dxvk.hud", hud->isChecked() ? "fps,frametimes" : "");
        plat::WriteText(confPath_, t);
        text_ = t;
    }
protected:
    void resizeEvent(QResizeEvent*) override { Relayout(); }

private:
    int Layout(int w) {
        const int LX = 16, FX = 200, ROW = 30, HDR = 28; int y = 16;
        Put(hdr, LX, y, w - 32, 18); y += HDR;
        Put(enable, LX, y, 320, 22); y += ROW;
        Put(note, LX, y, w - 32, 36); y += 42;
        Put(lblPresent, LX, y + 3, FX - LX - 6, 18); Put(present, FX, y, 170, 24); y += ROW;
        Put(lblMaxLat,  LX, y + 3, FX - LX - 6, 18); Put(maxLat,  FX, y, 170, 24); y += ROW;
        Put(hud, LX, y, 320, 22); y += ROW + 6;
        Put(editConf, LX, y, 130, 24); y += 34;
        Put(credit, LX, y, w - 32, 16); y += 22;
        return y + 6;
    }
    const mctde::Install& inst_;
    std::function<void()> refit_;
    Scroller sc_;
    std::string text_, confPath_;
    QLabel *hdr, *note, *lblPresent, *lblMaxLat, *credit;
    QCheckBox *enable, *hud;
    QComboBox *present, *maxLat;
    QPushButton* editConf;
};

// ============================================================ the host window
class SettingsDialog : public QDialog {
public:
    SettingsDialog(QWidget* parent, const mctde::Install& inst, int initialTab) : QDialog(parent) {
        setWindowTitle("Settings");
        QFont f = font(); f.setPointSizeF(9); setFont(f);
        setStyleSheet(StyleSheet());
        dsAvail_ = plat::FileExists(inst.DsfixIniPath());
        mcAvail_ = plat::FileExists(inst.IniPath());

        const int TABH = 28, TABW = 96, TABX = 12, TABGAP = 2, TABY = SET_PANELY - TABH - 6;
        tabDs_ = new TabButton("DSFix", this); tabDs_->setGeometry(TABX, TABY, TABW, TABH);
        tabMc_ = new TabButton("mctde", this); tabMc_->setGeometry(TABX + TABW + TABGAP, TABY, TABW, TABH);
        tabVk_ = new TabButton("Vulkan", this); tabVk_->setGeometry(TABX + 2 * (TABW + TABGAP), TABY, TABW, TABH);
        tabDs_->setObjectName("tabDs"); tabMc_->setObjectName("tabMc"); tabVk_->setObjectName("tabVk");   // controller LB / RB
        save_ = new RoundButton("Save", this, true);
        cancel_ = new RoundButton("Cancel", this);
        save_->setDefault(true);

        auto refit = [this] { ApplySize(); };
        if (dsAvail_) ds_ = new DsfixPage(this, inst, refit);
        else dsMissing_ = Placeholder("dsfix.ini was not found next to the launcher.\nInstall DSFix into this folder to configure it.");
        if (mcAvail_) mc_ = new McPage(this, inst, refit);
        else mcMissing_ = Placeholder("mctde-link.ini was not found next to the launcher.");
        vk_ = new VulkanPage(this, inst, refit);   // always available (manages dxvk.conf)

        connect(tabDs_, &QPushButton::clicked, this, [this] { SelectTab(0); });
        connect(tabMc_, &QPushButton::clicked, this, [this] { SelectTab(1); });
        connect(tabVk_, &QPushButton::clicked, this, [this] { SelectTab(2); });
        connect(save_, &QPushButton::clicked, this, [this] {
            if (ds_) ds_->Save();
            if (mc_) mc_->Save();
            if (vk_) vk_->Save();
            accept();
        });
        connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);

        if (getenv("MCTDE_SETTINGS_ADV")) { if (ds_) ds_->ForceAdvanced(); if (mc_) mc_->ForceAdvanced(); }
        if (!dsAvail_ && initialTab == 0) initialTab = 1;   // don't open on an empty DSFix tab
        if (!mcAvail_ && initialTab == 1) initialTab = 0;
        SelectTab(initialTab);
    }

private:
    QLabel* Placeholder(const char* text) {
        auto* l = new QLabel(text, this);
        l->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        l->setWordWrap(true);
        return l;
    }
    QWidget* Page(int tab) const {
        if (tab == 2) return (QWidget*)vk_;
        return tab == 0 ? (ds_ ? (QWidget*)ds_ : (QWidget*)dsMissing_) : (mc_ ? (QWidget*)mc_ : (QWidget*)mcMissing_);
    }

    void SelectTab(int tab) {
        tab_ = tab;
        Page(0)->setVisible(tab == 0);
        Page(1)->setVisible(tab == 1);
        Page(2)->setVisible(tab == 2);
        tabDs_->active = tab == 0; tabMc_->active = tab == 1; tabVk_->active = tab == 2;
        // Dynamic property the gamepad handler reads to cycle LB/RB through the three tabs.
        tabDs_->setProperty("cur", tab == 0); tabMc_->setProperty("cur", tab == 1); tabVk_->setProperty("cur", tab == 2);
        tabDs_->update(); tabMc_->update(); tabVk_->update();
        ApplySize();
        // Start on the tab's first field (controller navigation). Deferred: on the first call the
        // window isn't shown yet and focus set now would be lost to the default button.
        QTimer::singleShot(0, this, [this, tab] { if (tab_ == tab) pad::FocusFirstIn(Page(tab)); });
    }

    // Ask for a window height that fits the active tab's content (a short page doesn't leave a gap
    // above Save / Cancel); taller pages are capped to the screen and scroll. The compositor has the
    // final say -- on Wayland a shown window may not grow on request -- so this only REQUESTS the
    // size, and resizeEvent / PlaceAll lay everything out from the size we actually got.
    void ApplySize() {
        QScreen* scr = screen() ? screen() : QApplication::primaryScreen();
        int availH = scr ? scr->availableGeometry().height() : 800;
        int maxPanelH = std::max(160, std::min(SET_TOPCAP, availH - 40) - SET_PANELY - SET_BTNBAR);
        int panelH = 180;
        if (tab_ == 0 && ds_) panelH = std::clamp(ds_->ContentHeight() + 2, 160, maxPanelH);
        if (tab_ == 1 && mc_) panelH = std::clamp(mc_->ContentHeight() + 2, 160, maxPanelH);
        if (tab_ == 2 && vk_) panelH = std::clamp(vk_->ContentHeight() + 2, 160, maxPanelH);
        int want = SET_PANELY + panelH + SET_BTNBAR;
        // Fixed width; height free between a floor and the wanted size, so a compositor that keeps
        // the old height still gets a valid (scrolling) layout instead of a clipped one.
        setMinimumSize(SET_CW, SET_PANELY + 160 + SET_BTNBAR);
        setMaximumSize(SET_CW, std::max(want, SET_PANELY + 160 + SET_BTNBAR));
        resize(SET_CW, want);
        if (QWindow* w = windowHandle()) w->resize(SET_CW, want);   // push it to a shown native window too
        PlaceAll();
    }

    // Lay the tabs' panel and the Save / Cancel bar out inside the window's real size.
    void PlaceAll() {
        const int CW = width(), CH = height();
        const int panelH = std::max(40, CH - SET_PANELY - SET_BTNBAR);
        QWidget* page = Page(tab_);
        if (page == dsMissing_ || page == mcMissing_) page->setGeometry(20, SET_PANELY + 40, CW - 40, 60);
        else page->setGeometry(0, SET_PANELY, CW, panelH);
        if (tab_ == 0 && ds_) ds_->Relayout();
        if (tab_ == 1 && mc_) mc_->Relayout();
        if (tab_ == 2 && vk_) vk_->Relayout();
        const int btnY = CH - 36;
        save_->setGeometry(CW - 164, btnY, 70, 28);
        cancel_->setGeometry(CW - 86, btnY, 70, 28);
        save_->raise(); cancel_->raise();
    }

protected:
    void resizeEvent(QResizeEvent*) override { PlaceAll(); }

private:
    bool dsAvail_ = false, mcAvail_ = false;
    int tab_ = 0;
    TabButton *tabDs_, *tabMc_, *tabVk_;
    RoundButton *save_, *cancel_;
    DsfixPage* ds_ = nullptr;
    McPage* mc_ = nullptr;
    VulkanPage* vk_ = nullptr;
    QLabel *dsMissing_ = nullptr, *mcMissing_ = nullptr;
};

} // namespace

bool OpenSettings(QWidget* parent, const mctde::Install& inst, int initialTab) {
    if (!plat::FileExists(inst.DsfixIniPath()) && !plat::FileExists(inst.IniPath())) {
        QMessageBox::warning(parent, "Settings", "Neither mctde-link.ini nor dsfix.ini was found next to the launcher.");
        return false;
    }
    if (const char* t = getenv("MCTDE_SETTINGS_TAB")) initialTab = atoi(t);   // dev aid
    SettingsDialog dlg(parent, inst, initialTab);
    // Developer aid: MCTDE_SETTINGS_SHOT=<file.png> renders the open window to a PNG and closes it.
    if (const char* shot = getenv("MCTDE_SETTINGS_SHOT")) {
        QTimer::singleShot(300, &dlg, [&dlg, shot] {
            if (const char* h = getenv("MCTDE_SETTINGS_FORCE_H")) {   // simulate a compositor that won't grow it
                dlg.setMinimumHeight(0); dlg.setMaximumHeight(atoi(h)); dlg.resize(dlg.width(), atoi(h));
            }
            dlg.grab().save(shot);
            if (getenv("MCTDE_SETTINGS_SAVE"))   // ...and press Save (round-trip testing)
                for (QPushButton* b : dlg.findChildren<QPushButton*>()) if (b->text() == "Save") { b->click(); return; }
            dlg.reject();
        });
        return dlg.exec() == QDialog::Accepted;
    }
    return dlg.exec() == QDialog::Accepted;
}
