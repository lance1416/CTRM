// SPDX-License-Identifier: GPL-3.0-or-later
#include "tertiarybutton.h"

#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/tokens.h"

#include <QPainter>
#include <QStyleOptionToolButton>

namespace ctrm::app {

namespace {

constexpr qreal kDisabledIcon = 0.35; // a disabled icon at 35% (design-tokens 4.5)
constexpr qreal kFocusRingWidth = 2.0;

void fillRounded(QPainter &painter, const QRectF &rect, const QColor &colour)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(rect, tokens::radiusHover, tokens::radiusHover);
}

} // namespace

TertiaryButton::TertiaryButton(QWidget *parent)
    : QToolButton(parent)
{
    // autoRaise repaints the button when the mouse enters and leaves it.
    setAutoRaise(true);
    setAttribute(Qt::WA_Hover);
}

bool TertiaryButton::isIconButton() const
{
    return m_iconButton;
}

void TertiaryButton::setIconButton(bool on)
{
    if (m_iconButton == on)
        return;
    m_iconButton = on;
    if (on) {
        setToolButtonStyle(Qt::ToolButtonIconOnly);
        setIconSize(QSize(tokens::iconS, tokens::iconS));
    }
    updateGeometry();
    update();
}

bool TertiaryButton::framed() const
{
    return isDown() || hasFocus() || (isEnabled() && underMouse());
}

QSize TertiaryButton::sizeHint() const
{
    if (m_iconButton)
        return {tokens::hitMin, tokens::hitMin};
    return QToolButton::sizeHint().expandedTo(QSize(tokens::hitMin, tokens::hitMin));
}

QSize TertiaryButton::minimumSizeHint() const
{
    return m_iconButton ? sizeHint() : QToolButton::minimumSizeHint();
}

void TertiaryButton::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    const CtrmColors c = CtrmColors::fromPalette(palette());
    const bool enabled = isEnabled();
    const bool hover = enabled && underMouse();

    // The fill: never the platform frame (it has a border line on macOS).
    if (enabled && isDown())
        fillRounded(painter, rect(), c.fillPressed);
    else if (hover)
        fillRounded(painter, rect(), c.fillHover);
    if ((option.state & QStyle::State_HasFocus) &&
        (option.state & QStyle::State_KeyboardFocusChange)) {
        painter.setPen(QPen(palette().color(QPalette::Accent), kFocusRingWidth));
        painter.setBrush(Qt::NoBrush);
        const qreal half = kFocusRingWidth / 2;
        painter.drawRoundedRect(QRectF(rect()).adjusted(half, half, -half, -half),
                                tokens::radiusHover, tokens::radiusHover);
    }

    if (m_iconButton) {
        // The icon in text-dim at rest, text on hover or press, at 35% when disabled.
        QColor colour = hover || isDown() ? palette().color(QPalette::WindowText) : c.stateNeutral;
        if (!enabled)
            colour.setAlphaF(kDisabledIcon);
        const QPixmap pixmap = tintedPixmap(icon(), iconSize(), devicePixelRatioF(), colour);
        const QRect target(QPoint(0, 0), iconSize());
        painter.drawPixmap(target.translated(rect().center() - target.center()), pixmap);
        return;
    }

    // As the label part of QCommonStyle's CC_ToolButton, with no panel.
    const int frame = style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &option, this);
    option.rect =
            style()->subControlRect(QStyle::CC_ToolButton, &option, QStyle::SC_ToolButton, this)
                    .adjusted(frame, frame, -frame, -frame);
    option.state &= ~(QStyle::State_Sunken | QStyle::State_On | QStyle::State_Raised);
    if (!enabled) {
        const QColor dim = c.stateNeutral;
        option.palette.setColor(QPalette::ButtonText, dim);
        option.palette.setColor(QPalette::WindowText, dim);
    }
    style()->drawControl(QStyle::CE_ToolButtonLabel, &option, &painter, this);
}

} // namespace ctrm::app
