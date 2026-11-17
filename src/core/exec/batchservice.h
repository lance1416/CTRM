// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/executor.h"
#include "exec/historystore.h"
#include "exec/journal.h"
#include "exec/plan.h"
#include "exec/planner.h"
#include "exec/undo.h"
#include "fs/filesystem.h"
#include "preview/previewengine.h"
#include "rules/rulechain.h"

#include <QList>
#include <QSet>
#include <QString>

#include <atomic>
#include <memory>
#include <optional>

namespace ctrm {

struct AppInfo
{
    QString ctrmVersion;
    QString os; // empty: QSysInfo::prettyProductName()
};

struct ReportRow
{
    ItemId item = 0;
    QString oldPath;
    QString finalPath;
    // Priority when more than one applies: LeftAtTemp, Failed, Renamed, RolledBack, NotRun.
    enum class Result { Renamed, Failed, RolledBack, NotRun, LeftAtTemp } result = Result::NotRun;
    QString errorText; // Failed: the text from the OS
};

struct FinishResult
{
    BatchState state = BatchState::Incomplete;
    QList<ReportRow> report;
    QString error; // set when the batch could not be read or the journal could not be written
};

struct PrepareResult
{
    enum class Kind { Ready, PreviewChanged, NothingToDo, Error } kind = Kind::Error;
    std::optional<Plan> plan;
    PreviewResult freshPreview;
    QSet<ItemId> identityChanged;
    QString error;
};

class BatchService;

// One batch from start to its end state. Not thread-safe.
class BatchSession
{
public:
    ~BatchSession();
    QString batchId() const;
    const Plan &plan() const;
    RunOutcome run(const std::atomic<bool> &cancel, const Executor::Progress &progress = {});
    // After run().finished: writes complete; updates the original of an undo batch.
    FinishResult complete();
    FinishResult rollBack(); // after a failed or cancelled run
    FinishResult keep();     // after a failed or cancelled run (PM D10)

private:
    friend class BatchService;
    class Recorder;
    BatchSession(FileSystem &fs, HistoryStore &history, std::unique_ptr<JournalWriter> writer,
                 Plan plan, QString undoOf);
    FinishResult finish(BatchState state, const QHash<int, QString> &failedSteps,
                        bool journalFailed);
    bool runJournalFailed() const;
    QHash<int, QString> runFailures() const;

    FileSystem &m_fs;
    HistoryStore &m_history;
    std::unique_ptr<JournalWriter> m_writer;
    std::unique_ptr<Recorder> m_recorder;
    Plan m_plan;
    QString m_undoOf;
    RunOutcome m_run;
};

// The one entry point for running, rolling back, recovering and undoing batches.
class BatchService
{
public:
    BatchService(FileSystem &fs, HistoryStore &history, AppInfo info);

    // Re-reads the folders and identities, then compares with the shown preview (PRD 7.6).
    PrepareResult prepare(const PreviewInput &input, const PreviewResult &shown);
    // Writes the journal header. nullptr and *error set on failure (nothing is renamed).
    std::unique_ptr<BatchSession> start(const Plan &plan, const RuleChain &chain,
                                        const QString &undoOf, QString *error);

    // Batches in state in-progress or incomplete, newest first. Includes damaged journals with
    // that state, and damaged journals with no readable header (BatchSummary::damaged).
    QList<BatchSummary> pendingRecovery() const;
    // Rolls back the done steps of an in-progress or incomplete batch (crash recovery,
    // "Retry Roll Back"). State rolled-back, else incomplete. Refuses a damaged journal.
    FinishResult recoverRollBack(const QString &batchId);
    // "Mark as complete" for an in-progress or incomplete batch, also a damaged one. For an
    // undo batch, also sets undone or partly-undone on the original.
    bool markComplete(const QString &batchId, QString *error);

    // Refuses an undo batch (AC13.8) and a batch whose state is not complete or partly-undone.
    UndoPreview previewUndo(const QString &batchId, QString *error);
    PlanResult planUndo(const UndoPreview &preview);

private:
    FileSystem &m_fs;
    HistoryStore &m_history;
    AppInfo m_info;
};

// Report rows from the plan and the step records. failedSteps: step index -> error text of
// the failures to report (a failed run step, failed roll back steps).
QList<ReportRow> buildReport(const Plan &plan, const QList<StepRecord> &records,
                             const QHash<int, QString> &failedSteps);

} // namespace ctrm
