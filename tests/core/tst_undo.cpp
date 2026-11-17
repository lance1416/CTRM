// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/batchservice.h"
#include "exec/undo.h"
#include "fakefilesystem.h"
#include "temptree.h"

#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <atomic>

using namespace ctrm;

namespace {

const AppInfo kApp{QStringLiteral("0.1.0"), QStringLiteral("test")};

struct Env
{
    FakeFileSystem fs;
    QTemporaryDir dir;
    HistoryStore history{dir.path()};
    BatchService service{fs, history, kApp};

    explicit Env(IdentityType type = IdentityType::Strong)
    {
        fs.addFolder(QStringLiteral("/f"),
                     VolumeInfo{CaseSensitivity::Sensitive, false, type, QStringLiteral("fakefs")});
    }
    void add(const QString &name, const QByteArray &content)
    {
        fs.addFile(QStringLiteral("/f/") + name, content);
    }
};

// One request per (oldPath, newName) pair, with the identity type of the volume.
Plan planFor(FakeFileSystem &fs, const QList<QPair<QString, QString>> &renames,
             const QSet<int> &folders = {})
{
    QList<RenameRequest> requests;
    ItemId id = 1;
    for (const auto &[path, newName] : renames) {
        RenameRequest r;
        r.item = id++;
        r.folder = path.left(path.lastIndexOf(u'/'));
        r.oldName = path.mid(path.lastIndexOf(u'/') + 1);
        r.newName = newName;
        r.isFolder = folders.contains(int(r.item));
        r.identity = *fs.identity(path, fs.volumeInfo(r.folder).identityType);
        requests.append(r);
    }
    PlanResult result = makePlan(requests, fs, newBatchId());
    if (!result.plan)
        qFatal("makePlan failed: %s", qPrintable(result.error));
    return *result.plan;
}

// Runs a plan to its end. stopAfter >= 0: cancel after that many steps and leave the batch
// in progress (as after a crash). Returns the batch ID.
QString runBatch(Env &env, const Plan &plan, const QString &undoOf = {}, int stopAfter = -1)
{
    QString error;
    auto s = env.service.start(plan, RuleChain(), undoOf, &error);
    if (!s)
        qFatal("start failed: %s", qPrintable(error));
    std::atomic<bool> cancel{false};
    const RunOutcome out = s->run(cancel, [&](int done, int) {
        if (done == stopAfter)
            cancel.store(true);
    });
    if (stopAfter < 0) {
        if (!out.finished)
            qFatal("run failed: %s", qPrintable(out.failure.errorText));
        s->complete();
    }
    return plan.batchId;
}

// previewUndo -> planUndo -> run. Returns the undo batch ID.
QString undo(Env &env, const UndoPreview &preview)
{
    const PlanResult plan = env.service.planUndo(preview);
    if (!plan.plan)
        qFatal("planUndo failed: %s", qPrintable(plan.error));
    return runBatch(env, *plan.plan, preview.originalBatchId);
}

BatchState stateOf(HistoryStore &history, const QString &batchId)
{
    QString error;
    const auto rec = history.load(batchId, &error);
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

} // namespace

class TstUndo : public QObject
{
    Q_OBJECT

private slots:
    void undoRestoresAll();
    void undoSwap();
    void undoAfterRestart();
    void undoOlderBatch();
    void undoReportsChangedItem();
    void quickUndoNeverPartial();
    void undoConflictWhenOldNameTaken();
    void partlyUndone();
    void undoBatchNotUndoable();
    void weakIdentityPreview();
    void undoFolderAndChild();
    void undoAfterMarkCompleteTemp();
    void rolledBackItemsNotListed();
    void markCompleteUndoBatchUpdatesOriginal();
    void markCompleteRefusesFinishedBatch();
    void undoNestedFolders();
    void undoChildAfterParentUndone();
    void undoWeakFolderWithRenamedChild();
};

void TstUndo::undoRestoresAll()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    env.add("c.txt", "C");
    const QString id = runBatch(
            env,
            planFor(env.fs, {{"/f/a.txt", "x.txt"}, {"/f/b.txt", "y.txt"}, {"/f/c.txt", "z.txt"}}));
    QString error;
    const UndoPreview p = env.service.previewUndo(id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(p.originalBatchId, id);
    QCOMPARE(p.items.size(), 3);
    QCOMPARE(p.passCount, 3);
    QVERIFY(!p.weak());
    QVERIFY(p.quickUndoAllowed());
    QCOMPARE(p.items[0].currentName, QStringLiteral("x.txt"));
    QCOMPARE(p.items[0].originalName, QStringLiteral("a.txt"));
    QCOMPARE(p.items[0].preview.status, Status::Changed);

    const QString undoId = undo(env, p);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt", "c.txt"}));
    QCOMPARE(env.fs.content("/f/b.txt"), QByteArray("B"));
    QCOMPARE(stateOf(env.history, id), BatchState::Undone);
    QCOMPARE(stateOf(env.history, undoId), BatchState::Complete);
    QString err2;
    QCOMPARE(env.history.load(undoId, &err2)->header.undoOf, id);
}

void TstUndo::undoSwap()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    const QString id =
            runBatch(env, planFor(env.fs, {{"/f/a.txt", "b.txt"}, {"/f/b.txt", "a.txt"}}));
    QString error;
    const UndoPreview p = env.service.previewUndo(id, &error);
    QVERIFY(p.quickUndoAllowed());
    undo(env, p);
    QCOMPARE(env.fs.content("/f/a.txt"), QByteArray("A"));
    QCOMPARE(env.fs.content("/f/b.txt"), QByteArray("B"));
    QVERIFY(!hasTempName(env.fs.names("/f")));
    QCOMPARE(stateOf(env.history, id), BatchState::Undone);
}

void TstUndo::undoAfterRestart()
{
    Env env;
    env.add("a.txt", "A");
    const QString id = runBatch(env, planFor(env.fs, {{"/f/a.txt", "x.txt"}}));
    HistoryStore history(env.dir.path());
    BatchService service(env.fs, history, kApp);
    QString error;
    const UndoPreview p = service.previewUndo(id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(p.quickUndoAllowed());
    const PlanResult plan = service.planUndo(p);
    QVERIFY(plan.plan);
    auto s = service.start(*plan.plan, RuleChain(), id, &error);
    QVERIFY(s);
    std::atomic<bool> cancel{false};
    QVERIFY(s->run(cancel).finished);
    QCOMPARE(s->complete().state, BatchState::Complete);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt"}));
    QCOMPARE(stateOf(history, id), BatchState::Undone);
}

void TstUndo::undoOlderBatch()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    env.add("c.txt", "C");
    env.add("d.txt", "D");
    const QString first =
            runBatch(env, planFor(env.fs, {{"/f/a.txt", "a1.txt"}, {"/f/b.txt", "b1.txt"}}));
    const QString second =
            runBatch(env, planFor(env.fs, {{"/f/c.txt", "c2.txt"}, {"/f/d.txt", "d2.txt"}}));
    QString error;
    const UndoPreview p = env.service.previewUndo(first, &error);
    QVERIFY(p.quickUndoAllowed());
    undo(env, p);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt", "c2.txt", "d2.txt"}));
    QCOMPARE(stateOf(env.history, first), BatchState::Undone);
    QCOMPARE(stateOf(env.history, second), BatchState::Complete);
}

void TstUndo::undoReportsChangedItem()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    env.add("c.txt", "C");
    const QString id = runBatch(
            env,
            planFor(env.fs, {{"/f/a.txt", "x.txt"}, {"/f/b.txt", "y.txt"}, {"/f/c.txt", "z.txt"}}));
    QVERIFY(env.fs.renameNoReplace("/f/y.txt", "/f/moved.txt").ok); // another program
    QString error;
    const UndoPreview p = env.service.previewUndo(id, &error);
    QCOMPARE(p.items.size(), 3);
    QCOMPARE(p.items[1].preview.status, Status::Missing);
    QCOMPARE(p.items[0].preview.status, Status::Changed);
    QCOMPARE(p.items[2].preview.status, Status::Changed);
    QCOMPARE(p.passCount, 2);
    QVERIFY(!p.quickUndoAllowed());
    QCOMPARE(undoRequests(p).size(), 2);
}

void TstUndo::quickUndoNeverPartial()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    const QString id =
            runBatch(env, planFor(env.fs, {{"/f/a.txt", "x.txt"}, {"/f/b.txt", "y.txt"}}));
    QVERIFY(env.fs.remove("/f/y.txt"));
    env.add("y.txt", "NEW"); // same name, other file
    const int renames = env.fs.renameCount();
    QString error;
    const UndoPreview p = env.service.previewUndo(id, &error);
    QCOMPARE(p.items[1].preview.status, Status::Missing);
    QCOMPARE(p.items[1].preview.problems.value(0).code, DiagCode::IdentityChanged);
    QVERIFY(!p.quickUndoAllowed());
    QCOMPARE(env.fs.renameCount(), renames); // the preview renames nothing
    // The changed item is never in the undo requests.
    const QList<RenameRequest> reqs = undoRequests(p);
    QCOMPARE(reqs.size(), 1);
    QCOMPARE(reqs[0].oldName, QStringLiteral("x.txt"));
}

void TstUndo::undoConflictWhenOldNameTaken()
{
    Env env;
    env.add("a.txt", "A");
    const QString id = runBatch(env, planFor(env.fs, {{"/f/a.txt", "x.txt"}}));
    env.add("a.txt", "OTHER");
    QString error;
    const UndoPreview p = env.service.previewUndo(id, &error);
    QCOMPARE(p.items[0].preview.status, Status::Conflict);
    QCOMPARE(p.passCount, 0);
    QVERIFY(!p.quickUndoAllowed());
    QVERIFY(!env.service.planUndo(p).plan);
}

void TstUndo::partlyUndone()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    const QString id =
            runBatch(env, planFor(env.fs, {{"/f/a.txt", "x.txt"}, {"/f/b.txt", "y.txt"}}));
    QVERIFY(env.fs.renameNoReplace("/f/y.txt", "/f/away.txt").ok);
    QString error;
    UndoPreview p = env.service.previewUndo(id, &error);
    QCOMPARE(p.passCount, 1);
    undo(env, p);
    QCOMPARE(stateOf(env.history, id), BatchState::PartlyUndone);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "away.txt"}));

    // The item is back; a second undo restores only it.
    QVERIFY(env.fs.renameNoReplace("/f/away.txt", "/f/y.txt").ok);
    p = env.service.previewUndo(id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(p.items.size(), 1);
    QCOMPARE(p.items[0].originalName, QStringLiteral("b.txt"));
    QVERIFY(p.quickUndoAllowed());
    undo(env, p);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt"}));
    QCOMPARE(stateOf(env.history, id), BatchState::Undone);
    QCOMPARE(env.history.undoBatchesOf(id).size(), 2);
}

void TstUndo::undoBatchNotUndoable()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    const QString id = runBatch(env, planFor(env.fs, {{"/f/a.txt", "x.txt"}}));
    QString error;
    const QString undoId = undo(env, env.service.previewUndo(id, &error));
    QVERIFY(error.isEmpty());
    const UndoPreview p = env.service.previewUndo(undoId, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(p.items.isEmpty());

    // An undone batch cannot be undone again.
    error.clear();
    QVERIFY(env.service.previewUndo(id, &error).items.isEmpty());
    QVERIFY(!error.isEmpty());

    // A rolled-back batch cannot be undone.
    const Plan plan = planFor(env.fs, {{"/f/b.txt", "y.txt"}});
    auto s = env.service.start(plan, RuleChain(), {}, &error);
    std::atomic<bool> cancel{false};
    s->run(cancel);
    QCOMPARE(s->rollBack().state, BatchState::RolledBack);
    error.clear();
    QVERIFY(env.service.previewUndo(plan.batchId, &error).items.isEmpty());
    QVERIFY(!error.isEmpty());
}

void TstUndo::weakIdentityPreview()
{
    Env env(IdentityType::Weak);
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    const QString id =
            runBatch(env, planFor(env.fs, {{"/f/a.txt", "x.txt"}, {"/f/b.txt", "y.txt"}}));
    QString error;
    const auto rec = env.history.load(id, &error);
    QCOMPARE(rec->plan.requests[0].identity.type, IdentityType::Weak);

    const UndoPreview p = env.service.previewUndo(id, &error);
    QCOMPARE(p.passCount, 2);
    QVERIFY(p.weak());
    QVERIFY(!p.quickUndoAllowed());
    QCOMPARE(p.weakFolders.size(), 1);
    QCOMPARE(p.weakFolders[0], (QPair<QString, QString>{"/f", "fakefs"}));
    for (const UndoItem &u : p.items)
        QVERIFY(u.preview.warnings.contains(Diagnostic{DiagCode::WeakIdentity, "fakefs"}));
    undo(env, p);
    QCOMPARE(env.fs.names("/f"), (QStringList{"a.txt", "b.txt"}));
}

void TstUndo::undoFolderAndChild()
{
    Env env;
    env.fs.addFolder("/f/d");
    env.fs.addFile("/f/d/f.txt", "F");
    const QString id =
            runBatch(env, planFor(env.fs, {{"/f/d", "e"}, {"/f/d/f.txt", "g.txt"}}, {1}));
    QCOMPARE(env.fs.names("/f"), (QStringList{"e"}));
    QCOMPARE(env.fs.names("/f/e"), (QStringList{"g.txt"}));

    QString error;
    const UndoPreview p = env.service.previewUndo(id, &error);
    QCOMPARE(p.items.size(), 2);
    QCOMPARE(p.items[1].currentFolder, QStringLiteral("/f/e")); // path remap
    QCOMPARE(p.items[1].currentName, QStringLiteral("g.txt"));
    QVERIFY(p.quickUndoAllowed());
    undo(env, p);
    QCOMPARE(env.fs.names("/f"), (QStringList{"d"}));
    QCOMPARE(env.fs.content("/f/d/f.txt"), QByteArray("F"));
    QCOMPARE(stateOf(env.history, id), BatchState::Undone);
}

void TstUndo::undoAfterMarkCompleteTemp()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    const Plan plan = planFor(env.fs, {{"/f/a.txt", "b.txt"}, {"/f/b.txt", "a.txt"}});
    QCOMPARE(plan.steps[0].kind, StepKind::ToTemp);
    const QString id = runBatch(env, plan, {}, 1); // crash after the ToTemp step
    QVERIFY(hasTempName(env.fs.names("/f")));
    QString error;
    QVERIFY(env.service.markComplete(id, &error));

    const UndoPreview p = env.service.previewUndo(id, &error);
    QCOMPARE(p.items.size(), 1);
    QCOMPARE(p.items[0].currentName, plan.steps[0].to.mid(3));
    QCOMPARE(p.items[0].originalName, QStringLiteral("a.txt"));
    QVERIFY(p.quickUndoAllowed());
    undo(env, p);
    QVERIFY(!hasTempName(env.fs.names("/f")));
    QCOMPARE(env.fs.content("/f/a.txt"), QByteArray("A"));
    QCOMPARE(env.fs.content("/f/b.txt"), QByteArray("B"));
    QCOMPARE(stateOf(env.history, id), BatchState::Undone);
}

void TstUndo::rolledBackItemsNotListed()
{
    // A Keep after a failure: the rolled-back temp item is not an undo item.
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    env.add("c.txt", "C");
    const Plan plan =
            planFor(env.fs, {{"/f/c.txt", "z.txt"}, {"/f/a.txt", "b.txt"}, {"/f/b.txt", "a.txt"}});
    QString error;
    auto s = env.service.start(plan, RuleChain(), {}, &error);
    int toTemp = -1;
    for (const PlanStep &st : plan.steps)
        if (st.kind == StepKind::ToTemp)
            toTemp = st.index;
    std::atomic<bool> cancel{false};
    s->run(cancel, [&](int done, int) {
        if (done == toTemp + 1)
            cancel.store(true);
    });
    QCOMPARE(s->keep().state, BatchState::Complete);
    const UndoPreview p = env.service.previewUndo(plan.batchId, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QStringList originals;
    for (const UndoItem &u : p.items)
        originals.append(u.originalName);
    QVERIFY(!originals.contains("a.txt"));
    QVERIFY(!originals.contains("b.txt"));
}

void TstUndo::markCompleteUndoBatchUpdatesOriginal()
{
    Env env;
    env.add("a.txt", "A");
    env.add("b.txt", "B");
    const QString id =
            runBatch(env, planFor(env.fs, {{"/f/a.txt", "x.txt"}, {"/f/b.txt", "y.txt"}}));
    QString error;
    const PlanResult plan = env.service.planUndo(env.service.previewUndo(id, &error));
    QVERIFY(plan.plan);
    const QString undoId = runBatch(env, *plan.plan, id, 1); // crash after one undo step
    QCOMPARE(stateOf(env.history, id), BatchState::Complete);
    QVERIFY2(env.service.markComplete(undoId, &error), qPrintable(error));
    QCOMPARE(stateOf(env.history, undoId), BatchState::Complete);
    QCOMPARE(stateOf(env.history, id), BatchState::PartlyUndone);
    const UndoPreview p = env.service.previewUndo(id, &error);
    QCOMPARE(p.items.size(), 1);
}

void TstUndo::markCompleteRefusesFinishedBatch()
{
    Env env;
    env.add("a.txt", "A");
    const QString id = runBatch(env, planFor(env.fs, {{"/f/a.txt", "x.txt"}}));
    QString error;
    QVERIFY(!env.service.markComplete(id, &error));
    QVERIFY(!error.isEmpty());
}

void TstUndo::undoNestedFolders()
{
    Env env;
    env.fs.addFolder("/f/d");
    env.fs.addFolder("/f/d/s");
    env.fs.addFile("/f/d/s/f.txt", "F");
    const QString id = runBatch(
            env,
            planFor(env.fs, {{"/f/d", "e"}, {"/f/d/s", "t"}, {"/f/d/s/f.txt", "g.txt"}}, {1, 2}));
    QCOMPARE(env.fs.names("/f/e/t"), (QStringList{"g.txt"}));
    QString error;
    const UndoPreview p = env.service.previewUndo(id, &error);
    QCOMPARE(p.items.size(), 3);
    QCOMPARE(p.items[1].currentFolder, QStringLiteral("/f/e"));
    QCOMPARE(p.items[2].currentFolder, QStringLiteral("/f/e/t"));
    QVERIFY(p.quickUndoAllowed());
    undo(env, p);
    QCOMPARE(env.fs.content("/f/d/s/f.txt"), QByteArray("F"));
    QCOMPARE(env.fs.names("/f"), (QStringList{"d"}));
    QCOMPARE(stateOf(env.history, id), BatchState::Undone);
}

void TstUndo::undoChildAfterParentUndone()
{
    Env env;
    env.fs.addFolder("/f/d");
    env.fs.addFile("/f/d/f.txt", "F");
    const QString id =
            runBatch(env, planFor(env.fs, {{"/f/d", "e"}, {"/f/d/f.txt", "g.txt"}}, {1}));
    QVERIFY(env.fs.renameNoReplace("/f/e/g.txt", "/f/e/away.txt").ok);
    QString error;
    UndoPreview p = env.service.previewUndo(id, &error);
    QCOMPARE(p.passCount, 1); // the folder only
    undo(env, p);
    QCOMPARE(stateOf(env.history, id), BatchState::PartlyUndone);
    QCOMPARE(env.fs.names("/f/d"), (QStringList{"away.txt"}));

    // The parent is back at its old name: the child is found there, without remap.
    QVERIFY(env.fs.renameNoReplace("/f/d/away.txt", "/f/d/g.txt").ok);
    p = env.service.previewUndo(id, &error);
    QCOMPARE(p.items.size(), 1);
    QCOMPARE(p.items[0].currentFolder, QStringLiteral("/f/d"));
    QVERIFY(p.quickUndoAllowed());
    undo(env, p);
    QCOMPARE(env.fs.names("/f/d"), (QStringList{"f.txt"}));
    QCOMPARE(stateOf(env.history, id), BatchState::Undone);
}

// Real folders: renaming a child changes the modification time of its folder. On a volume with
// weak identity, a folder matches on its type only (its name is checked by the path).
void TstUndo::undoWeakFolderWithRenamedChild()
{
    TempTree t;
    t.file("d/a.txt", "A");
    const std::unique_ptr<FileSystem> fs = createNativeFileSystem();
    QTemporaryDir dir;
    HistoryStore history(dir.path());
    BatchService service(*fs, history, kApp);

    const auto request = [&](ItemId id, const QString &folder, const QString &oldName,
                             const QString &newName, bool isFolder) {
        RenameRequest r;
        r.item = id;
        r.folder = folder;
        r.oldName = oldName;
        r.newName = newName;
        r.isFolder = isFolder;
        r.identity = *fs->identity(folder + "/" + oldName, IdentityType::Weak);
        return r;
    };
    const QList<RenameRequest> requests{request(1, t.root(), "d", "e", true),
                                        request(2, t.root() + "/d", "a.txt", "b.txt", false)};
    QThread::msleep(20); // so that a rename in the folder gives it a new modification time
    PlanResult planned = makePlan(requests, *fs, newBatchId());
    QVERIFY2(planned.plan, qPrintable(planned.error));
    QString error;
    auto session = service.start(*planned.plan, RuleChain(), {}, &error);
    QVERIFY2(session, qPrintable(error));
    std::atomic<bool> cancel{false};
    const RunOutcome run = session->run(cancel);
    QVERIFY2(run.finished, qPrintable(run.failure.errorText));
    QCOMPARE(session->complete().state, BatchState::Complete);
    QVERIFY(QFile::exists(t.root() + "/e/b.txt"));

    QThread::msleep(20);
    const UndoPreview preview = service.previewUndo(planned.plan->batchId, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.items.size(), 2);
    QCOMPARE(preview.passCount, 2);
    PlanResult undo = service.planUndo(preview);
    QVERIFY2(undo.plan, qPrintable(undo.error));
    auto undoSession = service.start(*undo.plan, RuleChain(), planned.plan->batchId, &error);
    QVERIFY2(undoSession, qPrintable(error));
    const RunOutcome undone = undoSession->run(cancel);
    QVERIFY2(undone.finished, qPrintable(undone.failure.errorText));
    QCOMPARE(undoSession->complete().state, BatchState::Complete);
    QVERIFY(QFile::exists(t.root() + "/d/a.txt"));
}

QTEST_GUILESS_MAIN(TstUndo)
#include "tst_undo.moc"
