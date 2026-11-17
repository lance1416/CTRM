// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFont>

namespace ctrm::app {

// The five type styles of design-tokens 3.2. Do not add more.
enum class TypeToken { Display, Heading, LabelStrong, Body, Caption };

// The style as a ratio of base (normally QApplication::font()). On Windows and Linux the
// caption is never smaller than 10 pt.
QFont typeStyle(TypeToken t, const QFont &base);

// Tabular figures ("tnum"), so numbers do not move when they change.
QFont tabularFigures(QFont f);

namespace detail {
// typeStyle with the caption minimum given by the caller, for tests on all platforms.
QFont typeStyle(TypeToken t, const QFont &base, bool captionMinimum);
} // namespace detail

} // namespace ctrm::app
