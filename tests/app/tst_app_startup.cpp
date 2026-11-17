// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"
#include "controllers/batchcontroller.h"
#include "controllers/historycontroller.h"
#include "controllers/itemadder.h"
#include "controllers/preferencescontroller.h"
#include "controllers/recoverycontroller.h"
#include "dialogs/recoverydialog.h"
#include "exec/historystore.h"
#include "mainwindow.h"
#include "models/rulelistmodel.h"
#include "rules/replacerule.h"
#include "settings.h"
#include "startup.h"
#include "temptree.h"

#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace ctrm;
using namespace ctrm::app;

namespace {

struct Fixture
{
    Fixture()
        : qsettings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat)
        , ctx(createNativeFileSystem(), dir.path() + QStringLiteral("/history"),
              dir.path() + QStringLiteral("/presets"), &qsettings)
        , window(ctx)
        , batch(window, ctx, nullptr)
        , history(window, ctx, batch, nullptr)
        , recovery(window, ctx, history, nullptr)
        , preferences(window, ctx, nullptr)
    {
        window.itemAdder().askFolderDrop = [](const FolderDropQuestion &) {
            return FolderDropReply{FolderDropReply::Choice::RenameFolder, false, false};
        };
        batch.showReport = [](const FinishResult &) {};
        recovery.askRecovery = [this](const RecoveryInfo &info) {
            asked.append(info.batchId);
            return answer;
        };
        recovery.showReport = [](const FinishResult &, const QDateTime &) {};
    }

    // One batch that renames "old" to "new" in a new file. Returns the batch ID.
    QString renameOne(const QString &rel)
    {
        window.ruleModel().setChain({});
        window.itemAdder().addPaths({tree.file(rel, "x")});
        auto rule = std::make_unique<ReplaceRule>();
        rule->find = QStringLiteral("old");
        rule->replace = QStringLiteral("new");
        window.ruleModel().insertRule(0, std::move(rule));
        QSignalSpy spy(&batch, &BatchController::batchFinished);
        window.actions().rename->trigger();
        if (spy.count() == 0 && !spy.wait(30'000))
            return {};
        window.actions().clearList->trigger();
        return batch.lastBatchId();
    }

    QString fileOf(const QString &id)
    {
        for (const BatchSummary &s : ctx.history().list()) {
            if (s.batchId == id)
                return s.filePath;
        }
        return {};
    }

    // Changes the last state line of the journal: removes it (in-progress, as if CTRM stopped),
    // or writes another state.
    void setLastState(const QString &id, const QByteArray &state)
    {
        QFile f(fileOf(id));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QList<QByteArray> lines = f.readAll().split('\n');
        f.close();
        while (!lines.isEmpty() && lines.last().isEmpty())
            lines.removeLast();
        QVERIFY(lines.last().contains("\"type\":\"state\""));
        QVERIFY(lines.last().contains("\"complete\""));
        if (state.isEmpty())
            lines.removeLast();
        else
            lines.last().replace("\"complete\"", "\"" + state + "\"");
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(lines.join('\n') + '\n');
    }

    QString writeDamaged()
    {
        const QString path =
                ctx.historyDir() + QStringLiteral("/20261101T100000Z-0123456789abcdef.jsonl");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write("not json\n{\"type\":\n");
        return path;
    }

    QTemporaryDir dir;
    TempTree tree;
    QSettings qsettings;
    AppContext ctx;
    MainWindow window;
    BatchController batch;
    HistoryController history;
    RecoveryController recovery;
    PreferencesController preferences;

    RecoveryChoice answer = RecoveryChoice::Close;
    QStringList asked;
};

// Writes a lock file as QLockFile does, for a process ID.
void writeLockFile(const QString &path, qint64 pid)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray::number(pid) + '\n' + QCoreApplication::applicationName().toUtf8() + '\n' +
            QSysInfo::machineHostName().toUtf8() + '\n');
}

} // namespace

class TestAppStartup : public QObject
{
    Q_OBJECT
private slots:
    void lockOnce();
    void lockOfOpenCtrmStays();
    void staleLockTaken();
    void alreadyOpenText();
    void itemArguments();
    void retentionAfterRecovery();
    void retentionAfterDecision();
};

void TestAppStartup::lockOnce()
{
    QTemporaryDir dir;
    auto first = std::make_unique<InstanceLock>(dir.path());
    InstanceLock second(dir.path());
    QVERIFY(first->tryLock());
    QVERIFY(first->tryLock()); // the holder asks again
    QVERIFY(!second.tryLock());
    first.reset(); // the first CTRM closes
    QVERIFY(second.tryLock());
}

void TestAppStartup::lockOfOpenCtrmStays()
{
    // A CTRM that is open for a long time keeps its lock: no stale time. On macOS and Linux the
    // native file lock of the holder also stops the removal, so this test passes with the
    // default stale time too; setStaleLockTime(0) is for file systems with no native lock.
    QTemporaryDir dir;
    InstanceLock first(dir.path());
    QVERIFY(first.tryLock());
    QFile file(first.filePath());
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-3600),
                             QFileDevice::FileModificationTime));
    file.close();
    InstanceLock second(dir.path());
    QVERIFY(!second.tryLock());
}

void TestAppStartup::staleLockTaken()
{
    // CTRM stopped and left its lock file: the next start takes the lock.
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("-functions")});
    QVERIFY(process.waitForStarted());
    const qint64 pid = process.processId();
    QVERIFY(process.waitForFinished());
    QTemporaryDir dir;
    InstanceLock lock(dir.path());
    writeLockFile(lock.filePath(), pid);
    QVERIFY(lock.tryLock());
}

void TestAppStartup::alreadyOpenText()
{
    QCOMPARE(ctrm::app::alreadyOpenText(), QStringLiteral("CTRM is already open."));
    const QString why = QStringLiteral("Use the CTRM window that is open. Two open copies could "
                                       "rename the same files at the same time.");
    QCOMPARE(ctrm::app::alreadyOpenInformativeText(0), why);
    QCOMPARE(ctrm::app::alreadyOpenInformativeText(1),
             why + QStringLiteral("\n\nThe item was not added. Add it in the open CTRM window."));
    QCOMPARE(ctrm::app::alreadyOpenInformativeText(3),
             why + QStringLiteral(
                           "\n\nThe items were not added. Add them in the open CTRM window."));
}

void TestAppStartup::itemArguments()
{
    // Open With gives the items as arguments; options are not items (Qt removes its own).
    QCOMPARE(ctrm::app::itemArgumentCount({QStringLiteral("ctrm")}), 0);
    QCOMPARE(ctrm::app::itemArgumentCount({QStringLiteral("ctrm"), QStringLiteral("/a/b.txt"),
                                           QStringLiteral("-x"), QStringLiteral("/a/c")}),
             2);
}

void TestAppStartup::retentionAfterRecovery()
{
    Fixture f;
    const QString complete = f.renameOne(QStringLiteral("a_old.txt"));
    const QString open = f.renameOne(QStringLiteral("b_old.txt"));
    const QString incomplete = f.renameOne(QStringLiteral("c_old.txt"));
    QVERIFY(!complete.isEmpty() && !open.isEmpty() && !incomplete.isEmpty());
    f.setLastState(open, {});
    f.setLastState(incomplete, "incomplete");
    const QString damaged = f.writeDamaged();
    QVERIFY(!damaged.isEmpty());
    const QString completeFile = f.fileOf(complete);
    const QString openFile = f.fileOf(open);
    const QString incompleteFile = f.fileOf(incomplete);
    // Keep 1 batch (the newest: incomplete) and 1 day.
    f.ctx.settings().setRetentionCount(1);
    f.ctx.settings().setRetentionDays(1);

    f.answer = RecoveryChoice::Close; // no decision: the batch stays open
    runStartChecks(f.recovery, f.preferences, QDateTime::currentDateTimeUtc().addDays(30));
    QVERIFY(f.asked.contains(open));
    QVERIFY(!QFile::exists(completeFile));
    QVERIFY(QFile::exists(openFile));
    QVERIFY(QFile::exists(incompleteFile));
    QVERIFY(QFile::exists(damaged));
}

void TestAppStartup::retentionAfterDecision()
{
    // The user decides first: a batch marked complete in the recovery prompt is then an old
    // complete batch, and retention deletes it in the same start.
    Fixture f;
    const QString open = f.renameOne(QStringLiteral("a_old.txt"));
    const QString newest = f.renameOne(QStringLiteral("b_old.txt"));
    QVERIFY(!open.isEmpty() && !newest.isEmpty());
    f.setLastState(open, {});
    const QString openFile = f.fileOf(open);
    f.ctx.settings().setRetentionCount(1); // keeps only the newest batch
    f.ctx.settings().setRetentionDays(1);

    f.answer = RecoveryChoice::MarkComplete;
    runStartChecks(f.recovery, f.preferences, QDateTime::currentDateTimeUtc().addDays(30));
    QCOMPARE(f.asked, QStringList{open});
    QVERIFY(!QFile::exists(openFile));
    QVERIFY(!f.fileOf(newest).isEmpty());
}

QTEST_MAIN(TestAppStartup)
#include "tst_app_startup.moc"
