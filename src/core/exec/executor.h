// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/journal.h"
#include "exec/plan.h"
#include "fs/filesystem.h"

#include <QList>

#include <atomic>
#include <functional>

namespace ctrm {

struct RunOutcome
{
    bool finished = false; // all steps ok
    bool cancelled = false;
    int failedStep = -1;
    RenameResult failure;
    QList<int> okSteps; // forward steps with ok result, in run order
};

struct RollbackOutcome
{
    bool complete = false; // every rename ok and every record written
    QList<int> failedSteps;
    RenameResult lastFailure;   // the last rename failure, else the JournalWrite failure
    bool journalFailed = false; // a record could not be written (the renames still ran)
};

// Runs the steps of a plan. Each rename has a start record before it and a result record
// after it; no rename runs when its start record cannot be written. Each result goes to the
// journal before the next step starts.
// Not thread-safe; one instance per batch.
class Executor
{
public:
    using Progress = std::function<void(int done, int total)>;
    Executor(FileSystem &fs, JournalSink &journal);
    // Stops at the first failure, at a journal write failure, or when cancel is set (checked
    // before each step, so the current step always finishes).
    RunOutcome run(const Plan &plan, const std::atomic<bool> &cancel,
                   const Progress &progress = {});
    // Reverses okSteps in reverse order. Continues after a rename or journal failure. A step is
    // not reversed (it fails with SourceMissing or IdentityChanged) when the item at step.to is
    // missing or is not the item of its request.
    RollbackOutcome rollBack(const Plan &plan, const QList<int> &okSteps);
    // Only items whose last ok forward step is a ToTemp step: back to the old name, else to
    // the planned new name, else the item keeps the temporary name (not complete). An item
    // whose temporary name is missing or holds another item is not moved (not complete).
    RollbackOutcome restoreTempNames(const Plan &plan, const QList<int> &okSteps);

private:
    FileSystem &m_fs;
    JournalSink &m_journal;
};

} // namespace ctrm
