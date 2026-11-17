// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/planner.h"

#include "fs/folderinfo.h"

#include <QHash>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <deque>
#include <vector>

namespace ctrm {

namespace {

QString joinPath(const QString &folder, const QString &name)
{
    return folder.endsWith(u'/') ? folder + name : folder + u'/' + name;
}

// One rename that waits for its target name to become free.
struct Pending
{
    int request = 0; // index into the input requests
    QString from;    // current name (old name or temporary name)
    QString to;      // new name
    QString fromKey;
    QString toKey;
    StepKind kind = StepKind::Direct; // Direct, or FromTemp after a cycle break
    bool done = false;
};

class FolderPlanner
{
public:
    FolderPlanner(const QList<RenameRequest> &requests, FileSystem &fs, const QString &batchId,
                  int &tempCounter, QList<PlanStep> &steps)
        : m_requests(requests)
        , m_fs(fs)
        , m_batchId(batchId)
        , m_tempCounter(tempCounter)
        , m_steps(steps)
    {}

    // Returns an error text, or an empty string when the steps for the folder were added.
    QString plan(const QString &folder, const QList<int> &indexes)
    {
        m_folder = folder;
        m_volume = m_fs.volumeInfo(folder);

        QString listError;
        const auto entries = m_fs.listFolder(folder, &listError);
        if (!entries)
            return QStringLiteral("Cannot read folder %1: %2").arg(folder, listError);

        QSet<QString> oldKeys;
        for (int i : indexes) {
            const RenameRequest &r = m_requests[i];
            if (r.oldName == r.newName)
                return QStringLiteral("Old and new name are the same: %1")
                        .arg(joinPath(folder, r.oldName));
            const QString key = nameKey(r.oldName, m_volume);
            if (oldKeys.contains(key))
                return QStringLiteral("Item is in the batch twice: %1")
                        .arg(joinPath(folder, r.oldName));
            oldKeys.insert(key);
        }
        for (int i : indexes) {
            const QString key = nameKey(m_requests[i].newName, m_volume);
            if (m_newKeys.contains(key))
                return QStringLiteral("Two items get the same name: %1")
                        .arg(joinPath(folder, m_requests[i].newName));
            m_newKeys.insert(key);
        }
        for (const DirEntry &e : *entries) {
            const QString key = nameKey(e.name, m_volume);
            if (m_newKeys.contains(key) && !oldKeys.contains(key))
                return QStringLiteral("Name is already used in the folder: %1")
                        .arg(joinPath(folder, e.name));
        }

        // Case-only or normalization-only renames on this volume: two steps at once.
        for (int i : indexes) {
            const RenameRequest &r = m_requests[i];
            if (nameKey(r.oldName, m_volume) != nameKey(r.newName, m_volume))
                continue;
            const QString tmp = nextTempName();
            if (tmp.isEmpty())
                return QStringLiteral("No free temporary name in %1").arg(folder);
            addStep(r.item, r.oldName, tmp, StepKind::ToTemp);
            addStep(r.item, tmp, r.newName, StepKind::FromTemp);
        }

        // Other renames: emit each one when no other pending rename still holds its target.
        std::vector<Pending> pending;
        for (int i : indexes) {
            const RenameRequest &r = m_requests[i];
            const QString oldKey = nameKey(r.oldName, m_volume);
            const QString newKey = nameKey(r.newName, m_volume);
            if (oldKey != newKey)
                pending.push_back(
                        {i, r.oldName, r.newName, oldKey, newKey, StepKind::Direct, false});
        }
        QHash<QString, int> byFromKey; // current name key -> pending index
        QHash<QString, int> byToKey;   // target key -> pending index
        for (int p = 0; p < int(pending.size()); ++p) {
            byFromKey.insert(pending[p].fromKey, p);
            byToKey.insert(pending[p].toKey, p);
        }

        std::deque<int> ready;
        for (int p = 0; p < int(pending.size()); ++p) {
            if (!byFromKey.contains(pending[p].toKey))
                ready.push_back(p);
        }

        // Frees the name key that pending[p] held and makes its waiter ready.
        const auto release = [&](const QString &key) {
            byFromKey.remove(key);
            const auto waiter = byToKey.constFind(key);
            if (waiter != byToKey.constEnd() && !pending[*waiter].done)
                ready.push_back(*waiter);
        };

        int remaining = int(pending.size());
        std::size_t firstOpen = 0;
        while (remaining > 0) {
            while (!ready.empty()) {
                const int p = ready.front();
                ready.pop_front();
                Pending &e = pending[p];
                if (e.done)
                    continue;
                addStep(m_requests[e.request].item, e.from, e.to, e.kind);
                e.done = true;
                --remaining;
                release(e.fromKey);
            }
            if (remaining == 0)
                break;

            // A cycle: move the first open rename (input order) to a temporary name.
            while (pending[firstOpen].done)
                ++firstOpen;
            Pending &e = pending[firstOpen];
            const QString tmp = nextTempName();
            if (tmp.isEmpty())
                return QStringLiteral("No free temporary name in %1").arg(folder);
            addStep(m_requests[e.request].item, e.from, tmp, StepKind::ToTemp);
            const QString heldKey = e.fromKey;
            e.from = tmp;
            e.fromKey = nameKey(tmp, m_volume);
            e.kind = StepKind::FromTemp;
            byFromKey.insert(e.fromKey, int(firstOpen));
            release(heldKey);
        }
        return {};
    }

private:
    // The next temporary name that is not on disk and is not a new name in the batch.
    QString nextTempName()
    {
        for (int tries = 0; tries < 100000; ++tries) {
            const QString name = tempName(m_batchId, ++m_tempCounter);
            if (m_newKeys.contains(nameKey(name, m_volume)))
                continue;
            if (m_fs.exists(joinPath(m_folder, name)))
                continue;
            return name;
        }
        return {};
    }

    void addStep(ItemId item, const QString &from, const QString &to, StepKind kind)
    {
        PlanStep s;
        s.index = int(m_steps.size());
        s.item = item;
        s.from = joinPath(m_folder, from);
        s.to = joinPath(m_folder, to);
        s.kind = kind;
        m_steps.append(s);
    }

    const QList<RenameRequest> &m_requests;
    FileSystem &m_fs;
    const QString &m_batchId;
    int &m_tempCounter;
    QList<PlanStep> &m_steps;
    QString m_folder;
    VolumeInfo m_volume;
    QSet<QString> m_newKeys;
};

} // namespace

QString newBatchId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString tempName(const QString &batchId, int n)
{
    QString hex = batchId;
    hex.remove(u'-');
    return QStringLiteral(".ctrm-tmp-") + hex.left(8) + u'-' + QString::number(n);
}

PlanResult makePlan(const QList<RenameRequest> &requests, FileSystem &fs, const QString &batchId)
{
    // Group by folder, input order kept inside a folder.
    QList<QString> folders;
    QHash<QString, QList<int>> byFolder;
    for (int i = 0; i < requests.size(); ++i) {
        const QString &folder = requests[i].folder;
        if (!byFolder.contains(folder))
            folders.append(folder);
        byFolder[folder].append(i);
    }
    // Deepest folder first, so a child is renamed while its parent path is still the old one.
    std::sort(folders.begin(), folders.end(), [](const QString &a, const QString &b) {
        const auto da = a.count(u'/');
        const auto db = b.count(u'/');
        if (da != db)
            return da > db;
        return a < b;
    });

    Plan plan;
    plan.batchId = batchId;
    plan.requests = requests;
    int tempCounter = 0;
    for (const QString &folder : std::as_const(folders)) {
        FolderPlanner planner(requests, fs, batchId, tempCounter, plan.steps);
        const QString error = planner.plan(folder, byFolder.value(folder));
        if (!error.isEmpty())
            return PlanResult{std::nullopt, error};
    }
    return PlanResult{plan, {}};
}

} // namespace ctrm
