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

// Sets the outcome after all tries: a rename failure has priority over a journal failure.
void finish(RollbackOutcome &out)
{
    if (out.journalFailed && out.failedSteps.isEmpty())
        out.lastFailure =
                checkFailure(ErrorCode::JournalWrite, QStringLiteral("Cannot write the journal"));
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
    QHash<ItemId, const RenameRequest *> requests;
    for (const RenameRequest &r : plan.requests)
        requests.insert(r.item, &r);

    RunOutcome out;
    const int total = int(plan.steps.size());
    for (int i = 0; i < total; ++i) {
        if (cancel.load()) {
            out.cancelled = true;
            return out;
        }
        const PlanStep &step = plan.steps[i];

        // Check the source before the rename.
        RenameResult result;
        if (!m_fs.exists(step.from)) {
            result = checkFailure(ErrorCode::SourceMissing,
                                  QStringLiteral("Source missing: %1").arg(step.from));
        } else if (step.kind != StepKind::FromTemp) {
            const RenameRequest *req = requests.value(step.item);
            const auto actual = req ? m_fs.identity(step.from, req->identity.type) : std::nullopt;
            if (!req || !actual || !sameFile(req->identity, *actual))
                result = checkFailure(ErrorCode::IdentityChanged,
                                      QStringLiteral("File identity changed: %1").arg(step.from));
            else
                result = m_fs.renameNoReplace(step.from, step.to);
        } else {
            result = m_fs.renameNoReplace(step.from, step.to);
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
            out.failure = checkFailure(ErrorCode::JournalWrite,
                                       QStringLiteral("Cannot write the journal"));
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
    RollbackOutcome out;
    for (auto it = okSteps.crbegin(); it != okSteps.crend(); ++it) {
        if (*it < 0 || *it >= plan.steps.size())
            continue;
        const PlanStep &step = plan.steps[*it];
        const RenameResult r = m_fs.renameNoReplace(step.to, step.from);
        if (!m_journal.appendStep(step, recordOf(step.index, Phase::Rollback, r)))
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

    RollbackOutcome out;
    for (const ItemId item : std::as_const(order)) {
        const PlanStep &toTemp = plan.steps[lastOk.value(item)];
        if (toTemp.kind != StepKind::ToTemp)
            continue;

        // 1. Back to the old name.
        const RenameResult back = m_fs.renameNoReplace(toTemp.to, toTemp.from);
        if (!m_journal.appendStep(toTemp, recordOf(toTemp.index, Phase::Rollback, back)))
            out.journalFailed = true;
        if (back.ok)
            continue;

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
            last = m_fs.renameNoReplace(fromTemp->from, fromTemp->to);
            if (!m_journal.appendStep(*fromTemp, recordOf(fromTemp->index, Phase::Forward, last)))
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
