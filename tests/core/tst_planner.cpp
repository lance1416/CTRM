// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/planner.h"
#include "fakefilesystem.h"

#include <QRandomGenerator>
#include <QtTest>

#include <algorithm>

using namespace ctrm;

namespace {

const QString kBatch = QStringLiteral("1234abcd-5678-90ab-cdef-1234567890ab");

VolumeInfo volume(CaseSensitivity cs)
{
    return VolumeInfo{cs, false, IdentityType::Strong, QStringLiteral("fakefs")};
}

RenameRequest req(const QString &folder, const QString &oldName, const QString &newName,
                  ItemId id = 0)
{
    RenameRequest r;
    r.item = id;
    r.folder = folder;
    r.oldName = oldName;
    r.newName = newName;
    return r;
}

QString tmp(int n)
{
    return tempName(kBatch, n);
}

// "from>to" with names only (all steps in one folder) or full paths.
QStringList describe(const Plan &plan, bool fullPaths = false)
{
    QStringList out;
    for (const PlanStep &s : plan.steps) {
        if (fullPaths)
            out.append(s.from + u'>' + s.to);
        else
            out.append(s.from.mid(s.from.lastIndexOf(u'/') + 1) + u'>' +
                       s.to.mid(s.to.lastIndexOf(u'/') + 1));
    }
    return out;
}

// Runs all steps with no-replace renames. Returns the first failure text, or empty.
QString runPlan(FakeFileSystem &fs, const Plan &plan)
{
    for (const PlanStep &s : plan.steps) {
        const RenameResult r = fs.renameNoReplace(s.from, s.to);
        if (!r.ok)
            return QStringLiteral("step %1 %2 -> %3: %4")
                    .arg(s.index)
                    .arg(s.from, s.to, r.errorText);
    }
    return {};
}

} // namespace

class TestPlanner : public QObject
{
    Q_OBJECT
private slots:
    void chainNoTemp();
    void swapUsesTemp();
    void threeCycle();
    void caseOnlyInsensitive();
    void caseOnlySensitive();
    void folderAndChild();
    void foldersDeepestFirst();
    void tempNameFormat();
    void newBatchIdFormat();
    void tempNameSkipsExisting();
    void tempCounterSharedAcrossFolders();
    void targetTakenError();
    void targetTakenIgnoringCaseError();
    void duplicateTargetError();
    void sameNameError();
    void itemTwiceError();
    void unreadableFolderError();
    void stepIndexesAndItems();
    void mixedCyclesAndChain();
    void folderCycleWithChild();
    void sensitiveBothCasesAsOldNames();
    void applyPlanOnFake();
    void applyPlanOnFakeInsensitive();
};

void TestPlanner::chainNoTemp()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a");
    fs.addFile("/f/b");
    const PlanResult r = makePlan({req("/f", "a", "b"), req("/f", "b", "c")}, fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QCOMPARE(describe(*r.plan), QStringList({"b>c", "a>b"}));
    for (const PlanStep &s : r.plan->steps)
        QCOMPARE(s.kind, StepKind::Direct);
    QCOMPARE(r.plan->batchId, kBatch);
    QCOMPARE(r.plan->requests.size(), 2);
}

void TestPlanner::swapUsesTemp()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a");
    fs.addFile("/f/b");
    const PlanResult r = makePlan({req("/f", "a", "b"), req("/f", "b", "a")}, fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QCOMPARE(describe(*r.plan), QStringList({"a>" + tmp(1), "b>a", tmp(1) + ">b"}));
    QCOMPARE(r.plan->steps[0].kind, StepKind::ToTemp);
    QCOMPARE(r.plan->steps[1].kind, StepKind::Direct);
    QCOMPARE(r.plan->steps[2].kind, StepKind::FromTemp);
}

void TestPlanner::threeCycle()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a", "A");
    fs.addFile("/f/b", "B");
    fs.addFile("/f/c", "C");
    const PlanResult r =
            makePlan({req("/f", "a", "b"), req("/f", "b", "c"), req("/f", "c", "a")}, fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QCOMPARE(r.plan->steps.size(), 4);
    const auto temps = std::count_if(r.plan->steps.begin(), r.plan->steps.end(),
                                     [](const PlanStep &s) { return s.kind == StepKind::ToTemp; });
    QCOMPARE(temps, 1);
    QCOMPARE(describe(*r.plan), QStringList({"a>" + tmp(1), "c>a", "b>c", tmp(1) + ">b"}));
    QCOMPARE(runPlan(fs, *r.plan), QString());
    QCOMPARE(fs.content("/f/b"), QByteArray("A"));
    QCOMPARE(fs.content("/f/c"), QByteArray("B"));
    QCOMPARE(fs.content("/f/a"), QByteArray("C"));
}

void TestPlanner::caseOnlyInsensitive()
{
    FakeFileSystem fs;
    fs.addFolder("/f", volume(CaseSensitivity::Insensitive));
    fs.addFile("/f/photo.JPG");
    const PlanResult r = makePlan({req("/f", "photo.JPG", "photo.jpg")}, fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QCOMPARE(describe(*r.plan), QStringList({"photo.JPG>" + tmp(1), tmp(1) + ">photo.jpg"}));
    QCOMPARE(r.plan->steps[0].kind, StepKind::ToTemp);
    QCOMPARE(r.plan->steps[1].kind, StepKind::FromTemp);
    QCOMPARE(runPlan(fs, *r.plan), QString());
    QCOMPARE(fs.names("/f"), QStringList({"photo.jpg"}));

    // Unknown is treated as Insensitive.
    FakeFileSystem unk;
    unk.addFolder("/u", volume(CaseSensitivity::Unknown));
    unk.addFile("/u/A");
    const PlanResult u = makePlan({req("/u", "A", "a")}, unk, kBatch);
    QVERIFY2(u.plan, qPrintable(u.error));
    QCOMPARE(u.plan->steps.size(), 2);

    // Normalization-only rename on a normalization-insensitive volume.
    FakeFileSystem nfd;
    nfd.addFolder("/n", VolumeInfo{CaseSensitivity::Sensitive, true, IdentityType::Strong, "apfs"});
    nfd.addFile(QStringLiteral("/n/Café"));
    const PlanResult n =
            makePlan({req("/n", QStringLiteral("Café"), QStringLiteral("Café"))}, nfd, kBatch);
    QVERIFY2(n.plan, qPrintable(n.error));
    QCOMPARE(n.plan->steps.size(), 2);
    QCOMPARE(runPlan(nfd, *n.plan), QString());
}

void TestPlanner::caseOnlySensitive()
{
    FakeFileSystem fs;
    fs.addFolder("/f", volume(CaseSensitivity::Sensitive));
    fs.addFile("/f/photo.JPG");
    const PlanResult r = makePlan({req("/f", "photo.JPG", "photo.jpg")}, fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QCOMPARE(describe(*r.plan), QStringList({"photo.JPG>photo.jpg"}));
    QCOMPARE(r.plan->steps[0].kind, StepKind::Direct);
}

void TestPlanner::folderAndChild()
{
    FakeFileSystem fs;
    fs.addFolder("/r");
    fs.addFolder("/r/d");
    fs.addFile("/r/d/f.txt", "F");
    // The parent request comes first in the input.
    const PlanResult r = makePlan({req("/r", "d", "e"), req("/r/d", "f.txt", "g.txt")}, fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QCOMPARE(describe(*r.plan, true), QStringList({"/r/d/f.txt>/r/d/g.txt", "/r/d>/r/e"}));
    QCOMPARE(runPlan(fs, *r.plan), QString());
    QCOMPARE(fs.content("/r/e/g.txt"), QByteArray("F"));
}

void TestPlanner::foldersDeepestFirst()
{
    FakeFileSystem fs;
    fs.addFolder("/r/b");
    fs.addFolder("/r/a/x");
    fs.addFile("/r/b/1");
    fs.addFile("/r/a/2");
    fs.addFile("/r/a/x/3");
    fs.addFile("/r/4");
    const PlanResult r = makePlan({req("/r", "4", "44"), req("/r/b", "1", "11"),
                                   req("/r/a/x", "3", "33"), req("/r/a", "2", "22")},
                                  fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    // Depth 3, then depth 2 by path (/r/a before /r/b), then depth 1.
    QCOMPARE(describe(*r.plan), QStringList({"3>33", "2>22", "1>11", "4>44"}));
}

void TestPlanner::tempNameFormat()
{
    QCOMPARE(tempName("1234abcd-5678-90ab-cdef-1234567890ab", 3),
             QStringLiteral(".ctrm-tmp-1234abcd-3"));
}

void TestPlanner::newBatchIdFormat()
{
    const QString id = newBatchId();
    QCOMPARE(id.size(), 36);
    QVERIFY(!id.startsWith(u'{'));
    QVERIFY(!QUuid::fromString(id).isNull());
    QVERIFY(newBatchId() != id);
}

void TestPlanner::tempNameSkipsExisting()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a");
    fs.addFile("/f/b");
    fs.addFile("/f/" + tmp(1));
    const PlanResult r = makePlan({req("/f", "a", "b"), req("/f", "b", "a")}, fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QCOMPARE(describe(*r.plan), QStringList({"a>" + tmp(2), "b>a", tmp(2) + ">b"}));

    // A temporary name that is a new name in the batch is also skipped.
    FakeFileSystem fs2;
    fs2.addFolder("/f");
    fs2.addFile("/f/a");
    fs2.addFile("/f/b");
    fs2.addFile("/f/c");
    const PlanResult r2 = makePlan(
            {req("/f", "a", "b"), req("/f", "b", "a"), req("/f", "c", tmp(1))}, fs2, kBatch);
    QVERIFY2(r2.plan, qPrintable(r2.error));
    QVERIFY(describe(*r2.plan).contains("a>" + tmp(2)));
    QCOMPARE(runPlan(fs2, *r2.plan), QString());
}

void TestPlanner::tempCounterSharedAcrossFolders()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFolder("/g");
    for (const char *f : {"/f/a", "/f/b", "/g/a", "/g/b"})
        fs.addFile(f);
    const PlanResult r = makePlan(
            {req("/f", "a", "b"), req("/f", "b", "a"), req("/g", "a", "b"), req("/g", "b", "a")},
            fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QCOMPARE(describe(*r.plan, true),
             QStringList({"/f/a>/f/" + tmp(1), "/f/b>/f/a", "/f/" + tmp(1) + ">/f/b",
                          "/g/a>/g/" + tmp(2), "/g/b>/g/a", "/g/" + tmp(2) + ">/g/b"}));
}

void TestPlanner::targetTakenError()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a");
    fs.addFile("/f/c");
    const PlanResult r = makePlan({req("/f", "a", "c")}, fs, kBatch);
    QVERIFY(!r.plan);
    QVERIFY(!r.error.isEmpty());
}

void TestPlanner::targetTakenIgnoringCaseError()
{
    FakeFileSystem fs;
    fs.addFolder("/f", volume(CaseSensitivity::Insensitive));
    fs.addFile("/f/a");
    fs.addFile("/f/C");
    QVERIFY(!makePlan({req("/f", "a", "c")}, fs, kBatch).plan);
    // On a Sensitive volume the same names are free.
    FakeFileSystem sen;
    sen.addFolder("/f", volume(CaseSensitivity::Sensitive));
    sen.addFile("/f/a");
    sen.addFile("/f/C");
    QVERIFY(makePlan({req("/f", "a", "c")}, sen, kBatch).plan);
}

void TestPlanner::duplicateTargetError()
{
    FakeFileSystem fs;
    fs.addFolder("/f", volume(CaseSensitivity::Insensitive));
    fs.addFile("/f/a");
    fs.addFile("/f/b");
    QVERIFY(!makePlan({req("/f", "a", "x"), req("/f", "b", "x")}, fs, kBatch).plan);
    const PlanResult r = makePlan({req("/f", "a", "x"), req("/f", "b", "X")}, fs, kBatch);
    QVERIFY(!r.plan);
    QVERIFY(!r.error.isEmpty());
    // The same new name in two folders is not a conflict.
    fs.addFolder("/g", volume(CaseSensitivity::Insensitive));
    fs.addFile("/g/b");
    QVERIFY(makePlan({req("/f", "a", "x"), req("/g", "b", "x")}, fs, kBatch).plan);
}

void TestPlanner::sameNameError()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a");
    const PlanResult r = makePlan({req("/f", "a", "a")}, fs, kBatch);
    QVERIFY(!r.plan);
    QVERIFY(!r.error.isEmpty());
}

void TestPlanner::itemTwiceError()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a");
    QVERIFY(!makePlan({req("/f", "a", "b"), req("/f", "a", "c")}, fs, kBatch).plan);
}

void TestPlanner::unreadableFolderError()
{
    FakeFileSystem fs;
    const PlanResult r = makePlan({req("/missing", "a", "b")}, fs, kBatch);
    QVERIFY(!r.plan);
    QVERIFY(!r.error.isEmpty());
}

void TestPlanner::stepIndexesAndItems()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a");
    fs.addFile("/f/b");
    const PlanResult r = makePlan({req("/f", "a", "b", 7), req("/f", "b", "a", 8)}, fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QList<ItemId> items;
    for (int i = 0; i < r.plan->steps.size(); ++i) {
        QCOMPARE(r.plan->steps[i].index, i);
        items.append(r.plan->steps[i].item);
    }
    QCOMPARE(items, QList<ItemId>({7, 8, 7}));
}

void TestPlanner::mixedCyclesAndChain()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    for (const char *n : {"a", "b", "c", "d", "e", "x", "y"})
        fs.addFile(QStringLiteral("/f/") + n, n);
    // Two cycles (a<->b, c->d->e->c) and a chain (y->z, x->y).
    const PlanResult r = makePlan({req("/f", "a", "b"), req("/f", "c", "d"), req("/f", "x", "y"),
                                   req("/f", "b", "a"), req("/f", "d", "e"), req("/f", "e", "c"),
                                   req("/f", "y", "z")},
                                  fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QCOMPARE(r.plan->steps.size(), 9); // 7 requests + 2 cycle breaks
    QCOMPARE(runPlan(fs, *r.plan), QString());
    QCOMPARE(fs.content("/f/b"), QByteArray("a"));
    QCOMPARE(fs.content("/f/a"), QByteArray("b"));
    QCOMPARE(fs.content("/f/d"), QByteArray("c"));
    QCOMPARE(fs.content("/f/e"), QByteArray("d"));
    QCOMPARE(fs.content("/f/c"), QByteArray("e"));
    QCOMPARE(fs.content("/f/y"), QByteArray("x"));
    QCOMPARE(fs.content("/f/z"), QByteArray("y"));
    QVERIFY(!fs.exists("/f/x"));
}

void TestPlanner::folderCycleWithChild()
{
    FakeFileSystem fs;
    fs.addFolder("/r/p");
    fs.addFolder("/r/q");
    fs.addFile("/r/p/1", "P1");
    fs.addFile("/r/q/2", "Q2");
    const PlanResult r = makePlan(
            {req("/r", "p", "q"), req("/r", "q", "p"), req("/r/p", "1", "11")}, fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QCOMPARE(r.plan->steps.first().from, QStringLiteral("/r/p/1"));
    QCOMPARE(runPlan(fs, *r.plan), QString());
    QCOMPARE(fs.content("/r/q/11"), QByteArray("P1"));
    QCOMPARE(fs.content("/r/p/2"), QByteArray("Q2"));
}

void TestPlanner::sensitiveBothCasesAsOldNames()
{
    FakeFileSystem fs;
    fs.addFolder("/f", volume(CaseSensitivity::Sensitive));
    fs.addFile("/f/a", "lower");
    fs.addFile("/f/A", "upper");
    const PlanResult r = makePlan({req("/f", "a", "A"), req("/f", "A", "a")}, fs, kBatch);
    QVERIFY2(r.plan, qPrintable(r.error));
    QCOMPARE(r.plan->steps.size(), 3);
    QCOMPARE(runPlan(fs, *r.plan), QString());
    QCOMPARE(fs.content("/f/A"), QByteArray("lower"));
    QCOMPARE(fs.content("/f/a"), QByteArray("upper"));
}

void TestPlanner::applyPlanOnFake()
{
    QRandomGenerator rng(20261117);
    const QStringList names = {"a", "b", "c", "d", "e", "f"};
    for (int round = 0; round < 200; ++round) {
        FakeFileSystem fs;
        fs.addFolder("/f");
        for (const QString &n : names)
            fs.addFile("/f/" + n, n.toUtf8());
        // Some rounds also rename to names that are not in use.
        QStringList targets = names;
        if (round % 3 == 0)
            targets[int(rng.bounded(6))] = QStringLiteral("free");
        std::shuffle(targets.begin(), targets.end(), rng);

        QList<RenameRequest> requests;
        for (int i = 0; i < names.size(); ++i) {
            if (names[i] != targets[i])
                requests.append(req("/f", names[i], targets[i], ItemId(i + 1)));
        }
        if (requests.isEmpty())
            continue;
        const PlanResult r = makePlan(requests, fs, kBatch);
        QVERIFY2(r.plan, qPrintable(r.error));
        const QString failure = runPlan(fs, *r.plan);
        QVERIFY2(failure.isEmpty(),
                 qPrintable(QStringLiteral("round %1: %2").arg(round).arg(failure)));
        for (int i = 0; i < names.size(); ++i)
            QCOMPARE(fs.content("/f/" + targets[i]), names[i].toUtf8());
        QCOMPARE(fs.names("/f").size(), names.size());
    }
}

void TestPlanner::applyPlanOnFakeInsensitive()
{
    QRandomGenerator rng(4242);
    const QStringList names = {"a", "b", "c", "d", "e", "f"};
    for (int round = 0; round < 200; ++round) {
        FakeFileSystem fs;
        fs.addFolder("/f", volume(CaseSensitivity::Insensitive));
        for (const QString &n : names)
            fs.addFile("/f/" + n, n.toUtf8());
        QStringList targets = names;
        std::shuffle(targets.begin(), targets.end(), rng);
        for (QString &t : targets) {
            if (rng.bounded(2) == 1)
                t = t.toUpper();
        }

        QList<RenameRequest> requests;
        for (int i = 0; i < names.size(); ++i) {
            if (names[i] != targets[i])
                requests.append(req("/f", names[i], targets[i], ItemId(i + 1)));
        }
        if (requests.isEmpty())
            continue;
        const PlanResult r = makePlan(requests, fs, kBatch);
        QVERIFY2(r.plan, qPrintable(r.error));
        const QString failure = runPlan(fs, *r.plan);
        QVERIFY2(failure.isEmpty(),
                 qPrintable(QStringLiteral("round %1: %2").arg(round).arg(failure)));
        QStringList want = targets;
        want.sort();
        QCOMPARE(fs.names("/f"), want);
        for (int i = 0; i < names.size(); ++i)
            QCOMPARE(fs.content("/f/" + targets[i]), names[i].toUtf8());
    }
}

QTEST_GUILESS_MAIN(TestPlanner)
#include "tst_planner.moc"
