// SPDX-License-Identifier: GPL-3.0-or-later
#include "tertiarybutton.h"

#include <QStyleOptionToolButton>
#include <QStylePainter>

namespace ctrm::app {

TertiaryButton::TertiaryButton(QWidget *parent)
    : QToolButton(parent)
{
    // autoRaise repaints the button when the mouse enters and leaves it.
    setAutoRaise(true);
}

bool TertiaryButton::framed() const
{
    return isDown() || hasFocus() || (isEnabled() && underMouse());
}

void TertiaryButton::paintEvent(QPaintEvent * /*event*/)
{
    QStylePainter painter(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    if (framed()) {
        painter.drawComplexControl(QStyle::CC_ToolButton, option);
        return;
    }
    // As the label part of QCommonStyle's CC_ToolButton, with no panel.
    const int frame = style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &option, this);
    option.rect =
            style()->subControlRect(QStyle::CC_ToolButton, &option, QStyle::SC_ToolButton, this)
                    .adjusted(frame, frame, -frame, -frame);
    option.state &= ~(QStyle::State_Sunken | QStyle::State_On | QStyle::State_Raised);
    painter.drawControl(QStyle::CE_ToolButtonLabel, option);
}

} // namespace ctrm::app
