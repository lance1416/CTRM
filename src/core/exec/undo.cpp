// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/undo.h"

#include "fs/folderinfo.h"
#include "validate/namevalidator.h"

#include <QMap>
#include <QSet>

#include <algorithm>

namespace ctrm {

namespace {

QString joinPath(const QString &folder, const QString &name)
{
    return folder + u'/' + name;
}

QString nameOf(const QString &path)
{
    return path.mid(path.lastIndexOf(u'/') + 1);
}

bool validStep(const Plan &plan, int step)
{
    return step >= 0 && step < plan.steps.size();
}

// Items of the original that are renamed now: request -> current path (before folder remap).
QList<QPair<const RenameRequest *, QString>> renamedItems(const BatchRecord &original)
{
    const QHash<ItemId, QString> pos = replayPositions(original.plan, original.steps);
    QList<QPair<const RenameRequest *, QString>> out;
    for (const RenameRequest &r : original.plan.requests) {
        const QString now = pos.value(r.item);
        if (now != joinPath(r.folder, r.oldName))
            out.append({&r, now});
    }
    return out;
}

// Original item IDs that an undo batch has put back: the item is at the target of its undo
// request now.
QSet<ItemId> undoneItems(const QList<BatchRecord> &undoBatches)
{
    QSet<ItemId> out;
    for (const BatchRecord &b : undoBatches) {
        const QHash<ItemId, QString> pos = replayPositions(b.plan, b.steps);
        for (const RenameRequest &r : b.plan.requests) {
            if (pos.value(r.item) == joinPath(r.folder, r.newName))
                out.insert(r.item);
        }
    }
    return out;
}

// Maps a path of the original batch (old folder names) to the path now, with the folder
// renames that are still in effect. Longest folder prefix first.
class FolderRemap
{
public:
    void add(const QString &oldPath, const QString &newPath) { m_map.insert(oldPath, newPath); }
    QString apply(const QString &path) const
    {
        QString best;
        for (auto it = m_map.cbegin(); it != m_map.cend(); ++it) {
            const QString &k = it.key();
            if ((path == k || path.startsWith(k + u'/')) && k.size() > best.size())
                best = k;
        }
        return best.isEmpty() ? path : m_map.value(best) + path.mid(best.size());
    }

private:
    QMap<QString, QString> m_map;
};

} // namespace

QHash<ItemId, QString> replayPositions(const Plan &plan, const QList<StepRecord> &records)
{
    QHash<ItemId, QString> pos;
    for (const RenameRequest &r : plan.requests)
        pos.insert(r.item, joinPath(r.folder, r.oldName));
    for (const StepRecord &rec : records) {
        if (!rec.ok || !validStep(plan, rec.step))
            continue;
        const PlanStep &s = plan.steps[rec.step];
        pos.insert(s.item, rec.phase == Phase::Forward ? s.to : s.from);
    }
    return pos;
}

QList<int> replayDoneSteps(const Plan &plan, const QList<StepRecord> &records)
{
    QList<int> done;
    for (const StepRecord &rec : records) {
        if (!rec.ok || !validStep(plan, rec.step))
            continue;
        if (rec.phase == Phase::Forward) {
            if (!done.contains(rec.step))
                done.append(rec.step);
        } else {
            done.removeAll(rec.step);
        }
    }
    return done;
}

bool UndoPreview::weak() const
{
    return !weakFolders.isEmpty();
}

bool UndoPreview::quickUndoAllowed() const
{
    return passCount > 0 && passCount == items.size() && !weak();
}

QList<ItemId> remainingUndoItems(const BatchRecord &original, const QList<BatchRecord> &undoBatches)
{
    const QSet<ItemId> undone = undoneItems(undoBatches);
    QList<ItemId> out;
    for (const auto &[req, now] : renamedItems(original)) {
        Q_UNUSED(now);
        if (!undone.contains(req->item))
            out.append(req->item);
    }
    return out;
}

UndoPreview buildUndoPreview(const BatchRecord &original, const QList<BatchRecord> &undoBatches,
                             FileSystem &fs, OsRules os)
{
    UndoPreview out;
    out.originalBatchId = original.header.batchId;
    out.originalStarted = original.header.started;

    const QSet<ItemId> undone = undoneItems(undoBatches);
    QList<QPair<const RenameRequest *, QString>> items;
    for (const auto &entry : renamedItems(original)) {
        if (!undone.contains(entry.first->item))
            items.append(entry);
    }

    // Folder renames still in effect, parents before children, so that each new path is
    // already remapped.
    QList<QPair<const RenameRequest *, QString>> folders;
    for (const auto &entry : std::as_const(items)) {
        if (entry.first->isFolder)
            folders.append(entry);
    }
    std::sort(folders.begin(), folders.end(), [](const auto &a, const auto &b) {
        return a.first->folder.size() < b.first->folder.size();
    });
    FolderRemap remap;
    for (const auto &[req, now] : std::as_const(folders))
        remap.add(joinPath(req->folder, req->oldName),
                  joinPath(remap.apply(req->folder), nameOf(now)));

    PreviewInput in;
    in.os = os;
    QList<ProposedName> names;
    for (const auto &[req, now] : std::as_const(items)) {
        UndoItem u;
        u.item = req->item;
        u.currentFolder = remap.apply(req->folder);
        u.currentName = nameOf(now);
        u.originalName = req->oldName;
        u.isFolder = req->isFolder;
        u.expectedIdentity = req->identity;
        const QString path = joinPath(u.currentFolder, u.currentName);
        if (const auto id = fs.identity(path, req->identity.type)) {
            u.currentIdentity = *id;
            if (!sameFile(req->identity, *id))
                in.identityChanged.insert(req->item);
        }
        if (!in.folders.contains(u.currentFolder))
            in.folders.insert(u.currentFolder, readFolderInfo(fs, u.currentFolder));

        Item item;
        item.id = req->item;
        item.folder = u.currentFolder;
        item.name = u.currentName;
        item.isFolder = req->isFolder;
        item.identity = req->identity;
        in.items.append(item);
        names.append({req->oldName, std::nullopt});
        out.items.append(u);
    }

    // Undo restores names as they were: no portable-name rule (D9).
    const PreviewResult result = evaluateNames(in, names, NameValidator(os, false));
    for (int i = 0; i < out.items.size(); ++i) {
        UndoItem &u = out.items[i];
        u.preview = result.items.value(i);
        if (u.expectedIdentity.type == IdentityType::Weak) {
            const QString fsName = in.folders.value(u.currentFolder).volume.fsName;
            u.preview.warnings.append({DiagCode::WeakIdentity, fsName});
            const QPair<QString, QString> folder{u.currentFolder, fsName};
            if (!out.weakFolders.contains(folder))
                out.weakFolders.append(folder);
        }
        if (u.preview.status == Status::Changed || u.preview.status == Status::CaseOnly)
            ++out.passCount;
    }
    return out;
}

QList<RenameRequest> undoRequests(const UndoPreview &preview)
{
    QList<RenameRequest> out;
    for (const UndoItem &u : preview.items) {
        if (u.preview.status != Status::Changed && u.preview.status != Status::CaseOnly)
            continue;
        RenameRequest r;
        r.item = u.item;
        r.folder = u.currentFolder;
        r.oldName = u.currentName;
        r.newName = u.originalName;
        r.isFolder = u.isFolder;
        r.identity = u.currentIdentity;
        out.append(r);
    }
    return out;
}

} // namespace ctrm
