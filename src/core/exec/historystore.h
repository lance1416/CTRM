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

// A step that was running when CTRM stopped: its start record has no result. The rename may
// or may not have happened. from and to are the rename of this phase (for Rollback, the reverse
// rename). A temporary name is a name only CTRM uses (".ctrm-tmp-...").
struct OpenStep
{
    int step = 0;
    Phase phase = Phase::Forward;
    QString from;
    QString to;
    bool fromIsTemp = false;
    bool toIsTemp = false;
};

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
    // The journal cannot be read in full. The fields come from a best-effort read; when not
    // even the header can be read, batchId and started come from the file name and the state
    // is InProgress (not known to be finished). error: the text of the strict read.
    // Steps that were running when CTRM stopped (shown before "Mark as Complete"), in journal
    // order. Empty for a journal with no readable header.
    QList<OpenStep> openSteps;
    bool damaged = false;
    QString error;
};

// The history folder: one journal file per batch. A journal that cannot be read in full is
// listed as damaged and is never deleted.
class HistoryStore
{
public:
    explicit HistoryStore(QString dir);
    QString dir() const;
    QList<BatchSummary> list() const; // newest first
    std::optional<BatchRecord> load(const QString &batchId, QString *error,
                                    ReadMode mode = ReadMode::Strict) const;
    QList<BatchSummary> inProgress() const;
    // Oldest first. Read on a best-effort basis, so that a damaged undo journal still counts.
    QList<BatchRecord> undoBatchesOf(const QString &batchId) const;
    bool appendState(const QString &batchId, const StateRecord &rec, QString *error);
    // An original batch and its undo batches form a group, deleted or kept together. A group is
    // deleted only when every member started more than keepDays before now, no member is in the
    // newest keepCount batches, and no member is in-progress, incomplete or damaged. Returns
    // the number of files deleted.
    int applyRetention(int keepCount, int keepDays, const QDateTime &now);
    // Deletes every group with no in-progress, incomplete or damaged member. Returns count
    // deleted.
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
