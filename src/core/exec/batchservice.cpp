// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/batchservice.h"

#include "fs/folderinfo.h"
#include "presets/preset.h"

#include <QDateTime>
#include <QSysInfo>

namespace ctrm {

namespace {

QDateTime now()
{
    return QDateTime::currentDateTimeUtc();
}

bool willRename(const ItemPreview &p)
{
    return p.included && !p.skipped &&
           (p.status == Status::Changed || p.status == Status::CaseOnly);
}

// Error text of the last failed rollback record of each step in failedSteps.
void addRollbackFailures(const QList<StepRecord> &records, const QList<int> &failedSteps,
                         QHash<int, QString> &out)
{
    for (const StepRecord &rec : records) {
        if (rec.phase == Phase::Rollback && !rec.ok && failedSteps.contains(rec.step))
            out.insert(rec.step, rec.errorText);
    }
}

QString journalError(const QString &path)
{
    return QStringLiteral("Cannot write the journal %1").arg(path);
}

// After an undo batch is complete: the original is undone when no renamed item of it is left,
// else partly undone. Returns an error text, or empty.
QString updateOriginal(HistoryStore &history, const QString &undoOf, const QString &undoBatchId)
{
    QString error;
    const std::optional<BatchRecord> original = history.load(undoOf, &error);
    if (!original)
        return error;
    const QList<ItemId> left = remainingUndoItems(*original, history.undoBatchesOf(undoOf));
    const BatchState state = left.isEmpty() ? BatchState::Undone : BatchState::PartlyUndone;
    if (!history.appendState(undoOf, {state, now(), undoBatchId}, &error))
        return error;
    return {};
}

} // namespace

// Writes each record to the journal and keeps a copy for the report.
class BatchSession::Recorder : public JournalSink
{
public:
    Recorder(JournalSink &sink, QList<StepRecord> initial)
        : m_sink(sink)
        , records(std::move(initial))
    {}
    bool appendStep(const PlanStep &step, const StepRecord &rec) override
    {
        records.append(rec);
        return m_sink.appendStep(step, rec);
    }
    bool appendState(const StateRecord &rec) override { return m_sink.appendState(rec); }

private:
    JournalSink &m_sink;

public:
    QList<StepRecord> records;
};

QList<ReportRow> buildReport(const Plan &plan, const QList<StepRecord> &records,
                             const QHash<int, QString> &failedSteps)
{
    const QHash<ItemId, QString> pos = replayPositions(plan, records);
    QSet<int> forwardOk;
    for (const StepRecord &rec : records) {
        if (rec.ok && rec.phase == Phase::Forward)
            forwardOk.insert(rec.step);
    }

    QList<ReportRow> rows;
    for (const RenameRequest &r : plan.requests) {
        ReportRow row;
        row.item = r.item;
        row.oldPath = r.folder + u'/' + r.oldName;
        row.finalPath = pos.value(r.item, row.oldPath);
        bool atTemp = false;
        bool ran = false;
        int failed = -1;
        for (const PlanStep &s : plan.steps) {
            if (s.item != r.item)
                continue;
            if (s.kind == StepKind::ToTemp && s.to == row.finalPath)
                atTemp = true;
            if (forwardOk.contains(s.index))
                ran = true;
            if (failed < 0 && failedSteps.contains(s.index))
                failed = s.index;
        }
        if (atTemp) {
            row.result = ReportRow::Result::LeftAtTemp;
        } else if (failed >= 0) {
            row.result = ReportRow::Result::Failed;
            row.errorText = failedSteps.value(failed);
        } else if (row.finalPath == r.folder + u'/' + r.newName) {
            row.result = ReportRow::Result::Renamed;
        } else if (ran) {
            row.result = ReportRow::Result::RolledBack;
        }
        rows.append(row);
    }
    return rows;
}

// ---- BatchSession

BatchSession::BatchSession(FileSystem &fs, HistoryStore &history,
                           std::unique_ptr<JournalWriter> writer, Plan plan, QString undoOf)
    : m_fs(fs)
    , m_history(history)
    , m_writer(std::move(writer))
    , m_recorder(std::make_unique<Recorder>(*m_writer, QList<StepRecord>()))
    , m_plan(std::move(plan))
    , m_undoOf(std::move(undoOf))
{}

BatchSession::~BatchSession() = default;

QString BatchSession::batchId() const
{
    return m_plan.batchId;
}

const Plan &BatchSession::plan() const
{
    return m_plan;
}

RunOutcome BatchSession::run(const std::atomic<bool> &cancel, const Executor::Progress &progress)
{
    Executor ex(m_fs, *m_recorder);
    m_run = ex.run(m_plan, cancel, progress);
    return m_run;
}

FinishResult BatchSession::complete()
{
    return finish(BatchState::Complete, {}, runJournalFailed());
}

bool BatchSession::runJournalFailed() const
{
    return m_run.failedStep >= 0 && m_run.failure.errorCode == ErrorCode::JournalWrite;
}

QHash<int, QString> BatchSession::runFailures() const
{
    QHash<int, QString> failed;
    if (m_run.failedStep >= 0 && m_run.failure.errorCode != ErrorCode::JournalWrite)
        failed.insert(m_run.failedStep, m_run.failure.errorText);
    return failed;
}

FinishResult BatchSession::rollBack()
{
    QHash<int, QString> failed = runFailures();
    Executor ex(m_fs, *m_recorder);
    // The done steps from the records, so a second call does not repeat reversed steps.
    const RollbackOutcome rb = ex.rollBack(m_plan, replayDoneSteps(m_plan, m_recorder->records));
    addRollbackFailures(m_recorder->records, rb.failedSteps, failed);
    return finish(rb.complete ? BatchState::RolledBack : BatchState::Incomplete, failed,
                  runJournalFailed() || rb.journalFailed);
}

FinishResult BatchSession::keep()
{
    Executor ex(m_fs, *m_recorder);
    const RollbackOutcome rb =
            ex.restoreTempNames(m_plan, replayDoneSteps(m_plan, m_recorder->records));
    return finish(rb.complete ? BatchState::Complete : BatchState::Incomplete, runFailures(),
                  runJournalFailed() || rb.journalFailed);
}

FinishResult BatchSession::finish(BatchState state, const QHash<int, QString> &failedSteps,
                                  bool journalFailed)
{
    FinishResult out;
    out.state = state;
    out.report = buildReport(m_plan, m_recorder->records, failedSteps);
    if (journalFailed)
        out.error = journalError(m_writer->filePath());
    if (!m_writer->appendState({state, now(), {}})) {
        out.error = journalError(m_writer->filePath());
        return out;
    }
    if (state == BatchState::Complete && !m_undoOf.isEmpty()) {
        const QString error = updateOriginal(m_history, m_undoOf, batchId());
        if (!error.isEmpty())
            out.error = error;
    }
    return out;
}

// ---- BatchService

BatchService::BatchService(FileSystem &fs, HistoryStore &history, AppInfo info)
    : m_fs(fs)
    , m_history(history)
    , m_info(std::move(info))
{}

PrepareResult BatchService::prepare(const PreviewInput &input, const PreviewResult &shown)
{
    PrepareResult out;
    PreviewInput fresh = input;
    fresh.folders.clear();
    fresh.identityChanged.clear();
    for (const Item &item : input.items) {
        if (!fresh.folders.contains(item.folder))
            fresh.folders.insert(item.folder, readFolderInfo(m_fs, item.folder));
        if (!item.included)
            continue;
        const auto id = m_fs.identity(item.path(), item.identity.type);
        if (id && !sameFile(item.identity, *id))
            fresh.identityChanged.insert(item.id);
    }
    out.identityChanged = fresh.identityChanged;
    out.freshPreview = computePreview(fresh);

    bool changed = out.freshPreview.items.size() != shown.items.size();
    for (int i = 0; !changed && i < shown.items.size(); ++i) {
        const ItemPreview &a = out.freshPreview.items[i];
        const ItemPreview &b = shown.items[i];
        changed = a.newName != b.newName || a.status != b.status;
    }
    if (changed) {
        out.kind = PrepareResult::Kind::PreviewChanged;
        return out;
    }

    QList<RenameRequest> requests;
    bool blocked = false;
    for (int i = 0; i < input.items.size(); ++i) {
        const ItemPreview &p = out.freshPreview.items[i];
        const Item &item = input.items[i];
        if (p.included && !p.skipped && isBlocking(p.status))
            blocked = true;
        if (!willRename(p))
            continue;
        RenameRequest r;
        r.item = item.id;
        r.folder = item.folder;
        r.oldName = item.name;
        r.newName = p.newName;
        r.isFolder = item.isFolder;
        r.identity = item.identity;
        requests.append(r);
    }
    if (requests.isEmpty()) {
        out.kind = PrepareResult::Kind::NothingToDo;
        return out;
    }
    if (blocked) {
        out.error = QStringLiteral("Some items are blocked");
        return out;
    }
    PlanResult plan = makePlan(requests, m_fs, newBatchId());
    if (!plan.plan) {
        out.error = plan.error;
        return out;
    }
    out.plan = std::move(plan.plan);
    out.kind = PrepareResult::Kind::Ready;
    return out;
}

std::unique_ptr<BatchSession> BatchService::start(const Plan &plan, const RuleChain &chain,
                                                  const QString &undoOf, QString *error)
{
    Preset preset;
    preset.chain = chain;
    JournalHeader h;
    h.batchId = plan.batchId;
    h.started = now();
    h.ctrmVersion = m_info.ctrmVersion;
    h.os = m_info.os.isEmpty() ? QSysInfo::prettyProductName() : m_info.os;
    h.chain = presetToJsonObject(preset);
    h.undoOf = undoOf;
    auto writer = JournalWriter::create(m_history.dir(), h, plan, error);
    if (!writer)
        return nullptr;
    return std::unique_ptr<BatchSession>(
            new BatchSession(m_fs, m_history, std::move(writer), plan, undoOf));
}

QList<BatchSummary> BatchService::pendingRecovery() const
{
    QList<BatchSummary> out;
    for (const BatchSummary &s : m_history.list()) {
        if (s.state == BatchState::InProgress || s.state == BatchState::Incomplete)
            out.append(s);
    }
    return out;
}

FinishResult BatchService::recoverRollBack(const QString &batchId)
{
    FinishResult out;
    // Never from a damaged journal: a lost op or state line would make CTRM reverse the wrong
    // steps, or a batch that is finished (the user can mark it as complete instead).
    const std::optional<BatchRecord> rec = m_history.load(batchId, &out.error);
    if (!rec) {
        out.error = QStringLiteral("The journal is damaged. CTRM cannot roll it back: %1")
                            .arg(out.error);
        return out;
    }
    out.state = rec->state();
    if (out.state != BatchState::InProgress && out.state != BatchState::Incomplete) {
        out.error = QStringLiteral("Batch %1 is not in progress or incomplete").arg(batchId);
        return out;
    }
    auto writer = JournalWriter::openForAppend(rec->filePath, &out.error);
    if (!writer)
        return out;

    // Failures of the run: failed forward records before the first roll back.
    QHash<int, QString> failed;
    for (const StepRecord &s : rec->steps) {
        if (s.phase == Phase::Rollback)
            break;
        if (!s.ok)
            failed.insert(s.step, s.errorText);
    }

    BatchSession::Recorder recorder(*writer, rec->steps);
    Executor ex(m_fs, recorder);
    const RollbackOutcome rb = ex.rollBack(rec->plan, replayDoneSteps(rec->plan, rec->steps));
    addRollbackFailures(recorder.records.mid(rec->steps.size()), rb.failedSteps, failed);
    out.state = rb.complete ? BatchState::RolledBack : BatchState::Incomplete;
    out.report = buildReport(rec->plan, recorder.records, failed);
    if (rb.journalFailed)
        out.error = journalError(rec->filePath);
    if (!writer->appendState({out.state, now(), {}}))
        out.error = journalError(rec->filePath);
    return out;
}

bool BatchService::markComplete(const QString &batchId, QString *error)
{
    // From the summary, so that a damaged journal (even one with no readable header) can be
    // marked as complete. Only a state line is written; nothing is renamed.
    std::optional<BatchSummary> summary;
    for (const BatchSummary &s : m_history.list()) {
        if (s.batchId == batchId)
            summary = s;
    }
    if (!summary) {
        if (error)
            *error = QStringLiteral("No journal for batch %1").arg(batchId);
        return false;
    }
    if (summary->state != BatchState::InProgress && summary->state != BatchState::Incomplete) {
        if (error)
            *error = QStringLiteral("Batch %1 is not in progress or incomplete").arg(batchId);
        return false;
    }
    if (!m_history.appendState(batchId, {BatchState::Complete, now(), {}}, error))
        return false;
    if (!summary->undoOf.isEmpty()) {
        const QString e = updateOriginal(m_history, summary->undoOf, batchId);
        if (!e.isEmpty()) {
            if (error)
                *error = e;
            return false;
        }
    }
    return true;
}

UndoPreview BatchService::previewUndo(const QString &batchId, QString *error)
{
    const std::optional<BatchRecord> rec = m_history.load(batchId, error);
    if (!rec)
        return {};
    if (!rec->header.undoOf.isEmpty()) {
        if (error)
            *error = QStringLiteral("An undo batch cannot be undone");
        return {};
    }
    const BatchState state = rec->state();
    if (state != BatchState::Complete && state != BatchState::PartlyUndone) {
        if (error)
            *error = QStringLiteral("Only a complete or partly undone batch can be undone");
        return {};
    }
    return buildUndoPreview(*rec, m_history.undoBatchesOf(batchId), m_fs, currentOsRules());
}

PlanResult BatchService::planUndo(const UndoPreview &preview)
{
    const QList<RenameRequest> requests = undoRequests(preview);
    if (requests.isEmpty())
        return {std::nullopt, QStringLiteral("No item can be undone")};
    return makePlan(requests, m_fs, newBatchId());
}

} // namespace ctrm
