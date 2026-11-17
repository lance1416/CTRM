// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/itemdelegate.h"

#include "models/itemtablemodel.h"
#include "text/messages.h"
#include "widgets/itemviewpaint.h"

#include <QApplication>
#include <QPainter>

namespace ctrm::app {

namespace {

struct RowFacts
{
    Status status = Status::Unchanged;
    PostRun postRun = PostRun::None;
    bool excluded = false;
    bool blocking = false;
    bool warning = false;
};

RowFacts rowFacts(const QModelIndex &index)
{
    RowFacts f;
    f.status = Status(index.data(ItemTableModel::StatusRole).toInt());
    f.postRun = PostRun(index.data(ItemTableModel::PostRunRole).toInt());
    f.excluded = index.data(ItemTableModel::IsExcludedRole).toBool();
    f.blocking = index.data(ItemTableModel::IsBlockingRole).toBool();
    f.warning = index.data(ItemTableModel::HasWarningRole).toBool();
    return f;
}

} // namespace

ItemDelegate::ItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
    , m_colors(CtrmColors::fromPalette(QApplication::palette()))
{}

void ItemDelegate::setColors(const CtrmColors &colors)
{
    m_colors = colors;
}

void ItemDelegate::setHighlightChanged(bool on)
{
    m_highlightChanged = on;
}

bool ItemDelegate::highlightChanged() const
{
    return m_highlightChanged;
}

std::optional<StateIcon> ItemDelegate::rowStateIcon(const QModelIndex &index)
{
    const RowFacts f = rowFacts(index);
    switch (f.postRun) {
    case PostRun::Renamed:
        return StateIcon::Done;
    case PostRun::Failed:
        return StateIcon::Error;
    case PostRun::Skipped:
        return StateIcon::Excluded;
    case PostRun::RolledBack:
        return StateIcon::RolledBack;
    case PostRun::None:
        break;
    }
    if (f.excluded)
        return StateIcon::Excluded;
    if (f.blocking)
        return f.status == Status::Conflict ? StateIcon::Conflict : StateIcon::Error;
    // A warning replaces the change icon (ui-design 7.2).
    if (f.warning)
        return StateIcon::Warning;
    if (f.status == Status::Changed || f.status == Status::CaseOnly)
        return StateIcon::Change;
    return std::nullopt;
}

QString ItemDelegate::rowStateName(const QModelIndex &index)
{
    const auto icon = rowStateIcon(index);
    if (!icon)
        return tr("No change");
    switch (*icon) {
    case StateIcon::Change:
        return tr("Change");
    case StateIcon::Conflict:
        return tr("Conflict");
    case StateIcon::Error:
        return tr("Error");
    case StateIcon::Warning:
        return tr("Warning");
    case StateIcon::Done:
        return tr("Renamed");
    case StateIcon::RolledBack:
        return tr("Rolled back");
    case StateIcon::Excluded:
        return rowFacts(index).postRun == PostRun::Skipped ? tr("Skipped") : tr("Excluded");
    }
    return {};
}

void ItemDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    const RowFacts f = rowFacts(index);
    const int column = index.column();

    if (column == ItemTableModel::State) {
        option->text.clear();
        option->icon = QIcon();
        option->features &= ~QStyleOptionViewItem::HasDecoration;
        if (const auto icon = rowStateIcon(index)) {
            option->icon = stateIcon(*icon, m_colors);
            option->features |= QStyleOptionViewItem::HasDecoration;
        }
        option->decorationAlignment = Qt::AlignCenter;
        option->displayAlignment = Qt::AlignCenter;
        const QWidget *widget = option->widget;
        const QStyle *style = widget ? widget->style() : QApplication::style();
        const int size = style->pixelMetric(QStyle::PM_SmallIconSize, nullptr, widget);
        option->decorationSize = QSize(size, size);
    }

    // Names elide in the middle (the view's mode). A message has its key word first, so it
    // elides at the end (ui-design 7.1, audit M07).
    if (column == ItemTableModel::Message)
        option->textElideMode = Qt::ElideRight;

    // Tints (design-tokens 2.4): only blocking rows and failed rows.
    option->backgroundBrush = QBrush();
    if (m_colors.tintsEnabled) {
        if (f.postRun == PostRun::Failed)
            option->backgroundBrush = m_colors.tintError;
        else if (f.postRun == PostRun::None && f.blocking)
            option->backgroundBrush =
                    f.status == Status::Conflict ? m_colors.tintConflict : m_colors.tintError;
    }

    // Dimmed text: excluded and skipped rows, and the new name of an unchanged row.
    const bool dimRow = f.postRun == PostRun::Skipped || (f.postRun == PostRun::None && f.excluded);
    const bool dimNewName = column == ItemTableModel::NewName && f.postRun == PostRun::None &&
                            f.status == Status::Unchanged;
    if (dimRow || dimNewName) {
        const QColor dim = option->palette.color(QPalette::PlaceholderText);
        option->palette.setColor(QPalette::Text, dim);
        option->palette.setColor(QPalette::WindowText, dim);
    }
}

void ItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const
{
    QStyleOptionViewItem item = option;
    initStyleOption(&item, index);
    paintItemViewItem(painter, item);
    if (!m_highlightChanged || index.column() != ItemTableModel::NewName)
        return;
    const RowFacts f = rowFacts(index);
    if (f.postRun != PostRun::None || f.excluded ||
        (f.status != Status::Changed && f.status != Status::CaseOnly))
        return;

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QString newName = opt.text;
    const QString oldName = index.siblingAtColumn(ItemTableModel::OldName).data().toString();

    // The changed part is between the common start and the common end of the two names.
    qsizetype prefix = 0;
    const qsizetype shorter = qMin(oldName.size(), newName.size());
    while (prefix < shorter && oldName[prefix] == newName[prefix])
        ++prefix;
    qsizetype suffix = 0;
    while (suffix < shorter - prefix &&
           oldName[oldName.size() - 1 - suffix] == newName[newName.size() - 1 - suffix])
        ++suffix;
    const qsizetype end = newName.size() - suffix;
    if (end <= prefix)
        return;

    const QWidget *widget = opt.widget;
    const QStyle *style = widget ? widget->style() : QApplication::style();
    QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
    const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, widget) + 1;
    textRect.adjust(margin, 0, -margin, 0);
    const QFontMetrics metrics(opt.font);
    // An elided name has no exact character positions: no highlight.
    if (metrics.horizontalAdvance(newName) > textRect.width())
        return;
    const bool rtl = opt.direction == Qt::RightToLeft;
    const int x0 = metrics.horizontalAdvance(newName.left(prefix));
    const int x1 = metrics.horizontalAdvance(newName.left(end));
    QRect band(textRect.left() + x0, textRect.top(), x1 - x0, textRect.height());
    if (rtl)
        band.moveRight(textRect.right() - x0);
    QColor colour = opt.palette.color(QPalette::Highlight);
    colour.setAlphaF(0.25);
    painter->fillRect(band, colour);
}

} // namespace ctrm::app
