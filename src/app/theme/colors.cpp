// SPDX-License-Identifier: GPL-3.0-or-later
#include "colors.h"

#include <QGuiApplication>
#include <QStyleHints>

#include <algorithm>
#include <cmath>

namespace ctrm::app {

namespace {

// Tint sources (design-tokens 2.4).
constexpr QRgb kConflictTintSource = 0xE0A000;
constexpr QRgb kErrorTintSource = 0xD03030;
constexpr qreal kTintAmount = 0.12;
constexpr double kHighContrastMinimum = 15.0;
// Control fills: the amount of text over window (design-tokens 4.4).
constexpr qreal kFillControlDark = 0.10, kFillControlLight = 0.06;
constexpr qreal kFillHoverStepDark = 0.08, kFillHoverStepLight = 0.06;
// The primary button fill: darker in steps until white text has 4.5:1 (design-tokens 2.2).
constexpr double kOnAccentMinimum = 4.5;
constexpr int kAccentFillStep = 105; // percent, for QColor::darker
constexpr int kAccentFillSteps = 40;
constexpr qreal kFillPressedStepDark = 0.16, kFillPressedStepLight = 0.12;

double linear(double channel)
{
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor &c)
{
    const QColor rgb = c.toRgb();
    return 0.2126 * linear(rgb.redF()) + 0.7152 * linear(rgb.greenF()) +
           0.0722 * linear(rgb.blueF());
}

bool isPureBlackOrWhite(const QColor &c)
{
    const QRgb rgb = c.rgb() & RGB_MASK;
    return rgb == 0x000000 || rgb == 0xFFFFFF;
}

} // namespace

QColor mix(const QColor &a, const QColor &b, qreal amountOfA)
{
    const qreal t = std::clamp(amountOfA, 0.0, 1.0);
    const QColor ca = a.toRgb();
    const QColor cb = b.toRgb();
    auto channel = [t](int x, int y) { return qRound(x * t + y * (1.0 - t)); };
    return QColor(channel(ca.red(), cb.red()), channel(ca.green(), cb.green()),
                  channel(ca.blue(), cb.blue()));
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

namespace detail {

bool isHighContrast(Qt::ColorScheme scheme, const QColor &text, const QColor &base)
{
    return scheme == Qt::ColorScheme::Unknown && isPureBlackOrWhite(text) &&
           isPureBlackOrWhite(base) && contrastRatio(text, base) >= kHighContrastMinimum;
}

} // namespace detail

CtrmColors CtrmColors::fromPalette(const QPalette &p)
{
    const QColor base = p.color(QPalette::Base);
    const bool dark = base.lightnessF() < 0.5;
    CtrmColors c;
    c.stateChange = p.color(QPalette::Accent);
    c.stateConflict = dark ? QColor(0xF0B429) : QColor(0xA15C00);
    c.stateWarning = c.stateConflict;
    c.stateError = dark ? QColor(0xFF6B60) : QColor(0xC42B1C);
    c.stateDone = dark ? QColor(0x4CC76A) : QColor(0x1E7F34);
    // Some styles give a translucent placeholder colour. Make it opaque on base, so the
    // contrast ratio and the icons use the colour that the user sees.
    const QColor placeholder = p.color(QPalette::PlaceholderText);
    c.stateNeutral = mix(placeholder, base, placeholder.alphaF());
    c.tintConflict = mix(QColor(kConflictTintSource), base, kTintAmount);
    c.tintError = mix(QColor(kErrorTintSource), base, kTintAmount);
    const QColor text = p.color(QPalette::Text);
    const QColor window = p.color(QPalette::Window);
    const qreal rest = dark ? kFillControlDark : kFillControlLight;
    c.fillControl = mix(text, window, rest);
    c.fillHover = mix(text, window, rest + (dark ? kFillHoverStepDark : kFillHoverStepLight));
    c.fillPressed = mix(text, window, rest + (dark ? kFillPressedStepDark : kFillPressedStepLight));
    c.onAccent = QColor(Qt::white);
    c.accentFill = p.color(QPalette::Accent).toRgb();
    for (int i = 0; i < kAccentFillSteps && contrastRatio(c.accentFill.lighter(kAccentStateStep),
                                                          c.onAccent) < kOnAccentMinimum;
         ++i)
        c.accentFill = c.accentFill.darker(kAccentFillStep);
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    c.tintsEnabled = !detail::isHighContrast(scheme, p.color(QPalette::Text), base);
    return c;
}

} // namespace ctrm::app
