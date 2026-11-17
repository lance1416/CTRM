// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSortFilterProxyModel>
#include <QString>

namespace ctrm::app {

// Values of the "Show" control (ui-design 7.6).
enum class ShowMode { All, Changed, Problems, Warnings };

// Filters the rows of an ItemTableModel. It never sorts: the order stays the list order.
class ItemFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit ItemFilterProxy(QObject *parent = nullptr);

    void setShowMode(ShowMode m);
    ShowMode showMode() const;
    // Matches the old or the new name, case-insensitive. Empty: all rows.
    void setText(const QString &t);
    QString text() const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    template <typename Change>
    void changeFilter(Change change);

    ShowMode m_mode = ShowMode::All;
    QString m_text;
};

} // namespace ctrm::app
