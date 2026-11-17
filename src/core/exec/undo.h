// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/journal.h"
#include "exec/plan.h"
#include "fs/filesystem.h"
#include "model/item.h"
#include "preview/previewengine.h"
#include "validate/osrules.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

namespace ctrm {

// ---- Journal replay. Only ok records count; records are in journal order.

// Path of each request item after the records: "to" of an ok forward record, "from" of an ok
// rollback record, else folder + "/" + oldName.
QHash<ItemId, QString> replayPositions(const Plan &plan, const QList<StepRecord> &records);
// Forward steps that are done: an ok forward record and no later ok rollback record. In the
// order of their forward records (run order), so roll back can reverse them.
QList<int> replayDoneSteps(const Plan &plan, const QList<StepRecord> &records);

// ---- Undo (PRD 7.5)

struct UndoItem
{
    ItemId item = 0;
    QString currentFolder;
    QString currentName;
    QString originalName;
    bool isFolder = false;
    FileIdentity expectedIdentity; // from the journal
    FileIdentity currentIdentity;  // read now (empty type Weak, size -1 when missing)
    ItemPreview preview;           // status + diagnostics, same codes as PRD 7.1
};

struct UndoPreview
{
    QString originalBatchId;
    QDateTime originalStarted;
    QList<UndoItem> items;
    QList<QPair<QString, QString>> weakFolders; // folder, fsName
    int passCount = 0;                          // items with status Changed or CaseOnly
    bool weak() const;
    // Quick undo runs without showing the preview: only when every item passes and no item
    // has a weak identity. Never a partial quick undo.
    bool quickUndoAllowed() const;
};

// Items of the original that are renamed now (not rolled back) and not undone by one of the
// undoBatches (oldest first), each checked against the disk now. Reads the disk; no rename.
UndoPreview buildUndoPreview(const BatchRecord &original, const QList<BatchRecord> &undoBatches,
                             FileSystem &fs, OsRules os);
// Requests for the items that pass. Each request has the item ID of the original batch.
QList<RenameRequest> undoRequests(const UndoPreview &preview);

// Items of the original that are renamed now and not undone by one of the undoBatches.
// Used to set "undone" or "partly-undone" on the original.
QList<ItemId> remainingUndoItems(const BatchRecord &original,
                                 const QList<BatchRecord> &undoBatches);

} // namespace ctrm
