// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/historystore.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QHash>
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
    const QList<ctrm::BatchSummary> &batches() const;
    int rowOf(const QString &batchId) const; // -1 when not in the list

    // "Today 14:02", "Yesterday 18:15", else "14 Nov 10:05" (system locale). withSeconds:
    // "Today 14:02:07".
    static QString whenText(const QDateTime &time, const QDateTime &now, bool withSeconds = false);
    // whenText of s, with the seconds when another batch of all has the same text without them
    // (M13: the user can tell the batches apart).
    static QString whenText(const ctrm::BatchSummary &s, const QList<ctrm::BatchSummary> &all);
    QString whenText(const ctrm::BatchSummary &s) const; // from the cache of setBatches
    // whenText of each batch of all, by batch id.
    static QHash<QString, QString> whenTexts(const QList<ctrm::BatchSummary> &all);
    // One folder (home as "~"), "%n folders", or "Undo of <when of the original>".
    static QString foldersText(const ctrm::BatchSummary &s, const QList<ctrm::BatchSummary> &all);
    // "Complete", "Complete, 3 failed", "Damaged", ...
    static QString stateText(const ctrm::BatchSummary &s);
    // A path for the user: native separators, the home folder as "~".
    static QString displayPath(const QString &path);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    QList<ctrm::BatchSummary> m_batches;
    QHash<QString, QString> m_whenTexts; // batch id -> whenText, made in setBatches
};

} // namespace ctrm::app
