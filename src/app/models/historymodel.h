// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/historystore.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QList>

namespace ctrm::app {

// The batch table of the History window (ui-design 10.1), newest first.
class HistoryModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { When, Items, Folders, State, ColumnCount };

    explicit HistoryModel(QObject *parent = nullptr);

    void setBatches(const QList<ctrm::BatchSummary> &batches);
    const ctrm::BatchSummary &batch(int row) const;
    int rowOf(const QString &batchId) const; // -1 when not in the list

    // "Today 14:02", "Yesterday 18:15", else "14 Nov 10:05" (system locale).
    static QString whenText(const QDateTime &time, const QDateTime &now);
    // One folder (home as "~"), "%n folders", or "Undo of <when of the original>".
    static QString foldersText(const ctrm::BatchSummary &s, const QList<ctrm::BatchSummary> &all);
    // "Complete", "Complete, 3 failed", "Damaged", ...
    static QString stateText(const ctrm::BatchSummary &s);
    // The start time. For a damaged journal with no readable header, the time in the file
    // name; invalid when the file name has none (the When column shows "Unknown").
    static QDateTime startedOf(const ctrm::BatchSummary &s);
    // A path for the user: native separators, the home folder as "~".
    static QString displayPath(const QString &path);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    QList<ctrm::BatchSummary> m_batches;
};

} // namespace ctrm::app
