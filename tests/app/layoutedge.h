// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QGridLayout>
#include <QLayout>
#include <QList>
#include <QRect>
#include <QWidget>

namespace ctrm::test {

// The item of layout (or of a layout in it) that holds w, or nullptr.
inline QLayoutItem *layoutItemOf(QLayout *layout, const QWidget *w)
{
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (item->widget() == w)
            return item;
        if (QLayout *inner = item->layout()) {
            if (QLayoutItem *found = layoutItemOf(inner, w))
                return found;
        }
    }
    return nullptr;
}

// The rect that the user sees as the edge of w, relative to `to`: the layout item rect. A style
// (for example macOS) draws a widget inside its geometry and gives the layout margins for that,
// so the widget x is not the visual edge.
inline QRect visualRect(const QWidget *w, const QWidget *to)
{
    QRect r = w->geometry();
    QWidget *parent = w->parentWidget();
    if (parent && parent->layout()) {
        if (QLayoutItem *item = layoutItemOf(parent->layout(), w))
            r = item->geometry();
    }
    if (!parent)
        return r;
    return {parent->mapTo(to, r.topLeft()), r.size()};
}

inline int visualLeft(const QWidget *w, const QWidget *to)
{
    return visualRect(w, to).left();
}

// True when item (a widget or a layout) holds w, also in a child widget.
inline bool itemHolds(QLayoutItem *item, const QWidget *w)
{
    if (!item)
        return false;
    if (QWidget *field = item->widget())
        return field == w || field->isAncestorOf(w);
    if (QLayout *layout = item->layout()) {
        for (int i = 0; i < layout->count(); ++i) {
            if (itemHolds(layout->itemAt(i), w))
                return true;
        }
    }
    return false;
}

// The visible left edges of a form field, relative to `to`: the widget, or the visible widgets in
// column 0 of a grid (a field group).
inline QList<int> fieldVisualLefts(QLayoutItem *item, const QWidget *to)
{
    QList<int> lefts;
    if (QWidget *w = item->widget()) {
        lefts.append(visualLeft(w, to));
    } else if (auto *grid = qobject_cast<QGridLayout *>(item->layout())) {
        for (int i = 0; i < grid->count(); ++i) {
            int row = 0;
            int column = 0;
            int rowSpan = 0;
            int columnSpan = 0;
            grid->getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
            QWidget *cell = grid->itemAt(i)->widget();
            if (column == 0 && cell && !cell->isHidden())
                lefts.append(visualLeft(cell, to));
        }
    }
    return lefts;
}

} // namespace ctrm::test
