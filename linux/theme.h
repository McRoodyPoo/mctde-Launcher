// linux/theme.h -- the launcher's look, shared by the main window and the Settings window: the
// Windows launcher's palette, and custom-painted controls that copy its owner-drawn ones (the
// slide-switch Toggle and the rounded, gradient RoundButton).
#pragma once

#include <QCheckBox>
#include <QColor>
#include <QFocusEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>

#include <algorithm>

namespace ui {

// Theme colours, matching the Windows launcher's owner-drawn palette (ThBg/ThText/... there).
struct Theme { QColor bg, text, field, btn, border, accent, disabled; };
inline Theme ThemeFor(bool dark) {
    if (dark) return { QColor(30, 30, 30), QColor(225, 225, 225), QColor(45, 45, 48), QColor(55, 55, 58),
                       QColor(95, 95, 100), QColor(205, 175, 95), QColor(120, 120, 120) };
    return { QColor(245, 246, 249), QColor(32, 34, 40), QColor(255, 255, 255), QColor(236, 238, 242),
             QColor(205, 209, 217), QColor(178, 134, 48), QColor(150, 150, 150) };
}
inline bool g_dark = false;   // the active theme, read by the custom-painted controls below

inline QColor Lighten(QColor c, int d) { return QColor(std::min(255, c.red() + d), std::min(255, c.green() + d), std::min(255, c.blue() + d)); }
inline QColor Darken(QColor c, int d)  { return QColor(std::max(0, c.red() - d), std::max(0, c.green() - d), std::max(0, c.blue() - d)); }
inline QColor Alpha(QColor c, int a)   { c.setAlpha(a); return c; }
// Stroke a rounded rect whose OUTER edge is r (the pen is inset by half its width, like GpStrokeRound).
inline void StrokeRound(QPainter& p, QRectF r, qreal rad, QColor col, qreal pw) {
    p.setPen(QPen(col, pw)); p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(pw / 2, pw / 2, -pw / 2, -pw / 2), rad, rad);
}
// Keyboard focus only (Tab / arrows), so the mouse doesn't leave focus rings behind.
inline bool KeyboardFocus(Qt::FocusReason r) {
    return r == Qt::TabFocusReason || r == Qt::BacktabFocusReason || r == Qt::ShortcutFocusReason;
}

// A checkbox drawn as a sliding switch + label (the Windows launcher's PaintToggle). It stays a real
// QCheckBox, so clicks, Space and toggled() behave normally. The switch sits on the left so every
// row's switch lines up regardless of label length.
class Toggle : public QCheckBox {
public:
    Toggle(const QString& text, QWidget* parent) : QCheckBox(text, parent) { setCursor(Qt::PointingHandCursor); }
    bool contrastLabel = false;   // label sits on the banner's inverse half (Auto-Update)
protected:
    void focusInEvent(QFocusEvent* e) override { kbFocus_ = KeyboardFocus(e->reason()); QCheckBox::focusInEvent(e); }
    void focusOutEvent(QFocusEvent* e) override { kbFocus_ = false; QCheckBox::focusOutEvent(e); }
    bool hitButton(const QPoint& pos) const override { return rect().contains(pos); }
    void paintEvent(QPaintEvent*) override {
        Theme t = ThemeFor(g_dark);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool on = isChecked(), en = isEnabled(), focused = hasFocus() && kbFocus_;
        const qreal pw = 30, ph = std::min(18, height() - 2), px = 2, py = (height() - ph) / 2.0;
        QColor track = !en ? (g_dark ? QColor(74, 74, 82) : QColor(210, 212, 218))
                     : on  ? t.accent
                           : (g_dark ? QColor(92, 92, 100) : QColor(176, 178, 186));
        p.setPen(Qt::NoPen);
        p.setBrush(track);
        p.drawRoundedRect(QRectF(px, py, pw, ph), ph / 2, ph / 2);
        // Knob with a soft drop shadow (stacked low-alpha ellipses).
        const qreal kd = ph - 4, kx = on ? px + pw - kd - 2 : px + 2, ky = py + 2;
        for (int i = 2; i >= 1; --i) {
            p.setBrush(QColor(0, 0, 0, i == 2 ? 16 : 26));
            p.drawEllipse(QRectF(kx - 0.4, ky + i * 0.9, kd + 0.8, kd + 0.8));
        }
        p.setBrush(en ? QColor(255, 255, 255) : (g_dark ? QColor(150, 150, 158) : QColor(238, 240, 244)));
        p.drawEllipse(QRectF(kx, ky, kd, kd));
        if (focused) StrokeRound(p, QRectF(px - 2, py - 2.5, pw + 4, ph + 5), (ph + 5) / 2, t.accent, 2);
        if (!text().isEmpty()) {
            QColor tc = !en ? t.disabled
                      : contrastLabel ? (g_dark ? QColor(20, 20, 20) : QColor(230, 230, 230))
                      : focused ? t.accent : t.text;
            p.setPen(tc);
            QRectF tr(px + pw + 6, 0, width() - (px + pw + 6) - 4, height());
            p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter,
                       fontMetrics().elidedText(text(), Qt::ElideRight, int(tr.width())));
        }
    }
private:
    bool kbFocus_ = false;
};

// Rounded, gradient push button with a soft shadow (the Windows launcher's ThemeDrawButton). The
// primary one (PLAY) is filled with the gold accent; the rest are neutral surfaces.
class RoundButton : public QPushButton {
public:
    RoundButton(const QString& text, QWidget* parent, bool primary = false)
        : QPushButton(text, parent), primary_(primary) { setCursor(Qt::PointingHandCursor); }
protected:
    void focusInEvent(QFocusEvent* e) override { kbFocus_ = KeyboardFocus(e->reason()); QPushButton::focusInEvent(e); }
    void focusOutEvent(QFocusEvent* e) override { kbFocus_ = false; QPushButton::focusOutEvent(e); }
    void paintEvent(QPaintEvent*) override {
        Theme t = ThemeFor(g_dark);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool pressed = isDown(), focused = hasFocus() && kbFocus_, en = isEnabled();
        QColor base = primary_ ? t.accent : t.btn;
        QColor top = Lighten(base, primary_ ? 18 : 12), bot = Darken(base, primary_ ? 20 : 12);
        if (pressed) { QColor tmp = top; top = Darken(bot, 6); bot = tmp; }   // inverted = pushed in
        const qreal rad = 7, bx = 0.5, bw = width() - 1.0, bh = height() - 1.0;
        p.setPen(Qt::NoPen);
        if (!pressed)
            for (int i = 3; i >= 1; --i) {
                p.setBrush(QColor(0, 0, 0, i == 3 ? 10 : i == 2 ? 16 : 22));
                p.drawRoundedRect(QRectF(bx, 0.5 + i, bw, bh), rad, rad);
            }
        QLinearGradient g(0, -0.5, 0, bh + 1.5);
        g.setColorAt(0, top); g.setColorAt(1, bot);
        p.setBrush(g);
        p.drawRoundedRect(QRectF(bx, 0.5, bw, bh), rad, rad);
        StrokeRound(p, QRectF(bx + 1, 1.5, bw - 2, bh - 2), rad - 1, QColor(255, 255, 255, primary_ ? 70 : 55), 1);
        StrokeRound(p, QRectF(bx, 0.5, bw, bh), rad,
                    primary_ ? Darken(t.accent, 30) : Alpha(t.border, 170), 1);
        if (focused)
            StrokeRound(p, QRectF(bx + 2, 2.5, bw - 4, bh - 4), rad - 1,
                        g_dark ? QColor(255, 235, 170) : QColor(255, 255, 255), 1.6);
        p.setPen(!en ? t.disabled
                     : primary_ ? (g_dark ? QColor(28, 22, 10) : QColor(255, 250, 242))
                     : t.text);
        p.drawText(rect().adjusted(0, pressed ? 1 : 0, 0, pressed ? 1 : 0), Qt::AlignCenter, text());
    }
private:
    bool primary_;
    bool kbFocus_ = false;
};

} // namespace ui
