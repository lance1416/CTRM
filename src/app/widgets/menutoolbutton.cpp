// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/menutoolbutton.h"

#include <QApplication>
#include <QStyleOptionToolButton>
#include <QStylePainter>

namespace ctrm::app {

MenuToolButton::MenuToolButton(QWidget *parent)
    : QToolButton(parent)
{
    setPopupMode(QToolButton::InstantPopup);
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}

void MenuToolButton::initOption(QStyleOptionToolButton *option) const
{
    initStyleOption(option);
    // The style must not draw its own arrow.
    option->features &=
            ~(QStyleOptionToolButton::HasMenu | QStyleOptionToolButton::MenuButtonPopup |
              QStyleOptionToolButton::PopupDelay);
    option->subControls &= ~QStyle::SC_ToolButtonMenu;
}

int MenuToolButton::arrowSize() const
{
    return qMax(7, fontMetrics().height() / 2);
}

int MenuToolButton::arrowGap() const
{
    return qMax(4, fontMetrics().averageCharWidth() / 2);
}

QSize MenuToolButton::sizeHint() const
{
    ensurePolished();
    QStyleOptionToolButton opt;
    initOption(&opt);
    const QSize text = fontMetrics().size(Qt::TextShowMnemonic, opt.text);
    int w = text.width() + arrowGap() + arrowSize();
    int h = qMax(text.height(), arrowSize());
    if (!opt.icon.isNull() && opt.toolButtonStyle != Qt::ToolButtonTextOnly) {
        w += opt.iconSize.width() + 4;
        h = qMax(h, opt.iconSize.height());
    }
    return style()->sizeFromContents(QStyle::CT_ToolButton, &opt, QSize(w, h), this);
}

QSize MenuToolButton::minimumSizeHint() const
{
    return sizeHint();
}

QRect MenuToolButton::labelRect(const QStyleOptionToolButton &option) const
{
    QRect r = style()->subControlRect(QStyle::CC_ToolButton, &option, QStyle::SC_ToolButton, this);
    const int fw = style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &option, this);
    r.adjust(fw, fw, -fw, -fw);
    // The label and the arrow are one group in the centre.
    const int group = sizeHint().width() - (width() - r.width());
    const int spare = qMax(0, r.width() - group);
    r.setLeft(r.left() + spare / 2);
    r.setWidth(qMax(0, qMin(r.width(), group) - arrowGap() - arrowSize()));
    return r;
}

QRect MenuToolButton::arrowRect() const
{
    QStyleOptionToolButton opt;
    initOption(&opt);
    const QRect label = labelRect(opt);
    const int a = arrowSize();
    return QRect(label.right() + 1 + arrowGap(), label.center().y() - a / 2 + 1, a, a);
}

void MenuToolButton::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionToolButton opt;
    initOption(&opt);

    QStyleOptionToolButton bevel = opt;
    bevel.text.clear();
    bevel.icon = QIcon();
    painter.drawComplexControl(QStyle::CC_ToolButton, bevel);

    QStyleOptionToolButton label = opt;
    label.rect = labelRect(opt);
    painter.drawControl(QStyle::CE_ToolButtonLabel, label);

    QStyleOption arrow;
    arrow.initFrom(this);
    arrow.rect = arrowRect();
    painter.drawPrimitive(QStyle::PE_IndicatorArrowDown, arrow);
}

void useBodyFont(QWidget *widget)
{
    QFont body = QApplication::font();
    // An explicit size, so a class font of the style (QToolButton on macOS) does not apply.
    if (body.pointSizeF() > 0)
        body.setPointSizeF(body.pointSizeF());
    else
        body.setPixelSize(body.pixelSize());
    widget->setFont(body);
}

} // namespace ctrm::app
