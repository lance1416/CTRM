// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/buttonstyle.h"

#include <QPushButton>
#include <QStyle>

namespace ctrm::app {

void setPrimary(QPushButton *button, bool primary)
{
    button->setAutoDefault(false);
    button->setDefault(primary);
    // The inherited palette first, so the accent comes from the current parent palette.
    button->setPalette(QPalette());
    const bool fusion =
            button->style()->name().compare(QLatin1StringView("fusion"), Qt::CaseInsensitive) == 0;
    if (!fusion || !primary)
        return;
    const QPalette inherited = button->palette();
    QPalette accent;
    accent.setColor(QPalette::Button, inherited.color(QPalette::Accent));
    accent.setColor(QPalette::ButtonText, inherited.color(QPalette::HighlightedText));
    button->setPalette(accent);
}

} // namespace ctrm::app
