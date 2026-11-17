// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QLatin1StringView>
#include <QString>

namespace ctrm::app {

// Without a translation, Qt keeps "(s)" in a "%n" text. This makes the English plural of a text
// that tr(..., n) returned: "(s)" becomes "s", or nothing when n is 1. A translated text has no
// "(s)" and does not change.
inline QString numerus(QString text, int n)
{
    return text.replace(QLatin1StringView("(s)"), n == 1 ? QString() : QStringLiteral("s"));
}

} // namespace ctrm::app
