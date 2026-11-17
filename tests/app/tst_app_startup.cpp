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
#include <QSettings>
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

} // namespace

class TestAppStartup : public QObject
{
    Q_OBJECT
private slots:
    void retentionAfterRecovery();
    void retentionAfterDecision();
};

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
