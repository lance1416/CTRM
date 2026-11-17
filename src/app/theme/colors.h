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
    bool tintsEnabled = true; // false on a high-contrast theme
    static CtrmColors fromPalette(const QPalette &p);
};

// WCAG 2.1 contrast ratio, from 1 to 21. The order of the colours is not important.
double contrastRatio(const QColor &a, const QColor &b);

namespace detail {
// The high-contrast heuristic: the colour scheme is not known, and text and base are pure black
// and pure white.
bool isHighContrast(Qt::ColorScheme scheme, const QColor &text, const QColor &base);
} // namespace detail

} // namespace ctrm::app
