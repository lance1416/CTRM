// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QPalette>

namespace ctrm::app {

// amountOfA * a + (1 - amountOfA) * b, for each channel.
QColor mix(const QColor &a, const QColor &b, qreal amountOfA);

// The colours that CTRM owns (design-tokens 2.3 and 2.4). Make them again when the palette
// changes.
struct CtrmColors
{
    QColor stateChange, stateConflict, stateWarning, stateError, stateDone, stateNeutral;
    QColor tintConflict, tintError;
    // Control fills (design-tokens 4.4): text over window. Rest fill of text fields, combo
    // boxes, spin boxes and secondary push buttons; hover and pressed of each button type.
    QColor fillControl, fillHover, fillPressed;
    // The `window` surface (design-tokens 2.1, surface rule): the palette Window, or `text` 8%
    // over `base` when the palette gives Window almost equal to Base (macOS light mode).
    QColor window;
    // The primary button fill (design-tokens 2.2): accent, darker until on-accent text has 4.5:1
    // on it, also on hover (8% lighter).
    QColor accentFill;
    // Text and marks on accent-fill: white in both modes (audit R5-01).
    QColor onAccent;
    bool tintsEnabled = true; // false on a high-contrast theme
    static CtrmColors fromPalette(const QPalette &p);
};

// The percent step of the primary button fill on hover (lighter) and when pressed (darker).
inline constexpr int kAccentStateStep = 108;

// WCAG 2.1 contrast ratio, from 1 to 21. The order of the colours is not important.
double contrastRatio(const QColor &a, const QColor &b);

namespace detail {
// The high-contrast heuristic: the colour scheme is not known, and text and base are pure black
// and pure white.
bool isHighContrast(Qt::ColorScheme scheme, const QColor &text, const QColor &base);
} // namespace detail

} // namespace ctrm::app
