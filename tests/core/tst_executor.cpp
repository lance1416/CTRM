// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/executor.h"
#include "exec/planner.h"
#include "fakefilesystem.h"
#include "temptree.h"

#include <QThread>
#include <QtTest>

#include <atomic>
#include <cerrno>

using namespace ctrm;

namespace {

const QString kBatch = QStringLiteral("1234abcd-5678-90ab-cdef-1234567890ab");

// Records the journal lines in memory. failOn = n makes the n-th appendStep call (1-based)
// fail; the record is not kept.
class MemorySink : public JournalSink
{
public:
    struct Line
    {
        PlanStep step;
        StepRecord rec;
    };
    QList<Line> lines;
    QList<StateRecord> states;
    struct Start
    {
        int step;
        Phase phase;
        int linesBefore; // op records before this start record
    };
    QList<Start> starts;
    int failOn = 0;
    int calls = 0;
    int failStartOn = 0;
    int startCalls = 0;

    bool appendStart(const PlanStep &step, Phase phase) override
    {
        ++startCalls;
        if (startCalls == failStartOn)
            return false;
        starts.append({step.index, phase, int(lines.size())});
        return true;
    }

    bool appendStep(const PlanStep &step, const StepRecord &rec) override
    {
        ++calls;
        if (calls == failOn)
            return false;
        lines.append({step, rec});
        return true;
    }
    bool appendState(const StateRecord &rec) override
    {
        states.append(rec);
        return true;
    }
};

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
    PlanResult result = makePlan(requests, fs, kBatch);
    if (!result.plan)
        qFatal("makePlan failed: %s", qPrintable(result.error));
    return *result.plan;
}

bool hasTempName(const QStringList &names)
{
    for (const QString &n : names)
        if (n.startsWith(QStringLiteral(".ctrm-tmp-")))
            return true;
    return false;
}

int indexOfKind(const Plan &plan, StepKind kind)
{
    for (const PlanStep &s : plan.steps)
        if (s.kind == kind)
            return s.index;
    return -1;
}

// A swap a.txt <-> b.txt in /f.
void addSwapFiles(FakeFileSystem &fs)
{
    fs.addFolder(QStringLiteral("/f"));
    fs.addFile(QStringLiteral("/f/a.txt"), "A");
    fs.addFile(QStringLiteral("/f/b.txt"), "B");
}

Plan swapPlan(FakeFileSystem &fs)
{
    return planFor(fs, {{QStringLiteral("/f/a.txt"), QStringLiteral("b.txt")},
                        {QStringLiteral("/f/b.txt"), QStringLiteral("a.txt")}});
}

// Three independent renames a->x, b->y, c->z in /f.
Plan threePlan(FakeFileSystem &fs)
{
    fs.addFolder(QStringLiteral("/f"));
    fs.addFile(QStringLiteral("/f/a.txt"), "A");
    fs.addFile(QStringLiteral("/f/b.txt"), "B");
    fs.addFile(QStringLiteral("/f/c.txt"), "C");
    return planFor(fs, {{QStringLiteral("/f/a.txt"), QStringLiteral("x.txt")},
                        {QStringLiteral("/f/b.txt"), QStringLiteral("y.txt")},
                        {QStringLiteral("/f/c.txt"), QStringLiteral("z.txt")}});
}

} // namespace

class TstExecutor : public QObject
{
    Q_OBJECT

private slots:
    void runsAllSteps();
    void journalBeforeNextStep();
    void failureStops();
    void rollBackRestores();
    void raceTargetAppears();
    void identityChangedStops();
    void sourceMissingStops();
    void cancelStopsAfterCurrent();
    void cancelMidCycleRollBackRestoresAll();
    void restoreTempNamesOnly();
    void restoreTempNamesUsesNewNameWhenOldTaken();
    void restoreTempNamesKeepsTempWhenBothTaken();
    void restoreTempNamesJournalShowsNewName();
    void stopsWhenJournalAppendFails();
    void rollBackContinuesOnFailure();
    void noTempLeftAfterSuccess();
    void cancelBeforeStartRunsNothing();
    void rollBackReportsJournalFailure();
    void restoreTempNamesReportsJournalFailure();
    void restoreTempNamesSkipsFinishedItems();
    void rollBackSkipsReplacedItem();
    void rollBackSkipsMissingItem();
    void restoreTempNamesSkipsReplacedItem();
    void startRecordBeforeEachRename();
    void startWriteFailureStopsRun();
    void rollBackStartFailureDoesNotRename();
    void restoreTempNamesWritesStarts();
    void restoreTempNamesStopsOnStartFailure();
    void weakFolderWithRenamedChild();
};

void TstExecutor::runsAllSteps()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    QCOMPARE(plan.steps.size(), 3);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    QList<int> progress;
    const RunOutcome out =
            ex.run(plan, cancel, [&](int done, int total) { progress.append(done * 10 + total); });

    QVERIFY(out.finished);
    QVERIFY(!out.cancelled);
    QCOMPARE(out.failedStep, -1);
    QCOMPARE(out.okSteps, (QList<int>{0, 1, 2}));
    QCOMPARE(progress, (QList<int>{13, 23, 33}));
    QCOMPARE(fs.names(QStringLiteral("/f")), (QStringList{"a.txt", "b.txt"}));
    QCOMPARE(fs.content(QStringLiteral("/f/a.txt")), QByteArray("B"));
    QCOMPARE(fs.content(QStringLiteral("/f/b.txt")), QByteArray("A"));
    QCOMPARE(sink.lines.size(), 3);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(sink.lines[i].rec.step, i);
        QCOMPARE(sink.lines[i].rec.phase, Phase::Forward);
        QVERIFY(sink.lines[i].rec.ok);
        QCOMPARE(sink.lines[i].step.from, plan.steps[i].from);
        QVERIFY(sink.lines[i].rec.time.isValid());
    }
}

void TstExecutor::journalBeforeNextStep()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    MemorySink sink;
    QList<int> seen; // journal size at the start of each rename
    fs.onBeforeRename([&](const QString &, const QString &) { seen.append(sink.lines.size()); });
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    QVERIFY(ex.run(plan, cancel).finished);
    QCOMPARE(seen, (QList<int>{0, 1, 2}));
}

void TstExecutor::failureStops()
{
    FakeFileSystem fs;
    const Plan plan = threePlan(fs);
    fs.failRename(plan.steps[1].from, EACCES, QStringLiteral("Permission denied"));
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);

    QVERIFY(!out.finished);
    QCOMPARE(out.failedStep, 1);
    QCOMPARE(out.failure.errorCode, EACCES);
    QCOMPARE(out.okSteps, (QList<int>{0}));
    QCOMPARE(fs.renameCount(), 2);
    QVERIFY(fs.exists(plan.steps[2].from));
    QCOMPARE(sink.lines.size(), 2);
    QVERIFY(!sink.lines[1].rec.ok);
    QCOMPARE(sink.lines[1].rec.errorCode, EACCES);
    QCOMPARE(sink.lines[1].rec.errorText, QStringLiteral("Permission denied"));
}

void TstExecutor::rollBackRestores()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    fs.addFile(QStringLiteral("/f/c.txt"), "C");
    const Plan plan = planFor(fs, {{QStringLiteral("/f/a.txt"), QStringLiteral("b.txt")},
                                   {QStringLiteral("/f/b.txt"), QStringLiteral("a.txt")},
                                   {QStringLiteral("/f/c.txt"), QStringLiteral("d.txt")}});
    QCOMPARE(plan.steps.size(), 4);
    const int last = int(plan.steps.size()) - 1;
    fs.failRename(plan.steps[last].from, EIO, QStringLiteral("I/O error"));
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);
    QCOMPARE(out.failedStep, last);
    QCOMPARE(out.okSteps.size(), last);

    const RollbackOutcome rb = ex.rollBack(plan, out.okSteps);
    QVERIFY(rb.complete);
    QVERIFY(rb.failedSteps.isEmpty());
    QCOMPARE(fs.names(QStringLiteral("/f")), (QStringList{"a.txt", "b.txt", "c.txt"}));
    QCOMPARE(fs.content(QStringLiteral("/f/a.txt")), QByteArray("A"));
    QCOMPARE(fs.content(QStringLiteral("/f/b.txt")), QByteArray("B"));
    QCOMPARE(fs.content(QStringLiteral("/f/c.txt")), QByteArray("C"));

    // Rollback records, newest forward step first.
    const QList<MemorySink::Line> back = sink.lines.mid(last + 1);
    QCOMPARE(back.size(), last);
    for (int k = 0; k < last; ++k) {
        QCOMPARE(back[k].rec.phase, Phase::Rollback);
        QCOMPARE(back[k].rec.step, out.okSteps[last - 1 - k]);
        QVERIFY(back[k].rec.ok);
    }
}

void TstExecutor::raceTargetAppears()
{
    FakeFileSystem fs;
    fs.addFolder(QStringLiteral("/f"));
    fs.addFile(QStringLiteral("/f/a.txt"), "A");
    const Plan plan = planFor(fs, {{QStringLiteral("/f/a.txt"), QStringLiteral("b.txt")}});
    bool added = false;
    fs.onBeforeRename([&](const QString &, const QString &) {
        if (!added) {
            added = true;
            fs.addFile(QStringLiteral("/f/b.txt"), "OTHER");
        }
    });
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);

    QCOMPARE(out.failedStep, 0);
    QCOMPARE(out.failure.errorCode, ErrorCode::TargetExists);
    QVERIFY(out.okSteps.isEmpty());
    QCOMPARE(fs.content(QStringLiteral("/f/b.txt")), QByteArray("OTHER"));
    QCOMPARE(fs.content(QStringLiteral("/f/a.txt")), QByteArray("A"));
    QCOMPARE(sink.lines.size(), 1);
    QCOMPARE(sink.lines[0].rec.errorCode, ErrorCode::TargetExists);
}

void TstExecutor::identityChangedStops()
{
    FakeFileSystem fs;
    fs.addFolder(QStringLiteral("/f"));
    fs.addFile(QStringLiteral("/f/a.txt"), "A");
    const Plan plan = planFor(fs, {{QStringLiteral("/f/a.txt"), QStringLiteral("b.txt")}});
    // Another file with the same name (a new inode) replaces the planned one.
    QVERIFY(fs.remove(QStringLiteral("/f/a.txt")));
    fs.addFile(QStringLiteral("/f/a.txt"), "NEW");
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);

    QCOMPARE(out.failedStep, 0);
    QCOMPARE(out.failure.errorCode, ErrorCode::IdentityChanged);
    QCOMPARE(fs.renameCount(), 0);
    QCOMPARE(fs.names(QStringLiteral("/f")), (QStringList{"a.txt"}));
    QCOMPARE(sink.lines.size(), 1);
    QVERIFY(!sink.lines[0].rec.ok);
    QCOMPARE(sink.lines[0].rec.errorCode, ErrorCode::IdentityChanged);
}

void TstExecutor::sourceMissingStops()
{
    FakeFileSystem fs;
    const Plan plan = threePlan(fs);
    QVERIFY(fs.remove(plan.steps[1].from));
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);

    QCOMPARE(out.failedStep, 1);
    QCOMPARE(out.failure.errorCode, ErrorCode::SourceMissing);
    QCOMPARE(out.okSteps, (QList<int>{0}));
    QCOMPARE(fs.renameCount(), 1);
}

void TstExecutor::cancelStopsAfterCurrent()
{
    FakeFileSystem fs;
    const Plan plan = threePlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel, [&](int done, int) {
        if (done == 1)
            cancel.store(true);
    });

    QVERIFY(out.cancelled);
    QVERIFY(!out.finished);
    QCOMPARE(out.failedStep, -1);
    QCOMPARE(out.okSteps, (QList<int>{0}));
    QCOMPARE(fs.renameCount(), 1);
    QCOMPARE(sink.lines.size(), 1);
}

void TstExecutor::cancelMidCycleRollBackRestoresAll()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    const int toTemp = indexOfKind(plan, StepKind::ToTemp);
    QCOMPARE(toTemp, 0);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel, [&](int done, int) {
        if (done == toTemp + 1)
            cancel.store(true);
    });
    QVERIFY(out.cancelled);
    QVERIFY(hasTempName(fs.names(QStringLiteral("/f"))));

    const RollbackOutcome rb = ex.rollBack(plan, out.okSteps);
    QVERIFY(rb.complete);
    QVERIFY(!hasTempName(fs.names(QStringLiteral("/f"))));
    QCOMPARE(fs.names(QStringLiteral("/f")), (QStringList{"a.txt", "b.txt"}));
    QCOMPARE(fs.content(QStringLiteral("/f/a.txt")), QByteArray("A"));
    QCOMPARE(fs.content(QStringLiteral("/f/b.txt")), QByteArray("B"));
}

void TstExecutor::restoreTempNamesOnly()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    fs.addFile(QStringLiteral("/f/sub/c.txt"), "C");
    // Deepest folder first: the /f/sub rename runs before the swap.
    const Plan plan = planFor(fs, {{QStringLiteral("/f/a.txt"), QStringLiteral("b.txt")},
                                   {QStringLiteral("/f/b.txt"), QStringLiteral("a.txt")},
                                   {QStringLiteral("/f/sub/c.txt"), QStringLiteral("d.txt")}});
    QCOMPARE(plan.steps[0].from, QStringLiteral("/f/sub/c.txt"));
    const int toTemp = indexOfKind(plan, StepKind::ToTemp);
    QCOMPARE(toTemp, 1);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel, [&](int done, int) {
        if (done == toTemp + 1)
            cancel.store(true);
    });
    QCOMPARE(out.okSteps, (QList<int>{0, 1}));
    const int linesBefore = int(sink.lines.size());

    const RollbackOutcome rb = ex.restoreTempNames(plan, out.okSteps);
    QVERIFY(rb.complete);
    QVERIFY(!hasTempName(fs.names(QStringLiteral("/f"))));
    QCOMPARE(fs.names(QStringLiteral("/f")), (QStringList{"a.txt", "b.txt", "sub"}));
    QCOMPARE(fs.content(QStringLiteral("/f/a.txt")), QByteArray("A"));
    // The other ok step stays done.
    QCOMPARE(fs.names(QStringLiteral("/f/sub")), (QStringList{"d.txt"}));
    // One rollback record of the ToTemp step.
    QCOMPARE(sink.lines.size(), linesBefore + 1);
    QCOMPARE(sink.lines.last().rec.phase, Phase::Rollback);
    QCOMPARE(sink.lines.last().rec.step, toTemp);
    QVERIFY(sink.lines.last().rec.ok);
}

void TstExecutor::restoreTempNamesUsesNewNameWhenOldTaken()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel, [&](int, int) { cancel.store(true); });
    QCOMPARE(out.okSteps, (QList<int>{0}));
    // Another program takes the old name and frees the new name.
    fs.addFile(QStringLiteral("/f/a.txt"), "INTRUDER");
    QVERIFY(fs.remove(QStringLiteral("/f/b.txt")));
    const int linesBefore = int(sink.lines.size());

    const RollbackOutcome rb = ex.restoreTempNames(plan, out.okSteps);
    QVERIFY(rb.complete);
    QVERIFY(!hasTempName(fs.names(QStringLiteral("/f"))));
    QCOMPARE(fs.content(QStringLiteral("/f/b.txt")), QByteArray("A"));
    QCOMPARE(fs.content(QStringLiteral("/f/a.txt")), QByteArray("INTRUDER"));

    const int fromTemp = indexOfKind(plan, StepKind::FromTemp);
    QCOMPARE(sink.lines.size(), linesBefore + 2);
    const MemorySink::Line &first = sink.lines[linesBefore];
    QCOMPARE(first.rec.phase, Phase::Rollback);
    QCOMPARE(first.rec.step, 0);
    QVERIFY(!first.rec.ok);
    QCOMPARE(first.rec.errorCode, ErrorCode::TargetExists);
    const MemorySink::Line &second = sink.lines[linesBefore + 1];
    QCOMPARE(second.rec.phase, Phase::Forward);
    QCOMPARE(second.rec.step, fromTemp);
    QVERIFY(second.rec.ok);
}

void TstExecutor::restoreTempNamesKeepsTempWhenBothTaken()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel, [&](int, int) { cancel.store(true); });
    QCOMPARE(out.okSteps, (QList<int>{0}));
    fs.addFile(QStringLiteral("/f/a.txt"), "INTRUDER"); // b.txt is still there

    const RollbackOutcome rb = ex.restoreTempNames(plan, out.okSteps);
    QVERIFY(!rb.complete);
    QCOMPARE(rb.failedSteps, (QList<int>{0}));
    QCOMPARE(rb.lastFailure.errorCode, ErrorCode::TargetExists);
    QVERIFY(fs.exists(plan.steps[0].to));
    QCOMPARE(fs.content(plan.steps[0].to), QByteArray("A"));
    QCOMPARE(fs.content(QStringLiteral("/f/b.txt")), QByteArray("B"));
    // Both tries are in the journal with their errors.
    QCOMPARE(sink.lines.size(), 3);
    QCOMPARE(sink.lines[1].rec.phase, Phase::Rollback);
    QVERIFY(!sink.lines[1].rec.ok);
    QCOMPARE(sink.lines[2].rec.phase, Phase::Forward);
    QCOMPARE(sink.lines[2].rec.step, indexOfKind(plan, StepKind::FromTemp));
    QVERIFY(!sink.lines[2].rec.ok);
}

void TstExecutor::restoreTempNamesJournalShowsNewName()
{
    // Old name taken -> try 2 (FromTemp). Readers of the journal must see the item at its new
    // name, and a recovery roll back from the journal must put it back to its old name.
    TempTree t;
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    JournalHeader h;
    h.batchId = kBatch;
    h.started = QDateTime::currentDateTimeUtc();
    h.ctrmVersion = QStringLiteral("0.1.0");
    h.os = QStringLiteral("test");
    QString error;
    auto writer = JournalWriter::create(t.root(), h, plan, &error);
    QVERIFY2(writer, qPrintable(error));
    Executor ex(fs, *writer);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel, [&](int, int) { cancel.store(true); });
    QCOMPARE(out.okSteps, (QList<int>{0}));
    fs.addFile(QStringLiteral("/f/a.txt"), "INTRUDER");
    QVERIFY(fs.remove(QStringLiteral("/f/b.txt")));
    QVERIFY(ex.restoreTempNames(plan, out.okSteps).complete);

    const auto rec = readJournal(writer->filePath(), &error);
    QVERIFY2(rec, qPrintable(error));
    // Replay the ok records: where is item 1 now, and which forward steps are still done?
    const PlanStep &first = rec->plan.steps[0];
    QString position = first.from;
    QList<int> done;
    for (const StepRecord &r : rec->steps) {
        if (!r.ok)
            continue;
        const PlanStep &s = rec->plan.steps[r.step];
        if (r.phase == Phase::Forward)
            done.append(r.step);
        else
            done.removeAll(r.step);
        if (s.item == first.item)
            position = r.phase == Phase::Forward ? s.to : s.from;
    }
    const int fromTemp = indexOfKind(plan, StepKind::FromTemp);
    QCOMPARE(position, QStringLiteral("/f/b.txt"));
    QCOMPARE(done, (QList<int>{0, fromTemp}));
    QCOMPARE(fs.content(position), QByteArray("A"));

    // Recovery roll back, after the other program frees the old name.
    QVERIFY(fs.remove(QStringLiteral("/f/a.txt")));
    const RollbackOutcome rb = ex.rollBack(rec->plan, done);
    QVERIFY(rb.complete);
    QCOMPARE(fs.names(QStringLiteral("/f")), (QStringList{"a.txt"}));
    QCOMPARE(fs.content(QStringLiteral("/f/a.txt")), QByteArray("A"));
}

void TstExecutor::stopsWhenJournalAppendFails()
{
    FakeFileSystem fs;
    const Plan plan = threePlan(fs);
    MemorySink sink;
    sink.failOn = 2;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);

    QVERIFY(!out.finished);
    QCOMPARE(out.failedStep, 1);
    QCOMPARE(out.failure.errorCode, ErrorCode::JournalWrite);
    QCOMPARE(out.okSteps, (QList<int>{0, 1})); // step 1 was done; roll back must undo it
    QCOMPARE(fs.renameCount(), 2);
    QVERIFY(fs.exists(plan.steps[2].from));

    const RollbackOutcome rb = ex.rollBack(plan, out.okSteps);
    QVERIFY(rb.complete);
    QCOMPARE(fs.names(QStringLiteral("/f")), (QStringList{"a.txt", "b.txt", "c.txt"}));
}

void TstExecutor::rollBackContinuesOnFailure()
{
    FakeFileSystem fs;
    const Plan plan = threePlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);
    QVERIFY(out.finished);
    fs.failRename(plan.steps[1].to, EACCES, QStringLiteral("Permission denied"));

    const RollbackOutcome rb = ex.rollBack(plan, out.okSteps);
    QVERIFY(!rb.complete);
    QCOMPARE(rb.failedSteps, (QList<int>{1}));
    QCOMPARE(rb.lastFailure.errorCode, EACCES);
    QVERIFY(fs.exists(plan.steps[0].from));
    QVERIFY(fs.exists(plan.steps[1].to));
    QVERIFY(fs.exists(plan.steps[2].from));
    QCOMPARE(fs.renameCount(), 6);
    QCOMPARE(sink.lines.size(), 6);
    QVERIFY(!sink.lines[4].rec.ok);
    QCOMPARE(sink.lines[4].rec.phase, Phase::Rollback);
}

void TstExecutor::noTempLeftAfterSuccess()
{
    FakeFileSystem fs;
    fs.addFolder(QStringLiteral("/f"));
    fs.addFile(QStringLiteral("/f/1.txt"), "1");
    fs.addFile(QStringLiteral("/f/2.txt"), "2");
    fs.addFile(QStringLiteral("/f/3.txt"), "3");
    // A three-cycle plus a swap.
    fs.addFile(QStringLiteral("/f/a.txt"), "A");
    fs.addFile(QStringLiteral("/f/b.txt"), "B");
    const Plan plan = planFor(fs, {{QStringLiteral("/f/1.txt"), QStringLiteral("2.txt")},
                                   {QStringLiteral("/f/2.txt"), QStringLiteral("3.txt")},
                                   {QStringLiteral("/f/3.txt"), QStringLiteral("1.txt")},
                                   {QStringLiteral("/f/a.txt"), QStringLiteral("b.txt")},
                                   {QStringLiteral("/f/b.txt"), QStringLiteral("a.txt")}});
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    QVERIFY(ex.run(plan, cancel).finished);
    QVERIFY(!hasTempName(fs.names(QStringLiteral("/f"))));
    QCOMPARE(fs.content(QStringLiteral("/f/2.txt")), QByteArray("1"));
    QCOMPARE(fs.content(QStringLiteral("/f/1.txt")), QByteArray("3"));
    QCOMPARE(fs.content(QStringLiteral("/f/b.txt")), QByteArray("A"));
}

void TstExecutor::cancelBeforeStartRunsNothing()
{
    FakeFileSystem fs;
    const Plan plan = threePlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{true};
    const RunOutcome out = ex.run(plan, cancel);
    QVERIFY(out.cancelled);
    QVERIFY(out.okSteps.isEmpty());
    QCOMPARE(fs.renameCount(), 0);
    QVERIFY(sink.lines.isEmpty());
}

void TstExecutor::rollBackReportsJournalFailure()
{
    FakeFileSystem fs;
    const Plan plan = threePlan(fs);
    MemorySink sink;
    sink.failOn = 5; // the second rollback record
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);
    QVERIFY(out.finished);

    const RollbackOutcome rb = ex.rollBack(plan, out.okSteps);
    // All renames still run, but the outcome is not complete.
    QCOMPARE(fs.names(QStringLiteral("/f")), (QStringList{"a.txt", "b.txt", "c.txt"}));
    QVERIFY(rb.journalFailed);
    QVERIFY(!rb.complete);
    QVERIFY(rb.failedSteps.isEmpty());
    QCOMPARE(rb.lastFailure.errorCode, ErrorCode::JournalWrite);
}

void TstExecutor::restoreTempNamesReportsJournalFailure()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    MemorySink sink;
    sink.failOn = 2; // the record of try 1
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel, [&](int, int) { cancel.store(true); });
    QCOMPARE(out.okSteps, (QList<int>{0}));

    const RollbackOutcome rb = ex.restoreTempNames(plan, out.okSteps);
    QCOMPARE(fs.names(QStringLiteral("/f")), (QStringList{"a.txt", "b.txt"}));
    QVERIFY(rb.journalFailed);
    QVERIFY(!rb.complete);
    QCOMPARE(rb.lastFailure.errorCode, ErrorCode::JournalWrite);
}

void TstExecutor::restoreTempNamesSkipsFinishedItems()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);
    QVERIFY(out.finished);
    const int renames = fs.renameCount();

    // Each item's last ok step is Direct or FromTemp: nothing to do.
    const RollbackOutcome rb = ex.restoreTempNames(plan, out.okSteps);
    QVERIFY(rb.complete);
    QCOMPARE(fs.renameCount(), renames);
    QCOMPARE(sink.lines.size(), 3);
    QCOMPARE(fs.content(QStringLiteral("/f/a.txt")), QByteArray("B"));
}

void TstExecutor::rollBackSkipsReplacedItem()
{
    FakeFileSystem fs;
    const Plan plan = threePlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);
    QVERIFY(out.finished);
    // Another program replaces the renamed file y.txt with a new file.
    QVERIFY(fs.remove(plan.steps[1].to));
    fs.addFile(plan.steps[1].to, "OTHER");
    const int renames = fs.renameCount();

    const RollbackOutcome rb = ex.rollBack(plan, out.okSteps);
    QVERIFY(!rb.complete);
    QCOMPARE(rb.failedSteps, (QList<int>{1}));
    QCOMPARE(rb.lastFailure.errorCode, ErrorCode::IdentityChanged);
    // The new file is not moved; the other steps are reversed.
    QCOMPARE(fs.renameCount(), renames + 2);
    QCOMPARE(fs.content(plan.steps[1].to), QByteArray("OTHER"));
    QVERIFY(!fs.exists(plan.steps[1].from));
    QCOMPARE(fs.content(plan.steps[0].from), QByteArray("A"));
    QCOMPARE(fs.content(plan.steps[2].from), QByteArray("C"));
    QCOMPARE(sink.lines.size(), 6);
    QCOMPARE(sink.lines[4].rec.step, 1);
    QCOMPARE(sink.lines[4].rec.phase, Phase::Rollback);
    QVERIFY(!sink.lines[4].rec.ok);
    QCOMPARE(sink.lines[4].rec.errorCode, ErrorCode::IdentityChanged);
}

void TstExecutor::rollBackSkipsMissingItem()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);
    QVERIFY(out.finished);
    // The item renamed by the last step (now b.txt, content "A") is deleted.
    QVERIFY(fs.remove(QStringLiteral("/f/b.txt")));

    const RollbackOutcome rb = ex.rollBack(plan, out.okSteps);
    QVERIFY(!rb.complete);
    QCOMPARE(rb.failedSteps, (QList<int>{2, 0}));
    QCOMPARE(rb.lastFailure.errorCode, ErrorCode::SourceMissing);
    // The other item goes back to b.txt.
    QCOMPARE(fs.names(QStringLiteral("/f")), (QStringList{"b.txt"}));
    QCOMPARE(fs.content(QStringLiteral("/f/b.txt")), QByteArray("B"));
}

void TstExecutor::restoreTempNamesSkipsReplacedItem()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel, [&](int, int) { cancel.store(true); });
    QCOMPARE(out.okSteps, (QList<int>{0}));
    const QString temp = plan.steps[0].to;
    QVERIFY(fs.remove(temp));
    fs.addFile(temp, "OTHER");
    const int renames = fs.renameCount();

    const RollbackOutcome rb = ex.restoreTempNames(plan, out.okSteps);
    QVERIFY(!rb.complete);
    QCOMPARE(rb.failedSteps, (QList<int>{0}));
    QCOMPARE(rb.lastFailure.errorCode, ErrorCode::IdentityChanged);
    QCOMPARE(fs.renameCount(), renames);
    QCOMPARE(fs.content(temp), QByteArray("OTHER"));
    QCOMPARE(sink.lines.size(), 2);
    QCOMPARE(sink.lines[1].rec.phase, Phase::Rollback);
    QCOMPARE(sink.lines[1].rec.errorCode, ErrorCode::IdentityChanged);
}

void TstExecutor::startRecordBeforeEachRename()
{
    FakeFileSystem fs;
    const Plan plan = threePlan(fs);
    MemorySink sink;
    QList<QPair<int, int>> seen; // (start records, op records) at each rename
    fs.onBeforeRename([&](const QString &, const QString &) {
        seen.append({int(sink.starts.size()), int(sink.lines.size())});
    });
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);
    QVERIFY(out.finished);
    QVERIFY(ex.rollBack(plan, out.okSteps).complete);
    // Each rename has its start record, and the record of the rename before it.
    QCOMPARE(seen, (QList<QPair<int, int>>{{1, 0}, {2, 1}, {3, 2}, {4, 3}, {5, 4}, {6, 5}}));
    QCOMPARE(sink.starts[0].step, 0);
    QCOMPARE(sink.starts[0].phase, Phase::Forward);
    QCOMPARE(sink.starts[3].step, 2);
    QCOMPARE(sink.starts[3].phase, Phase::Rollback);
}

void TstExecutor::startWriteFailureStopsRun()
{
    FakeFileSystem fs;
    const Plan plan = threePlan(fs);
    MemorySink sink;
    sink.failStartOn = 2;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);
    QVERIFY(!out.finished);
    QCOMPARE(out.failedStep, 1);
    QCOMPARE(out.failure.errorCode, ErrorCode::JournalWrite);
    QCOMPARE(out.okSteps, (QList<int>{0}));
    QCOMPARE(fs.renameCount(), 1); // no rename without its start record
    QVERIFY(fs.exists(plan.steps[1].from));
    QCOMPARE(sink.lines.size(), 1);
}

void TstExecutor::rollBackStartFailureDoesNotRename()
{
    FakeFileSystem fs;
    const Plan plan = threePlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel);
    QVERIFY(out.finished);
    sink.failStartOn = sink.startCalls + 2; // the start record of the roll back of step 1

    const RollbackOutcome rb = ex.rollBack(plan, out.okSteps);
    QVERIFY(!rb.complete);
    QVERIFY(rb.journalFailed);
    QCOMPARE(rb.failedSteps, (QList<int>{1}));
    QCOMPARE(rb.lastFailure.errorCode, ErrorCode::JournalWrite);
    QCOMPARE(fs.renameCount(), 5);
    QVERIFY(fs.exists(plan.steps[1].to)); // not moved
    QVERIFY(fs.exists(plan.steps[0].from));
    QVERIFY(fs.exists(plan.steps[2].from));
}

void TstExecutor::restoreTempNamesWritesStarts()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel, [&](int, int) { cancel.store(true); });
    QCOMPARE(out.okSteps, (QList<int>{0}));
    QCOMPARE(plan.steps[0].kind, StepKind::ToTemp);
    const int before = int(sink.starts.size());
    QList<int> startsAtRename;
    fs.onBeforeRename([&](const QString &, const QString &) {
        startsAtRename.append(int(sink.starts.size()));
    });
    // Try 1 and try 2 both fail: each try has its own start record before the rename.
    fs.failRename(plan.steps[0].to, EACCES, QStringLiteral("Permission denied"));
    QVERIFY(!ex.restoreTempNames(plan, out.okSteps).complete);
    QCOMPARE(sink.starts.size(), before + 2);
    QCOMPARE(startsAtRename, (QList<int>{before + 1, before + 2}));
    QCOMPARE(sink.starts[before].step, 0);
    QCOMPARE(sink.starts[before].phase, Phase::Rollback);
    QCOMPARE(sink.starts[before + 1].step, indexOfKind(plan, StepKind::FromTemp));
    QCOMPARE(sink.starts[before + 1].phase, Phase::Forward);
}

void TstExecutor::restoreTempNamesStopsOnStartFailure()
{
    FakeFileSystem fs;
    addSwapFiles(fs);
    const Plan plan = swapPlan(fs);
    MemorySink sink;
    Executor ex(fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(plan, cancel, [&](int, int) { cancel.store(true); });
    QCOMPARE(out.okSteps, (QList<int>{0}));
    const int renames = fs.renameCount();
    sink.failStartOn = sink.startCalls + 1; // the start record of try 1
    const RollbackOutcome rb = ex.restoreTempNames(plan, out.okSteps);
    QVERIFY(!rb.complete);
    QVERIFY(rb.journalFailed);
    QCOMPARE(rb.failedSteps, (QList<int>{0}));
    QCOMPARE(fs.renameCount(), renames); // try 2 does not run either
    QVERIFY(fs.exists(plan.steps[0].to));
}

// Real folders: the child rename gives the folder a new modification time. With weak identity
// a folder matches on its type only, so the folder step still runs.
void TstExecutor::weakFolderWithRenamedChild()
{
    TempTree t;
    t.file(QStringLiteral("d/a.txt"), "A");
    const std::unique_ptr<FileSystem> fs = createNativeFileSystem();
    const auto request = [&](ItemId id, const QString &folder, const QString &oldName,
                             const QString &newName, bool isFolder) {
        RenameRequest r;
        r.item = id;
        r.folder = folder;
        r.oldName = oldName;
        r.newName = newName;
        r.isFolder = isFolder;
        r.identity = *fs->identity(folder + u'/' + oldName, IdentityType::Weak);
        return r;
    };
    const QList<RenameRequest> requests{
            request(1, t.root(), QStringLiteral("d"), QStringLiteral("e"), true),
            request(2, t.root() + QStringLiteral("/d"), QStringLiteral("a.txt"),
                    QStringLiteral("b.txt"), false)};
    QThread::msleep(20);
    const PlanResult planned = makePlan(requests, *fs, kBatch);
    QVERIFY2(planned.plan, qPrintable(planned.error));
    QCOMPARE(planned.plan->steps.first().item, ItemId(2)); // the child first

    MemorySink sink;
    Executor ex(*fs, sink);
    std::atomic<bool> cancel{false};
    const RunOutcome out = ex.run(*planned.plan, cancel);
    QVERIFY2(out.finished, qPrintable(out.failure.errorText));
    QVERIFY(QFile::exists(t.root() + QStringLiteral("/e/b.txt")));

    // A file at the folder's path is not the folder.
    QVERIFY(!sameFile(requests[0].identity,
                      *fs->identity(t.root() + QStringLiteral("/e/b.txt"), IdentityType::Weak)));
    // Roll back: the folder first, then the child.
    QVERIFY(ex.rollBack(*planned.plan, out.okSteps).complete);
    QVERIFY(QFile::exists(t.root() + QStringLiteral("/d/a.txt")));
}

QTEST_GUILESS_MAIN(TstExecutor)
#include "tst_executor.moc"
