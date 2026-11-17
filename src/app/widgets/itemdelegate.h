// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "theme/colors.h"
#include "theme/icons.h"

#include <QStyledItemDelegate>

#include <optional>

namespace ctrm::app {

// Draws the rows of the item table (ui-design 7.2): the state icon, the row tints, the dimmed
// text, and the optional highlight of the changed characters.
class ItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ItemDelegate(QObject *parent);

    void setColors(const CtrmColors &colors);
    void setHighlightChanged(bool on);
    bool highlightChanged() const;

    // Public, so tests can read the look of a cell.
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    // The state icon of the row of index (any column); nothing for an unchanged row.
    static std::optional<StateIcon> rowStateIcon(const QModelIndex &index);
    // The name of the state for the screen reader, for example "Conflict".
    static QString rowStateName(const QModelIndex &index);

private:
    CtrmColors m_colors;
    bool m_highlightChanged = false;
};

} // namespace ctrm::app
