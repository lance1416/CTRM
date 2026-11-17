// SPDX-License-Identifier: GPL-3.0-or-later
#include "models/itemfilterproxy.h"

#include "model/status.h"
#include "models/itemtablemodel.h"

namespace ctrm::app {

template <typename Change>
void ItemFilterProxy::changeFilter(Change change)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    change();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    change();
    invalidateRowsFilter();
#endif
}

ItemFilterProxy::ItemFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // The filter reads the custom roles, so data changes must filter again.
    setDynamicSortFilter(true);
}

void ItemFilterProxy::setShowMode(ShowMode m)
{
    if (m == m_mode)
        return;
    changeFilter([&] { m_mode = m; });
}

ShowMode ItemFilterProxy::showMode() const
{
    return m_mode;
}

void ItemFilterProxy::setText(const QString &t)
{
    if (t == m_text)
        return;
    changeFilter([&] { m_text = t; });
}

QString ItemFilterProxy::text() const
{
    return m_text;
}

bool ItemFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *m = sourceModel();
    const QModelIndex idx = m->index(sourceRow, ItemTableModel::OldName, sourceParent);

    if (!m_text.isEmpty()) {
        const QString oldName = idx.data(Qt::DisplayRole).toString();
        const QString newName =
                m->index(sourceRow, ItemTableModel::NewName, sourceParent).data().toString();
        if (!oldName.contains(m_text, Qt::CaseInsensitive) &&
            !newName.contains(m_text, Qt::CaseInsensitive))
            return false;
    }

    const bool excluded = idx.data(ItemTableModel::IsExcludedRole).toBool();
    switch (m_mode) {
    case ShowMode::All:
        return true;
    case ShowMode::Changed: {
        const auto s = Status(idx.data(ItemTableModel::StatusRole).toInt());
        return !excluded && (s == Status::Changed || s == Status::CaseOnly);
    }
    case ShowMode::Problems:
        return idx.data(ItemTableModel::IsBlockingRole).toBool() ||
               PostRun(idx.data(ItemTableModel::PostRunRole).toInt()) == PostRun::Failed;
    case ShowMode::Warnings:
        return idx.data(ItemTableModel::HasWarningRole).toBool();
    }
    return true;
}

} // namespace ctrm::app
