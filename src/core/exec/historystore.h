// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/journal.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace ctrm {

struct BatchSummary
{
    QString batchId;
    QString filePath;
    QDateTime started;
    QDateTime ended;
    int itemCount = 0;
    QStringList folders; // distinct request folders, first-use order
    BatchState state = BatchState::InProgress;
    QString undoOf;
    int failedCount = 0; // forward steps with result failed (History: "Complete, 3 failed")
};

// The history folder: one journal file per batch. Files that cannot be read are left alone
// (never listed, never deleted).
class HistoryStore
{
public:
    explicit HistoryStore(QString dir);
    QString dir() const;
    QList<BatchSummary> list() const; // newest first
    std::optional<BatchRecord> load(const QString &batchId, QString *error) const;
    QList<BatchSummary> inProgress() const;
    QList<BatchRecord> undoBatchesOf(const QString &batchId) const; // oldest first
    bool appendState(const QString &batchId, const StateRecord &rec, QString *error);
    // An original batch and its undo batches form a group, deleted or kept together. A group is
    // deleted only when every member started more than keepDays before now, no member is in the
    // newest keepCount batches, and no member is in-progress or incomplete. Returns the number
    // of files deleted.
    int applyRetention(int keepCount, int keepDays, const QDateTime &now);
    // Deletes every group with no in-progress or incomplete member. Returns count deleted.
    int clear();

private:
    QList<BatchSummary> readAll() const;
    QString fileOf(const QString &batchId) const;
    // Deletes the files of every group with no member for which keep() is true.
    int deleteGroups(const QList<BatchSummary> &all,
                     const std::function<bool(const BatchSummary &)> &keep);

    QString m_dir;
};

} // namespace ctrm
