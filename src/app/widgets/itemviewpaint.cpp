// SPDX-License-Identifier: GPL-3.0-or-later
#include "itemviewpaint.h"

#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

namespace ctrm::app {

void paintItemViewItem(QPainter *painter, const QStyleOptionViewItem &option)
{
    const QWidget *widget = option.widget;
    const QStyle *style = widget ? widget->style() : QApplication::style();
    if (!(option.features & QStyleOptionViewItem::HasCheckIndicator)) {
        style->drawControl(QStyle::CE_ItemViewItem, &option, painter, widget);
        return;
    }
    const QRect check =
            style->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, widget);

    // The background of the full item.
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, painter, widget);

    // The contents with no indicator, moved so that the text starts where it starts with one.
    QStyleOptionViewItem rest = option;
    rest.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    const int with = style->subElementRect(QStyle::SE_ItemViewItemText, &option, widget).left();
    const int without = style->subElementRect(QStyle::SE_ItemViewItemText, &rest, widget).left();
    if (option.direction == Qt::RightToLeft)
        rest.rect.setRight(rest.rect.right() - (without - with));
    else
        rest.rect.setLeft(rest.rect.left() + (with - without));
    // The background is drawn: no second selection or brush on the contents.
    rest.backgroundBrush = Qt::NoBrush;
    if (option.state & QStyle::State_Selected) {
        rest.state &= ~QStyle::State_Selected;
        rest.palette.setColor(QPalette::Text, option.palette.color(QPalette::HighlightedText));
    }
    style->drawControl(QStyle::CE_ItemViewItem, &rest, painter, widget);

    QStyleOptionViewItem indicator = option;
    indicator.rect = QRect(QPoint(0, 0), check.size());
    indicator.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange);
    switch (option.checkState) {
    case Qt::Unchecked:
        indicator.state |= QStyle::State_Off;
        break;
    case Qt::PartiallyChecked:
        indicator.state |= QStyle::State_NoChange;
        break;
    case Qt::Checked:
        indicator.state |= QStyle::State_On;
        break;
    }
    painter->save();
    painter->translate(check.topLeft());
    style->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &indicator, painter, widget);
    painter->restore();
}

} // namespace ctrm::app
