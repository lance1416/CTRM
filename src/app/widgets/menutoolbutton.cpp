// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/menutoolbutton.h"

#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/tokens.h"

#include <QApplication>
#include <QPainter>
#include <QStyleOptionToolButton>
#include <QStylePainter>

namespace ctrm::app {

namespace {

constexpr qreal kDisabledIcon = 0.35; // design-tokens 4.5: a disabled icon at 35%

} // namespace

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

bool MenuToolButton::iconShown(const QStyleOptionToolButton &option) const
{
    return !option.icon.isNull() && option.toolButtonStyle != Qt::ToolButtonTextOnly;
}

int MenuToolButton::contentWidth(const QStyleOptionToolButton &option) const
{
    int w = fontMetrics().size(Qt::TextShowMnemonic, option.text).width() + tokens::space1 +
            tokens::iconXs;
    if (iconShown(option))
        w += tokens::iconS + tokens::space1;
    return w;
}

int MenuToolButton::contentLeft(const QStyleOptionToolButton &option) const
{
    // At the size hint this is the padding of the style (space-2 in the toolbar); in a wider
    // button the content stays in the centre.
    return option.rect.left() + qMax(0, (option.rect.width() - contentWidth(option)) / 2);
}

QSize MenuToolButton::sizeHint() const
{
    ensurePolished();
    QStyleOptionToolButton opt;
    initOption(&opt);
    int h = qMax(fontMetrics().height(), tokens::iconXs);
    if (iconShown(opt))
        h = qMax(h, tokens::iconS);
    return style()->sizeFromContents(QStyle::CT_ToolButton, &opt, QSize(contentWidth(opt), h),
                                     this);
}

QSize MenuToolButton::minimumSizeHint() const
{
    return sizeHint();
}

QRect MenuToolButton::arrowRect() const
{
    QStyleOptionToolButton opt;
    initOption(&opt);
    // M17: on the text line, which is in the vertical centre of the button.
    const int left = contentLeft(opt) + contentWidth(opt) - tokens::iconXs;
    return QRect(left, opt.rect.top() + (opt.rect.height() - tokens::iconXs) / 2, tokens::iconXs,
                 tokens::iconXs);
}

void MenuToolButton::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionToolButton opt;
    initOption(&opt);

    // The bevel: the style draws the fills of a toolbar text button (design-tokens 4.5).
    QStyleOptionToolButton bevel = opt;
    bevel.text.clear();
    bevel.icon = QIcon();
    painter.drawComplexControl(QStyle::CC_ToolButton, bevel);

    const bool enabled = opt.state & QStyle::State_Enabled;
    const QColor text = enabled ? opt.palette.color(QPalette::ButtonText)
                                : CtrmColors::fromPalette(opt.palette).stateNeutral;
    const qreal dpr = devicePixelRatioF();
    int x = contentLeft(opt);
    if (iconShown(opt)) {
        const QPixmap icon = opt.icon.pixmap(QSize(tokens::iconS, tokens::iconS), dpr);
        painter.setOpacity(enabled ? 1.0 : kDisabledIcon);
        painter.drawPixmap(
                QStyle::visualRect(opt.direction, opt.rect,
                                   QRect(x,
                                         opt.rect.top() + (opt.rect.height() - tokens::iconS) / 2,
                                         tokens::iconS, tokens::iconS)),
                icon);
        painter.setOpacity(1.0);
        x += tokens::iconS + tokens::space1;
    }
    const QRect arrow = arrowRect();
    int flags = Qt::AlignVCenter | Qt::AlignLeading | Qt::TextSingleLine;
    flags |= style()->styleHint(QStyle::SH_UnderlineShortcut, &opt, this) ? Qt::TextShowMnemonic
                                                                          : Qt::TextHideMnemonic;
    painter.setPen(text);
    painter.drawText(QStyle::visualRect(opt.direction, opt.rect,
                                        QRect(x, opt.rect.top(), arrow.left() - tokens::space1 - x,
                                              opt.rect.height())),
                     flags, opt.text);

    // C01: the chevron of the icon set, in the text colour.
    QPalette chevronPalette = opt.palette;
    chevronPalette.setColor(QPalette::WindowText, text);
    painter.drawPixmap(QStyle::visualRect(opt.direction, opt.rect, arrow),
                       toolIcon(QStringLiteral("chevron-xs-down"), chevronPalette)
                               .pixmap(QSize(tokens::iconXs, tokens::iconXs), dpr));
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
