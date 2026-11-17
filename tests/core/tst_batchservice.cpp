// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/batchservice.h"
#include "fakefilesystem.h"
#include "fs/folderinfo.h"
#include "fs/itemsource.h"
#include "rules/insertrule.h"

#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <cerrno>

using namespace ctrm;

namespace {

struct Env
{
    FakeFileSystem fs;
    QTemporaryDir dir;
    HistoryStore history{dir.path()};
    BatchService service{fs, history, AppInfo{QStringLiteral("0.1.0"), QStringLiteral("test")}};

    Env() { fs.addFolder(QStringLiteral("/f")); }
    void add(const QString &name, const QByteArray &content)
    {
        fs.addFile(QStringLiteral("/f/") + name, content);
    }
};

QList<Item> itemsFor(FakeFileSystem &fs, const QStringList &paths)
{
    QList<Item> items;
    ItemId id = 1;
    for (const QString &p : paths) {
        QString error;
        std::optional<Item> item = makeItem(fs, p, &error);
        if (!item)
            qFatal("makeItem %s: %s", qPrintable(p), qPrintable(error));
        item->id = id++;
        items.append(*item);
    }
    return items;
}

PreviewInput prefixInput(FakeFileSystem &fs, const QList<Item> &items, const QString &prefix)
{
    PreviewInput in;
    in.items = items;
    if (!prefix.isEmpty()) {
        auto rule = std::make_unique<InsertRule>();
        rule->text = prefix;
        in.chain.insert(0, std::move(rule));
    }
    for (const Item &item : items) {
        if (!in.folders.contains(item.folder))
            in.folders.insert(item.folder, readFolderInfo(fs, item.folder));
    }
    return in;
}

// One request per (oldPath, newName) pair; the identity comes from the file system now.
Plan planFor(FakeFileSystem &fs, const QList<QPair<QString, QString>> &renames)
{
    QList<RenameRequest> requests;
    ItemId id = 1;
    for (const auto &[path, newName] : renames) {
        RenameRequest r;
        r.item = id++;
        r.folder = path.left(path.lastIndexOf(u'/'));
        r.oldName = path.mid(path.lastIndexOf(u'/') + 1);
        r.newName = newName;
        r.identity = *fs.identity(path, IdentityType::Strong);
        requests.append(r);
    }
    PlanResult result = makePlan(requests, fs, newBatchId());
    if (!result.plan)
        qFatal("makePlan failed: %s", qPrintable(result.error));
    return *result.plan;
}

// a, b, c -> x, y, z
Plan threePlan(Env &env)
{
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    env.add("c.txt", "C");
    return planFor(env.fs, {{"/f/a.txt", "x.txt"}, {"/f/b.txt", "y.txt"}, {"/f/c.txt", "z.txt"}});
}

Plan swapPlan(Env &env)
{
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    return planFor(env.fs, {{"/f/a.txt", "b.txt"}, {"/f/b.txt", "a.txt"}});
}

std::unique_ptr<BatchSession> startOrFail(Env &env, const Plan &plan)
{
    QString error;
    auto session = env.service.start(plan, RuleChain(), {}, &error);
    if (!session)
        qFatal("start failed: %s", qPrintable(error));
    return session;
}

// Runs and sets cancel after `stopAfter` steps (-1: no cancel).
RunOutcome runSession(BatchSession &s, int stopAfter = -1)
{
    std::atomic<bool> cancel{false};
    return s.run(cancel, [&](int done, int) {
        if (done == stopAfter)
            cancel.store(true);
    });
}

BatchState stateOf(Env &env, const QString &batchId)
{
    QString error;
    const auto rec = env.history.load(batchId, &error);
    if (!rec)
        qFatal("load failed: %s", qPrintable(error));
    return rec->state();
}

bool hasTempName(const QStringList &names)
{
    for (const QString &n : names)
        if (n.startsWith(QStringLiteral(".ctrm-tmp-")))
            return true;
    return false;
}

const ReportRow *rowOf(const QList<ReportRow> &rows, ItemId item)
{
    for (const ReportRow &r : rows)
        if (r.item == item)
            return &r;
    return nullptr;
}

// A crash after the start record of a step (and, when rename is true, after the rename) and
// before its result record.
void crashInStep(Env &env, const Plan &plan, int step, Phase phase, bool rename)
{
    QString error;
    auto w = JournalWriter::openForAppend(env.history.list().first().filePath, &error);
    if (!w || !w->appendStart(plan.steps[step], phase))
        qFatal("cannot write the start record: %s", qPrintable(error));
    const PlanStep &s = plan.steps[step];
    if (rename) {
        const bool forward = phase == Phase::Forward;
        if (!env.fs.renameNoReplace(forward ? s.from : s.to, forward ? s.to : s.from).ok)
            qFatal("rename failed");
    }
}

// Runs the first `steps` steps of the plan, then stops with no end state (a crash).
void runAndCrash(Env &env, const Plan &plan, int steps)
{
    auto s = startOrFail(env, plan);
    std::atomic<bool> cancel{steps == 0};
    s->run(cancel, [&](int done, int) {
        if (done == steps)
            cancel.store(true);
    });
}

} // namespace

class TstBatchService : public QObject
{
    Q_OBJECT

private slots:
    void prepareReady();
    void prepareDetectsChange();
    void prepareDetectsIdentityChange();
    void prepareSkipsSkipped();
    void prepareNothingToDo();
    void prepareBlocked();
    void runCompleteJournal();
    void failureRollBack();
    void failureKeep();
    void keepRestoresTempNamedItems();
    void keepUsesNewNameWhenOldTaken();
    void keepLeavesTempWhenBothTaken();
    void summaryFailedCount();
    void rollBackFailIncomplete();
    void recoveryRollBack();
    void recoveryRefusesFinishedBatch();
    void recoveryRefusesDamagedJournal();
    void pendingShowsUnreadableJournal();
    void damagedBatchCannotBeUndone();
    void markCompleteOnDamagedJournal();
    void crashForwardDone();
    void crashForwardNotDone();
    void crashNotDoneOtherItemAtOldName();
    void crashToTempDone();
    void crashFromTempDone();
    void crashRollbackDone();
    void crashBothNamesTaken();
    void recoveryMarkComplete();
    void reportRows();
    void secondRollBackInSession();
};

void TstBatchService::prepareReady()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    env.add("c.txt", "C");
    const QList<Item> items = itemsFor(env.fs, {"/f/a.txt", "/f/b.txt", "/f/c.txt"});
    const PreviewInput in = prefixInput(env.fs, items, "x_");
    const PreviewResult shown = computePreview(in);
    const PrepareResult r = env.service.prepare(in, shown);
    QCOMPARE(r.kind, PrepareResult::Kind::Ready);
    QVERIFY(r.plan);
    QCOMPARE(r.plan->requests.size(), 3);
    QCOMPARE(r.plan->requests[0].newName, QStringLiteral("x_a.txt"));
    QVERIFY(!r.plan->batchId.isEmpty());
}

void TstBatchService::prepareDetectsChange()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    const QList<Item> items = itemsFor(env.fs, {"/f/a.txt", "/f/b.txt"});
    const PreviewInput in = prefixInput(env.fs, items, "x_");
    const PreviewResult shown = computePreview(in);
    QVERIFY(env.fs.renameNoReplace("/f/a.txt", "/f/q.txt").ok); // another program
    const PrepareResult r = env.service.prepare(in, shown);
    QCOMPARE(r.kind, PrepareResult::Kind::PreviewChanged);
    QVERIFY(!r.plan);
    QCOMPARE(r.freshPreview.items[0].status, Status::Missing);
    QCOMPARE(r.freshPreview.items[1].status, Status::Changed);
}

void TstBatchService::prepareDetectsIdentityChange()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    const QList<Item> items = itemsFor(env.fs, {"/f/a.txt", "/f/b.txt"});
    const PreviewInput in = prefixInput(env.fs, items, "x_");
    const PreviewResult shown = computePreview(in);
    QVERIFY(env.fs.remove("/f/b.txt"));
    env.add("b.txt", "NEW"); // same name, other file
    const PrepareResult r = env.service.prepare(in, shown);
    QCOMPARE(r.kind, PrepareResult::Kind::PreviewChanged);
    QCOMPARE(r.identityChanged, (QSet<ItemId>{2}));
    QCOMPARE(r.freshPreview.items[1].status, Status::Missing);
    QCOMPARE(r.freshPreview.items[1].problems.value(0).code, DiagCode::IdentityChanged);
}

void TstBatchService::prepareSkipsSkipped()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    env.add("c.txt", "C");
    const QList<Item> items = itemsFor(env.fs, {"/f/a.txt", "/f/b.txt", "/f/c.txt"});
    PreviewInput in = prefixInput(env.fs, items, "x_");
    in.skip = {2};
    const PreviewResult shown = computePreview(in);
    const PrepareResult r = env.service.prepare(in, shown);
    QCOMPARE(r.kind, PrepareResult::Kind::Ready);
    QCOMPARE(r.plan->requests.size(), 2);
    for (const RenameRequest &req : r.plan->requests)
        QVERIFY(req.item != 2);
}

void TstBatchService::prepareNothingToDo()
{
    Env env;
    env.add("a.txt", "A");
    const QList<Item> items = itemsFor(env.fs, {"/f/a.txt"});
    const PreviewInput in = prefixInput(env.fs, items, {});
    const PrepareResult r = env.service.prepare(in, computePreview(in));
    QCOMPARE(r.kind, PrepareResult::Kind::NothingToDo);
    QVERIFY(!r.plan);
}

void TstBatchService::prepareBlocked()
{
    Env env;
    env.add("a.txt", "A");
    env.add("x_a.txt", "OTHER");
    env.add("b.txt", "B");
    QList<Item> items = itemsFor(env.fs, {"/f/a.txt", "/f/x_a.txt", "/f/b.txt"});
    items[1].included = false; // x_a.txt stays: a.txt -> x_a.txt is a conflict
    const PreviewInput in = prefixInput(env.fs, items, "x_");
    const PreviewResult shown = computePreview(in);
    QCOMPARE(shown.items[0].status, Status::Conflict);
    QCOMPARE(shown.items[2].status, Status::Changed);
    const PrepareResult r = env.service.prepare(in, shown);
    QCOMPARE(r.kind, PrepareResult::Kind::Error);
    QVERIFY(!r.error.isEmpty());
    QVERIFY(!r.plan);
}

void TstBatchService::runCompleteJournal()
{
    Env env;
    const Plan plan = threePlan(env);
    auto s = startOrFail(env, plan);
    QVERIFY(runSession(*s).finished);
    const FinishResult f = s->complete();
    QCOMPARE(f.state, BatchState::Complete);
    QVERIFY(f.error.isEmpty());
    const QList<BatchSummary> list = env.history.list();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().batchId, plan.batchId);
    QCOMPARE(list.first().state, BatchState::Complete);
    QCOMPARE(list.first().failedCount, 0);
    QCOMPARE(env.fs.names("/f"), (QStringList{"x.txt", "y.txt", "z.txt"}));
    for (const ReportRow &row : f.report)
        QCOMPARE(row.result, ReportRow::Result::Renamed);
    QCOMPARE(f.report[0].finalPath, QStringLiteral("/f/x.txt"));
}

void TstBatchService::failureRollBack()
{
    Env env;
    const Plan plan = threePlan(env);
    env.fs.failRename(plan.steps[1].from, EACCES, "Permission denied");
    auto s = startOrFail(env, plan);
    QCOMPARE(runSession(*s).failedStep, 1);
    const FinishResult f = s->rollBack();
    QCOMPARE(f.state, BatchState::RolledBack);
    QCOMPARE(stateOf(env, plan.batchId), BatchState::RolledBack);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt", "c.txt"}));
    QCOMPARE(env.fs.content("/f/a.txt"), QByteArray("A"));
}

void TstBatchService::failureKeep()
{
    Env env;
    const Plan plan = threePlan(env);
    env.fs.failRename(plan.steps[1].from, EACCES, "Permission denied");
    auto s = startOrFail(env, plan);
    QCOMPARE(runSession(*s).failedStep, 1);
    const FinishResult f = s->keep();
    QCOMPARE(f.state, BatchState::Complete);
    QCOMPARE(stateOf(env, plan.batchId), BatchState::Complete);
    QCOMPARE(env.fs.names("/f"), (QStringList{"b.txt", "c.txt", "x.txt"}));
}

void TstBatchService::keepRestoresTempNamedItems()
{
    Env env;
    const Plan plan = swapPlan(env);
    QCOMPARE(plan.steps[0].kind, StepKind::ToTemp);
    env.fs.failRename(plan.steps[1].from, EIO, "I/O error");
    auto s = startOrFail(env, plan);
    QCOMPARE(runSession(*s).failedStep, 1);
    QVERIFY(hasTempName(env.fs.names("/f")));
    const FinishResult f = s->keep();
    QCOMPARE(f.state, BatchState::Complete);
    QVERIFY(!hasTempName(env.fs.names("/f")));
    QCOMPARE(env.fs.content("/f/a.txt"), QByteArray("A"));
    QCOMPARE(env.fs.content("/f/b.txt"), QByteArray("B"));
}

void TstBatchService::keepUsesNewNameWhenOldTaken()
{
    Env env;
    const Plan plan = swapPlan(env);
    auto s = startOrFail(env, plan);
    QVERIFY(runSession(*s, 1).cancelled);
    // Another program takes a.txt and removes b.txt.
    env.add("a.txt", "INTRUDER");
    QVERIFY(env.fs.remove("/f/b.txt"));
    const FinishResult f = s->keep();
    QCOMPARE(f.state, BatchState::Complete);
    QVERIFY(!hasTempName(env.fs.names("/f")));
    QCOMPARE(env.fs.content("/f/b.txt"), QByteArray("A"));
    QCOMPARE(rowOf(f.report, 1)->result, ReportRow::Result::Renamed);
    QCOMPARE(rowOf(f.report, 1)->finalPath, QStringLiteral("/f/b.txt"));
}

void TstBatchService::keepLeavesTempWhenBothTaken()
{
    Env env;
    const Plan plan = swapPlan(env);
    auto s = startOrFail(env, plan);
    QVERIFY(runSession(*s, 1).cancelled);
    env.add("a.txt", "INTRUDER"); // b.txt is still there
    const FinishResult f = s->keep();
    QCOMPARE(f.state, BatchState::Incomplete);
    QCOMPARE(stateOf(env, plan.batchId), BatchState::Incomplete);
    const ReportRow *row = rowOf(f.report, 1);
    QCOMPARE(row->result, ReportRow::Result::LeftAtTemp);
    QCOMPARE(row->finalPath, plan.steps[0].to);
    QCOMPARE(env.fs.content(plan.steps[0].to), QByteArray("A"));
    QCOMPARE(rowOf(f.report, 2)->result, ReportRow::Result::NotRun);
}

void TstBatchService::summaryFailedCount()
{
    Env env;
    const Plan plan = threePlan(env);
    env.fs.failRename(plan.steps[1].from, EACCES, "Permission denied");
    auto s = startOrFail(env, plan);
    runSession(*s);
    s->keep();
    const QList<BatchSummary> list = env.history.list();
    QCOMPARE(list.first().state, BatchState::Complete);
    QCOMPARE(list.first().failedCount, 1);
}

void TstBatchService::rollBackFailIncomplete()
{
    Env env;
    const Plan plan = threePlan(env);
    env.fs.failRename(plan.steps[2].from, EIO, "I/O error");          // run fails on step 2
    env.fs.failRename(plan.steps[1].to, EACCES, "Permission denied"); // roll back of step 1
    auto s = startOrFail(env, plan);
    QCOMPARE(runSession(*s).failedStep, 2);
    const FinishResult f = s->rollBack();
    QCOMPARE(f.state, BatchState::Incomplete);
    QCOMPARE(rowOf(f.report, 2)->result, ReportRow::Result::Failed);
    QCOMPARE(rowOf(f.report, 2)->errorText, QStringLiteral("Permission denied"));
    QCOMPARE(rowOf(f.report, 2)->finalPath, QStringLiteral("/f/y.txt"));
    QCOMPARE(rowOf(f.report, 1)->result, ReportRow::Result::RolledBack);
    s.reset();

    const QList<BatchSummary> pending = env.service.pendingRecovery();
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending.first().state, BatchState::Incomplete);
    // Retry Roll Back after the problem is gone.
    const FinishResult again = env.service.recoverRollBack(plan.batchId);
    QVERIFY2(again.error.isEmpty(), qPrintable(again.error));
    QCOMPARE(again.state, BatchState::RolledBack);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt", "c.txt"}));
    QVERIFY(env.service.pendingRecovery().isEmpty());
}

void TstBatchService::recoveryRollBack()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    env.add("c.txt", "C");
    env.add("d.txt", "D");
    env.add("e.txt", "E");
    const Plan plan = planFor(env.fs, {{"/f/a.txt", "v.txt"},
                                       {"/f/b.txt", "w.txt"},
                                       {"/f/c.txt", "x.txt"},
                                       {"/f/d.txt", "y.txt"},
                                       {"/f/e.txt", "z.txt"}});
    {
        auto s = startOrFail(env, plan);
        QCOMPARE(runSession(*s, 3).okSteps.size(), 3);
        // Crash: no end state is written.
    }
    const QList<BatchSummary> pending = env.service.pendingRecovery();
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending.first().batchId, plan.batchId);
    QCOMPARE(pending.first().state, BatchState::InProgress);

    // A new service, as after a restart.
    HistoryStore history(env.dir.path());
    BatchService service(env.fs, history, AppInfo{"0.1.0", "test"});
    const FinishResult f = service.recoverRollBack(plan.batchId);
    QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
    QCOMPARE(f.state, BatchState::RolledBack);
    QCOMPARE(stateOf(env, plan.batchId), BatchState::RolledBack);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt", "c.txt", "d.txt", "e.txt"}));
    QCOMPARE(env.fs.content("/f/c.txt"), QByteArray("C"));
    QCOMPARE(rowOf(f.report, 1)->result, ReportRow::Result::RolledBack);
    QCOMPARE(rowOf(f.report, 5)->result, ReportRow::Result::NotRun);
}

void TstBatchService::recoveryRefusesDamagedJournal()
{
    Env env;
    const Plan plan = threePlan(env);
    QString path;
    {
        auto s = startOrFail(env, plan);
        QCOMPARE(runSession(*s, 2).okSteps.size(), 2);
        path = env.history.list().first().filePath;
        // Crash: no end state. Then a line in the middle is damaged.
    }
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QList<QByteArray> lines = f.readAll().split('\n');
    f.close();
    lines.insert(2, "garbage");
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(lines.join('\n'));
    f.close();

    const QList<BatchSummary> pending = env.service.pendingRecovery();
    QCOMPARE(pending.size(), 1);
    QVERIFY(pending.first().damaged);
    QCOMPARE(pending.first().batchId, plan.batchId);
    QCOMPARE(pending.first().state, BatchState::InProgress);

    // A lost op or state line could make a roll back reverse the wrong steps: no rename.
    const int renames = env.fs.renameCount();
    const FinishResult r = env.service.recoverRollBack(plan.batchId);
    QVERIFY(!r.error.isEmpty());
    QCOMPARE(env.fs.renameCount(), renames);
    QCOMPARE(env.service.pendingRecovery().size(), 1);
}

void TstBatchService::pendingShowsUnreadableJournal()
{
    Env env;
    QFile f(env.dir.path() + "/20260101T000000000Z-lost.jsonl");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("garbage\n");
    f.close();
    const QList<BatchSummary> pending = env.service.pendingRecovery();
    QCOMPARE(pending.size(), 1);
    QVERIFY(pending.first().damaged);
    QCOMPARE(pending.first().batchId, QStringLiteral("lost"));
    // Nothing can be rolled back without a header: an error, no rename.
    const FinishResult r = env.service.recoverRollBack("lost");
    QVERIFY(!r.error.isEmpty());
    QCOMPARE(env.fs.renameCount(), 0);
}

void TstBatchService::damagedBatchCannotBeUndone()
{
    Env env;
    const Plan plan = threePlan(env);
    {
        auto s = startOrFail(env, plan);
        runSession(*s);
        s->complete();
    }
    const QString path = env.history.list().first().filePath;
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QList<QByteArray> lines = f.readAll().split('\n');
    f.close();
    lines.insert(2, "garbage");
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(lines.join('\n'));
    f.close();
    QVERIFY(env.history.list().first().damaged);
    QCOMPARE(env.history.list().first().state, BatchState::Complete);

    QString error;
    const UndoPreview undo = env.service.previewUndo(plan.batchId, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(undo.items.isEmpty());
    QCOMPARE(env.service.planUndo(undo).plan.has_value(), false);
}

void TstBatchService::markCompleteOnDamagedJournal()
{
    Env env;
    const Plan plan = threePlan(env);
    {
        auto s = startOrFail(env, plan);
        runSession(*s, 1); // crash: no end state
    }
    const QString path = env.history.list().first().filePath;
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QList<QByteArray> lines = f.readAll().split('\n');
    f.close();
    lines.insert(2, "garbage");
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(lines.join('\n'));
    f.close();
    // A journal with no readable header.
    QFile lost(env.dir.path() + "/20260101T000000000Z-lost.jsonl");
    QVERIFY(lost.open(QIODevice::WriteOnly));
    lost.write("garbage\n{\"type\":\"op\",\"st");
    lost.close();
    QCOMPARE(env.service.pendingRecovery().size(), 2);

    const int renames = env.fs.renameCount();
    QString error;
    QVERIFY2(env.service.markComplete(plan.batchId, &error), qPrintable(error));
    QVERIFY2(env.service.markComplete("lost", &error), qPrintable(error));
    QCOMPARE(env.fs.renameCount(), renames);
    QVERIFY(env.service.pendingRecovery().isEmpty());
    const QList<BatchSummary> list = env.history.list();
    QCOMPARE(list.size(), 2);
    for (const BatchSummary &s : list) {
        QVERIFY(s.damaged); // the batch stays damaged
        QCOMPARE(s.state, BatchState::Complete);
    }
    // A finished damaged batch cannot be marked again.
    QVERIFY(!env.service.markComplete("lost", &error));
}

void TstBatchService::crashForwardDone()
{
    Env env;
    const Plan plan = threePlan(env);
    runAndCrash(env, plan, 2);
    crashInStep(env, plan, 2, Phase::Forward, true); // c.txt -> z.txt is done
    const FinishResult f = env.service.recoverRollBack(plan.batchId);
    QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
    QCOMPARE(f.state, BatchState::RolledBack);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt", "c.txt"}));
    QCOMPARE(rowOf(f.report, 3)->result, ReportRow::Result::RolledBack);
}

void TstBatchService::crashForwardNotDone()
{
    Env env;
    const Plan plan = threePlan(env);
    runAndCrash(env, plan, 2);
    crashInStep(env, plan, 2, Phase::Forward, false); // c.txt -> z.txt did not run
    const FinishResult f = env.service.recoverRollBack(plan.batchId);
    QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
    QCOMPARE(f.state, BatchState::RolledBack);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt", "c.txt"}));
    QCOMPARE(rowOf(f.report, 3)->result, ReportRow::Result::NotRun);
    // History does not count the step as failed.
    QCOMPARE(env.history.list().first().failedCount, 0);
}

void TstBatchService::crashNotDoneOtherItemAtOldName()
{
    Env env;
    const Plan plan = threePlan(env);
    runAndCrash(env, plan, 2);
    crashInStep(env, plan, 2, Phase::Forward, false);
    // Another file is at the old name now: CTRM cannot tell what happened.
    QVERIFY(env.fs.remove("/f/c.txt"));
    env.add("c.txt", "other");
    const FinishResult f = env.service.recoverRollBack(plan.batchId);
    QCOMPARE(f.state, BatchState::Incomplete);
    QCOMPARE(rowOf(f.report, 3)->result, ReportRow::Result::Failed);
    QVERIFY(rowOf(f.report, 3)->errorText.contains("/f/c.txt"));
    QCOMPARE(env.fs.content("/f/c.txt"), QByteArray("other")); // not touched
    // The other steps are rolled back.
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt", "c.txt"}));
    QCOMPARE(env.service.pendingRecovery().size(), 1);
}

void TstBatchService::crashToTempDone()
{
    Env env;
    const Plan plan = swapPlan(env);
    QCOMPARE(plan.steps[0].kind, StepKind::ToTemp);
    runAndCrash(env, plan, 0);
    crashInStep(env, plan, 0, Phase::Forward, true); // a.txt -> temporary name is done
    QVERIFY(hasTempName(env.fs.names("/f")));
    const FinishResult f = env.service.recoverRollBack(plan.batchId);
    QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
    QCOMPARE(f.state, BatchState::RolledBack);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt"}));
    QCOMPARE(env.fs.content("/f/a.txt"), QByteArray("A"));
}

void TstBatchService::crashFromTempDone()
{
    Env env;
    const Plan plan = swapPlan(env);
    QCOMPARE(plan.steps[2].kind, StepKind::FromTemp);
    runAndCrash(env, plan, 2);
    crashInStep(env, plan, 2, Phase::Forward, true); // temporary name -> b.txt is done
    const FinishResult f = env.service.recoverRollBack(plan.batchId);
    QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
    QCOMPARE(f.state, BatchState::RolledBack);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt"}));
    QCOMPARE(env.fs.content("/f/a.txt"), QByteArray("A"));
    QCOMPARE(env.fs.content("/f/b.txt"), QByteArray("B"));
}

void TstBatchService::crashRollbackDone()
{
    Env env;
    const Plan plan = threePlan(env);
    runAndCrash(env, plan, 2);
    // Crash during the roll back of step 1: y.txt -> b.txt is done.
    crashInStep(env, plan, 1, Phase::Rollback, true);
    const int renames = env.fs.renameCount();
    const FinishResult f = env.service.recoverRollBack(plan.batchId);
    QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
    QCOMPARE(f.state, BatchState::RolledBack);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt", "c.txt"}));
    QCOMPARE(env.fs.renameCount(), renames + 1); // only step 0 is rolled back now
}

void TstBatchService::crashBothNamesTaken()
{
    Env env;
    const Plan plan = swapPlan(env);
    runAndCrash(env, plan, 0);
    crashInStep(env, plan, 0, Phase::Forward, false);
    // An item at the temporary name and the item at the old name: CTRM cannot tell.
    env.fs.addFile(plan.steps[0].to, "T");
    const FinishResult f = env.service.recoverRollBack(plan.batchId);
    QCOMPARE(f.state, BatchState::Incomplete);
    const ReportRow *row = rowOf(f.report, 1);
    QCOMPARE(row->result, ReportRow::Result::Failed);
    QVERIFY(row->errorText.contains(plan.steps[0].to)); // the temporary name is shown
    QCOMPARE(env.fs.content("/f/a.txt"), QByteArray("A"));
    QCOMPARE(env.fs.content(plan.steps[0].to), QByteArray("T"));
}

void TstBatchService::recoveryRefusesFinishedBatch()
{
    Env env;
    const Plan plan = threePlan(env);
    auto s = startOrFail(env, plan);
    runSession(*s);
    s->complete();
    const int renames = env.fs.renameCount();
    const FinishResult f = env.service.recoverRollBack(plan.batchId);
    QVERIFY(!f.error.isEmpty());
    QCOMPARE(f.state, BatchState::Complete);
    QCOMPARE(env.fs.renameCount(), renames);
}

void TstBatchService::recoveryMarkComplete()
{
    Env env;
    const Plan plan = threePlan(env);
    {
        auto s = startOrFail(env, plan);
        runSession(*s, 2);
    }
    QString error;
    QVERIFY2(env.service.markComplete(plan.batchId, &error), qPrintable(error));
    QCOMPARE(stateOf(env, plan.batchId), BatchState::Complete);
    QVERIFY(env.service.pendingRecovery().isEmpty());
    const UndoPreview undo = env.service.previewUndo(plan.batchId, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(undo.items.size(), 2);
    QCOMPARE(undo.items[0].originalName, QStringLiteral("a.txt"));
    QCOMPARE(undo.items[1].originalName, QStringLiteral("b.txt"));
}

void TstBatchService::reportRows()
{
    Env env;
    const Plan plan = threePlan(env);
    env.fs.failRename(plan.steps[1].from, EACCES, "Permission denied");
    auto s = startOrFail(env, plan);
    runSession(*s);
    const FinishResult f = s->rollBack();
    QCOMPARE(f.report.size(), 3);
    QCOMPARE(f.report[0].result, ReportRow::Result::RolledBack);
    QCOMPARE(f.report[0].oldPath, QStringLiteral("/f/a.txt"));
    QCOMPARE(f.report[0].finalPath, QStringLiteral("/f/a.txt"));
    QCOMPARE(f.report[1].result, ReportRow::Result::Failed);
    QCOMPARE(f.report[1].errorText, QStringLiteral("Permission denied"));
    QCOMPARE(f.report[2].result, ReportRow::Result::NotRun);
}

void TstBatchService::secondRollBackInSession()
{
    // A retry on the same session reverses only the steps that are still done.
    Env env;
    const Plan plan = threePlan(env);
    env.fs.failRename(plan.steps[2].from, EIO, "I/O error");
    env.fs.failRename(plan.steps[1].to, EACCES, "Permission denied");
    auto s = startOrFail(env, plan);
    runSession(*s);
    QCOMPARE(s->rollBack().state, BatchState::Incomplete);
    const int renames = env.fs.renameCount();
    const FinishResult f = s->rollBack();
    QCOMPARE(f.state, BatchState::RolledBack);
    QVERIFY(f.error.isEmpty());
    QCOMPARE(env.fs.renameCount(), renames + 1);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt", "c.txt"}));
}

QTEST_GUILESS_MAIN(TstBatchService)
#include "tst_batchservice.moc"
