// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/executor.h"

#include <QDateTime>
#include <QHash>

namespace ctrm {

namespace {

RenameResult checkFailure(int code, const QString &text)
{
    RenameResult r;
    r.errorCode = code;
    r.errorText = text;
    return r;
}

StepRecord recordOf(int step, Phase phase, const RenameResult &r)
{
    StepRecord rec;
    rec.step = step;
    rec.phase = phase;
    rec.ok = r.ok;
    rec.errorCode = r.ok ? 0 : r.errorCode;
    rec.errorText = r.ok ? QString() : r.errorText;
    rec.fallback = r.fallback;
    rec.time = QDateTime::currentDateTimeUtc();
    return rec;
}

using RequestMap = QHash<ItemId, const RenameRequest *>;

RequestMap requestMap(const Plan &plan)
{
    RequestMap map;
    for (const RenameRequest &r : plan.requests)
        map.insert(r.item, &r);
    return map;
}

// ok when the item at path exists and has the identity of its request. Else the failure to
// record (SourceMissing or IdentityChanged); nothing may be renamed then.
RenameResult checkItemAt(FileSystem &fs, const RequestMap &requests, ItemId item,
                         const QString &path)
{
    if (!fs.exists(path))
        return checkFailure(ErrorCode::SourceMissing,
                            QStringLiteral("Source missing: %1").arg(path));
    const RenameRequest *req = requests.value(item);
    const auto actual = req ? fs.identity(path, req->identity.type) : std::nullopt;
    if (!req || !actual || !sameFile(req->identity, *actual))
        return checkFailure(ErrorCode::IdentityChanged,
                            QStringLiteral("File identity changed: %1").arg(path));
    RenameResult r;
    r.ok = true;
    return r;
}

RenameResult journalFailure()
{
    return checkFailure(ErrorCode::JournalWrite, QStringLiteral("Cannot write the journal"));
}

// Writes the start record of the step, then renames. No rename when the start record cannot
// be written (JournalWrite failure).
RenameResult startAndRename(FileSystem &fs, JournalSink &journal, const PlanStep &step, Phase phase)
{
    if (!journal.appendStart(step, phase))
        return journalFailure();
    return phase == Phase::Forward ? fs.renameNoReplace(step.from, step.to)
                                   : fs.renameNoReplace(step.to, step.from);
}

// Sets the outcome after all tries: a rename failure has priority over a journal failure.
void finish(RollbackOutcome &out)
{
    if (out.journalFailed && out.failedSteps.isEmpty())
        out.lastFailure = journalFailure();
    out.complete = out.failedSteps.isEmpty() && !out.journalFailed;
}

} // namespace

Executor::Executor(FileSystem &fs, JournalSink &journal)
    : m_fs(fs)
    , m_journal(journal)
{}

RunOutcome Executor::run(const Plan &plan, const std::atomic<bool> &cancel,
                         const Progress &progress)
{
    const RequestMap requests = requestMap(plan);

    RunOutcome out;
    const int total = int(plan.steps.size());
    for (int i = 0; i < total; ++i) {
        if (cancel.load()) {
            out.cancelled = true;
            return out;
        }
        const PlanStep &step = plan.steps[i];

        // Check the source before the rename. A FromTemp source has a name only CTRM uses, so
        // only its presence is checked.
        RenameResult result;
        if (step.kind == StepKind::FromTemp && !m_fs.exists(step.from))
            result = checkFailure(ErrorCode::SourceMissing,
                                  QStringLiteral("Source missing: %1").arg(step.from));
        else if (step.kind != StepKind::FromTemp)
            result = checkItemAt(m_fs, requests, step.item, step.from);
        else
            result.ok = true;
        if (result.ok) {
            result = startAndRename(m_fs, m_journal, step, Phase::Forward);
            if (result.errorCode == ErrorCode::JournalWrite) {
                // No rename ran, and no more renames run without a journal.
                out.failedStep = i;
                out.failure = result;
                return out;
            }
        }

        if (result.ok)
            out.okSteps.append(i);
        const bool journalled = m_journal.appendStep(step, recordOf(i, Phase::Forward, result));
        if (!result.ok) {
            out.failedStep = i;
            out.failure = result;
            return out;
        }
        if (!journalled) {
            // The rename is done (it counts in okSteps, so roll back can undo it), but no more
            // renames run without a journal record.
            out.failedStep = i;
            out.failure = journalFailure();
            return out;
        }
        if (progress)
            progress(i + 1, total);
    }
    out.finished = true;
    return out;
}

RollbackOutcome Executor::rollBack(const Plan &plan, const QList<int> &okSteps)
{
    const RequestMap requests = requestMap(plan);
    RollbackOutcome out;
    for (auto it = okSteps.crbegin(); it != okSteps.crend(); ++it) {
        if (*it < 0 || *it >= plan.steps.size())
            continue;
        const PlanStep &step = plan.steps[*it];
        // Never move an item that is not the one this step renamed.
        RenameResult r = checkItemAt(m_fs, requests, step.item, step.to);
        if (r.ok)
            r = startAndRename(m_fs, m_journal, step, Phase::Rollback);
        if (r.errorCode == ErrorCode::JournalWrite ||
            !m_journal.appendStep(step, recordOf(step.index, Phase::Rollback, r)))
            out.journalFailed = true;
        if (!r.ok) {
            out.failedSteps.append(*it);
            out.lastFailure = r;
        }
    }
    finish(out);
    return out;
}

RollbackOutcome Executor::restoreTempNames(const Plan &plan, const QList<int> &okSteps)
{
    // The last ok forward step of each item, in run order.
    QHash<ItemId, int> lastOk;
    QList<ItemId> order;
    for (int s : okSteps) {
        if (s < 0 || s >= plan.steps.size())
            continue;
        const ItemId item = plan.steps[s].item;
        if (!lastOk.contains(item))
            order.append(item);
        lastOk.insert(item, s);
    }

    const RequestMap requests = requestMap(plan);
    RollbackOutcome out;
    for (const ItemId item : std::as_const(order)) {
        const PlanStep &toTemp = plan.steps[lastOk.value(item)];
        if (toTemp.kind != StepKind::ToTemp)
            continue;

        // 0. Never move an item that is not the one at its temporary name.
        const RenameResult check = checkItemAt(m_fs, requests, item, toTemp.to);
        if (!check.ok) {
            if (!m_journal.appendStep(toTemp, recordOf(toTemp.index, Phase::Rollback, check)))
                out.journalFailed = true;
            out.failedSteps.append(toTemp.index);
            out.lastFailure = check;
            continue;
        }

        // 1. Back to the old name.
        const RenameResult back = startAndRename(m_fs, m_journal, toTemp, Phase::Rollback);
        if (back.errorCode == ErrorCode::JournalWrite ||
            !m_journal.appendStep(toTemp, recordOf(toTemp.index, Phase::Rollback, back)))
            out.journalFailed = true;
        if (back.ok)
            continue;
        if (back.errorCode == ErrorCode::JournalWrite) {
            // No journal: no more renames for this item. It keeps the temporary name.
            out.failedSteps.append(toTemp.index);
            out.lastFailure = back;
            continue;
        }

        // 2. On to the planned new name: that is the item's FromTemp step.
        const PlanStep *fromTemp = nullptr;
        for (const PlanStep &s : plan.steps) {
            if (s.item == item && s.kind == StepKind::FromTemp && s.from == toTemp.to) {
                fromTemp = &s;
                break;
            }
        }
        RenameResult last = back;
        if (fromTemp) {
            last = startAndRename(m_fs, m_journal, *fromTemp, Phase::Forward);
            if (last.errorCode == ErrorCode::JournalWrite ||
                !m_journal.appendStep(*fromTemp, recordOf(fromTemp->index, Phase::Forward, last)))
                out.journalFailed = true;
            if (last.ok)
                continue;
        }

        // 3. The item keeps the temporary name.
        out.failedSteps.append(toTemp.index);
        out.lastFailure = last;
    }
    finish(out);
    return out;
}

} // namespace ctrm
