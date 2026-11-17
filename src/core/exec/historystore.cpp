// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/historystore.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace ctrm {

namespace {

BatchSummary summarize(const BatchRecord &r)
{
    BatchSummary s;
    s.batchId = r.header.batchId;
    s.filePath = r.filePath;
    s.started = r.header.started;
    s.ended = r.ended();
    s.itemCount = int(r.plan.requests.size());
    for (const RenameRequest &req : r.plan.requests) {
        if (!s.folders.contains(req.folder))
            s.folders.append(req.folder);
    }
    s.state = r.state();
    s.undoOf = r.header.undoOf;
    for (const StepRecord &step : r.steps) {
        if (step.phase == Phase::Forward && !step.ok)
            ++s.failedCount;
    }
    return s;
}

// The batch ID of the original batch of the group.
QString groupOf(const BatchSummary &s)
{
    return s.undoOf.isEmpty() ? s.batchId : s.undoOf;
}

bool unsafeToDelete(const BatchSummary &s)
{
    return s.state == BatchState::InProgress || s.state == BatchState::Incomplete;
}

} // namespace

HistoryStore::HistoryStore(QString dir)
    : m_dir(std::move(dir))
{}

QString HistoryStore::dir() const
{
    return m_dir;
}

QList<BatchSummary> HistoryStore::readAll() const
{
    QList<BatchSummary> result;
    const QDir d(m_dir);
    const QStringList files = d.entryList({QStringLiteral("*.jsonl")}, QDir::Files, QDir::Name);
    for (const QString &name : files) {
        QString error;
        const auto record = readJournal(d.filePath(name), &error);
        if (record)
            result.append(summarize(*record));
    }
    std::sort(result.begin(), result.end(), [](const BatchSummary &a, const BatchSummary &b) {
        if (a.started != b.started)
            return a.started > b.started;
        return a.batchId > b.batchId;
    });
    return result;
}

QList<BatchSummary> HistoryStore::list() const
{
    return readAll();
}

QString HistoryStore::fileOf(const QString &batchId) const
{
    if (batchId.isEmpty())
        return {};
    const QDir d(m_dir);
    const QString suffix = u'-' + batchId + QStringLiteral(".jsonl");
    const QStringList files = d.entryList({QStringLiteral("*.jsonl")}, QDir::Files, QDir::Name);
    for (const QString &name : files) {
        if (name.endsWith(suffix))
            return d.filePath(name);
    }
    return {};
}

std::optional<BatchRecord> HistoryStore::load(const QString &batchId, QString *error) const
{
    const QString path = fileOf(batchId);
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("No journal for batch %1").arg(batchId);
        return std::nullopt;
    }
    return readJournal(path, error);
}

QList<BatchSummary> HistoryStore::inProgress() const
{
    QList<BatchSummary> result;
    for (const BatchSummary &s : readAll()) {
        if (s.state == BatchState::InProgress)
            result.append(s);
    }
    return result;
}

QList<BatchRecord> HistoryStore::undoBatchesOf(const QString &batchId) const
{
    QList<BatchRecord> result;
    const QDir d(m_dir);
    const QStringList files = d.entryList({QStringLiteral("*.jsonl")}, QDir::Files, QDir::Name);
    for (const QString &name : files) {
        QString error;
        auto record = readJournal(d.filePath(name), &error);
        if (record && !batchId.isEmpty() && record->header.undoOf == batchId)
            result.append(std::move(*record));
    }
    std::sort(result.begin(), result.end(), [](const BatchRecord &a, const BatchRecord &b) {
        return a.header.started < b.header.started;
    });
    return result;
}

bool HistoryStore::appendState(const QString &batchId, const StateRecord &rec, QString *error)
{
    const QString path = fileOf(batchId);
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("No journal for batch %1").arg(batchId);
        return false;
    }
    auto writer = JournalWriter::openForAppend(path, error);
    if (!writer)
        return false;
    if (!writer->appendState(rec)) {
        if (error)
            *error = QStringLiteral("Cannot write the journal %1").arg(path);
        return false;
    }
    return true;
}

int HistoryStore::deleteGroups(const QList<BatchSummary> &all,
                               const std::function<bool(const BatchSummary &)> &keep)
{
    QSet<QString> keptGroups;
    for (const BatchSummary &s : all) {
        if (keep(s))
            keptGroups.insert(groupOf(s));
    }
    int deleted = 0;
    for (const BatchSummary &s : all) {
        if (!keptGroups.contains(groupOf(s)) && QFile::remove(s.filePath))
            ++deleted;
    }
    return deleted;
}

int HistoryStore::applyRetention(int keepCount, int keepDays, const QDateTime &now)
{
    const QList<BatchSummary> all = readAll(); // newest first
    QSet<QString> newest;
    for (int i = 0; i < all.size() && i < keepCount; ++i)
        newest.insert(all[i].batchId);
    const QDateTime cutoff = now.addDays(-keepDays);
    return deleteGroups(all, [&](const BatchSummary &s) {
        return unsafeToDelete(s) || newest.contains(s.batchId) || s.started >= cutoff;
    });
}

int HistoryStore::clear()
{
    return deleteGroups(readAll(), unsafeToDelete);
}

} // namespace ctrm
