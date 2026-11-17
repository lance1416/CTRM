// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"
#include "controllers/batchcontroller.h"
#include "controllers/itemadder.h"
#include "exec/batchservice.h"
#include "exec/historystore.h"
#include "fs/filesystem.h"
#include "mainwindow.h"
#include "models/itemtablemodel.h"
#include "models/rulelistmodel.h"
#include "preview/previewscheduler.h"
#include "rules/insertrule.h"
#include "rules/replacerule.h"
#include "temptree.h"
#include "widgets/messagebar.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSemaphore>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <atomic>
#include <memory>
#include <optional>

using namespace ctrm;
using namespace ctrm::app;

namespace {

// A main window and a batch controller on the native file system, with temporary folders.
// The dialogs are replaced: they record the question and give the set answer.
struct Fixture
{
    Fixture()
        : qsettings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat)
        , ctx(createNativeFileSystem(), dir.filePath(QStringLiteral("history")),
              dir.filePath(QStringLiteral("presets")), &qsettings)
        , window(ctx)
        , batch(window, ctx, nullptr)
    {
        window.itemAdder().askFolderDrop = [](const FolderDropQuestion &) {
            return FolderDropReply{FolderDropReply::Choice::RenameFolder, false, false};
        };
        batch.confirmSkip = [this](const SkipQuestion &q) {
            skipQuestion = q;
            return skipAnswer;
        };
        batch.askFailure = [this](const FailureInfo &info) {
            failure = info;
            return failureAnswer;
        };
        batch.showReport = [this](const FinishResult &r) { report = r; };
    }

    QString file(const QString &rel) { return tree.file(rel, "x"); }

    void add(const QStringList &rels)
    {
        QStringList paths;
        for (const QString &rel : rels)
            paths.append(tree.root() + u'/' + rel);
        window.itemAdder().addPaths(paths);
    }

    void replace(const QString &find, const QString &with, bool regex = false)
    {
        auto rule = std::make_unique<ReplaceRule>();
        rule->find = find;
        rule->replace = with;
        rule->mode = regex ? ReplaceRule::Mode::Regex : ReplaceRule::Mode::Literal;
        window.ruleModel().insertRule(window.ruleModel().rowCount(), std::move(rule));
    }

    bool exists(const QString &rel) const { return QFile::exists(tree.root() + u'/' + rel); }

    // Runs the action and waits for the end of the batch.
    bool waitForBatch(int timeout = 30'000)
    {
        QSignalSpy spy(&batch, &BatchController::batchFinished);
        return spy.wait(timeout) || spy.count() > 0;
    }

    QString message(int row) const
    {
        const ItemTableModel &m = const_cast<Fixture *>(this)->window.itemModel();
        return m.data(m.index(row, ItemTableModel::Message), ItemTableModel::MessageRole)
                .toString();
    }

    QString resultText() const
    {
        auto *bar = const_cast<Fixture *>(this)->window.messageBar();
        auto *label = bar->findChild<QLabel *>(QStringLiteral("messageBarText"));
        return bar->isVisibleTo(bar->window()) && label ? label->text() : QString();
    }

    QTemporaryDir dir;
    TempTree tree;
    QSettings qsettings;
    AppContext ctx;
    MainWindow window;
    BatchController batch;

    bool skipAnswer = true;
    std::optional<SkipQuestion> skipQuestion;
    FailureChoice failureAnswer = FailureChoice::RollBack;
    std::optional<FailureInfo> failure;
    std::optional<FinishResult> report;
};

// Makes the folder read-only for the test and writable again at the end.
struct ReadOnlyFolder
{
    explicit ReadOnlyFolder(QString path)
        : m_path(std::move(path))
    {
        QFile::setPermissions(m_path, QFile::ReadOwner | QFile::ExeOwner);
    }
    ~ReadOnlyFolder()
    {
        QFile::setPermissions(m_path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    QString m_path;
};

} // namespace

class TestBatch : public QObject
{
    Q_OBJECT
private slots:
    void renameNoConfirm();
    void skipAsksWithCount();
    void skipRenamesOthers();
    void skipShowsCascade();
    void renameWaitsForCurrentPreview();
    void previewChangedStops();
    void failureRollBack();
    void failureKeep();
    void cancelShowsDialog();
    void folderRenameUpdatesChildRows();
    void closeDuringRunCancelsFirst();
    void runOffUiThread();
};

void TestBatch::renameNoConfirm()
{
    Fixture f;
    f.window.show();
    f.file(QStringLiteral("old_a.txt"));
    f.file(QStringLiteral("old_b.txt"));
    f.add({QStringLiteral("old_a.txt"), QStringLiteral("old_b.txt")});
    f.replace(QStringLiteral("old"), QStringLiteral("new"));
    QVERIFY(f.window.actions().rename->isEnabled());

    bool modalSeen = false;
    QTimer watch;
    QObject::connect(&watch, &QTimer::timeout,
                     [&] { modalSeen |= QApplication::activeModalWidget() != nullptr; });
    watch.start(1);
    QSignalSpy spy(&f.batch, &BatchController::batchFinished);
    f.window.actions().rename->trigger();
    QVERIFY(spy.wait(30'000) || spy.count() > 0);
    QCoreApplication::processEvents();
    QVERIFY(!modalSeen);
    QVERIFY(!f.failure);
    QVERIFY(!f.report);

    QVERIFY(f.exists(QStringLiteral("new_a.txt")));
    QVERIFY(f.exists(QStringLiteral("new_b.txt")));
    QVERIFY(!f.exists(QStringLiteral("old_a.txt")));
    QCOMPARE(f.resultText(), QStringLiteral("Renamed 2 items."));
    QCOMPARE(f.message(0), QStringLiteral("Renamed"));
    QVERIFY(!f.batch.isRunning());
    QVERIFY(!f.batch.lastBatchId().isEmpty());
    QCOMPARE(f.batch.lastBatchListVersion(), f.window.itemModel().listVersion());
    const auto list = f.ctx.history().list();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().state, BatchState::Complete);
}

void TestBatch::skipAsksWithCount()
{
    Fixture f;
    for (const char *name : {"a1.txt", "a2.txt", "7", "b5.txt", "c6.txt"})
        f.file(QLatin1StringView(name));
    f.add({QStringLiteral("a1.txt"), QStringLiteral("a2.txt"), QStringLiteral("7"),
           QStringLiteral("b5.txt"), QStringLiteral("c6.txt")});
    f.replace(QStringLiteral("\\d"), QString(), true);
    QCOMPARE(f.window.currentPreview().summary.conflicts, 2);
    QCOMPARE(f.window.currentPreview().summary.errors, 1);

    f.skipAnswer = false;
    f.window.actions().skipAndRename->trigger();
    QTRY_VERIFY(f.skipQuestion.has_value());
    QCOMPARE(f.skipQuestion->skipCount, 3);
    QCOMPARE(f.skipQuestion->renameCount, 2);
    QCOMPARE(f.skipQuestion->conflicts, 2);
    QCOMPARE(f.skipQuestion->invalid, 1);
    QCOMPARE(f.skipQuestion->other, 0);
    QCOMPARE(f.skipQuestion->cascadeCount, 0);
    QTest::qWait(50);
    QVERIFY(!f.batch.isRunning());
    QVERIFY(f.exists(QStringLiteral("b5.txt")));
    QVERIFY(f.ctx.history().list().isEmpty());
}

void TestBatch::skipRenamesOthers()
{
    Fixture f;
    for (const char *name : {"a1.txt", "a2.txt", "7", "b5.txt", "c6.txt"})
        f.file(QLatin1StringView(name));
    f.add({QStringLiteral("a1.txt"), QStringLiteral("a2.txt"), QStringLiteral("7"),
           QStringLiteral("b5.txt"), QStringLiteral("c6.txt")});
    f.replace(QStringLiteral("\\d"), QString(), true);

    f.window.actions().skipAndRename->trigger();
    QVERIFY(f.waitForBatch());
    QVERIFY(f.exists(QStringLiteral("b.txt")));
    QVERIFY(f.exists(QStringLiteral("c.txt")));
    QVERIFY(f.exists(QStringLiteral("a1.txt")));
    QVERIFY(f.exists(QStringLiteral("a2.txt")));
    QVERIFY(f.exists(QStringLiteral("7")));
    QCOMPARE(f.message(0), QStringLiteral("Skipped: conflict"));
    QCOMPARE(f.message(1), QStringLiteral("Skipped: conflict"));
    QCOMPARE(f.message(2), QStringLiteral("Skipped: invalid name"));
    QCOMPARE(f.message(3), QStringLiteral("Renamed"));
    QCOMPARE(f.resultText(), QStringLiteral("Renamed 2 items."));
}

void TestBatch::skipShowsCascade()
{
    // "1" becomes "" (invalid). "b" becomes "1": the old name of the skipped item. "old_c" changes,
    // so Skip is available.
    Fixture f;
    f.file(QStringLiteral("1"));
    f.file(QStringLiteral("b"));
    f.file(QStringLiteral("old_c"));
    f.add({QStringLiteral("1"), QStringLiteral("b"), QStringLiteral("old_c")});
    f.replace(QStringLiteral("^1$"), QString(), true);
    f.replace(QStringLiteral("^b$"), QStringLiteral("1"), true);
    f.replace(QStringLiteral("old"), QStringLiteral("new"));
    f.skipAnswer = false;
    f.window.actions().skipAndRename->trigger();
    QTRY_VERIFY(f.skipQuestion.has_value());
    QCOMPARE(f.skipQuestion->skipCount, 2);
    QCOMPARE(f.skipQuestion->renameCount, 1);
    QCOMPARE(f.skipQuestion->cascadeCount, 1);
    QCOMPARE(f.skipQuestion->invalid, 1);
    QCOMPARE(f.skipQuestion->conflicts, 1);
}

void TestBatch::renameWaitsForCurrentPreview()
{
    // More than kWorkerThreshold items: the preview runs on the worker thread.
    Fixture f;
    QStringList names;
    for (int i = 0; i <= PreviewScheduler::kWorkerThreshold; ++i) {
        names.append(QStringLiteral("f%1.txt").arg(i));
        f.file(names.last());
    }
    f.add(names);
    auto rule = std::make_unique<InsertRule>();
    rule->text = QStringLiteral("x_");
    f.window.ruleModel().insertRule(0, std::move(rule));
    QTRY_VERIFY_WITH_TIMEOUT(f.window.previewIsCurrent(), 30'000);

    static_cast<InsertRule *>(f.window.ruleModel().chain().at(0))->text = QStringLiteral("y_");
    f.window.ruleModel().ruleChanged(0);
    QVERIFY(!f.window.previewIsCurrent());
    f.batch.renameNow();
    QVERIFY(f.waitForBatch(120'000));
    QVERIFY(f.exists(QStringLiteral("y_f0.txt")));
    QVERIFY(!f.exists(QStringLiteral("x_f0.txt")));
    QVERIFY(!f.exists(QStringLiteral("f0.txt")));
}

void TestBatch::previewChangedStops()
{
    Fixture f;
    f.file(QStringLiteral("old_a.txt"));
    const QString b = f.file(QStringLiteral("old_b.txt"));
    f.add({QStringLiteral("old_a.txt"), QStringLiteral("old_b.txt")});
    f.replace(QStringLiteral("old"), QStringLiteral("new"));
    QVERIFY(QFile::remove(b));

    f.batch.renameNow();
    QTRY_COMPARE(f.window.statusBar()->currentMessage(),
                 QStringLiteral("The files changed on disk. Check the new preview."));
    QVERIFY(!f.batch.isRunning());
    QVERIFY(f.exists(QStringLiteral("old_a.txt")));
    QVERIFY(f.ctx.history().list().isEmpty());
    QTRY_VERIFY(f.window.previewIsCurrent());
    QCOMPARE(f.window.currentPreview().items.at(1).status, Status::Missing);
}

void TestBatch::failureRollBack()
{
    Fixture f;
    // Folders of the same depth run in name order: "a" first, then "b" fails.
    f.file(QStringLiteral("a/old_a.txt"));
    f.file(QStringLiteral("b/old_b.txt"));
    f.add({QStringLiteral("a/old_a.txt"), QStringLiteral("b/old_b.txt")});
    f.replace(QStringLiteral("old"), QStringLiteral("new"));
    ReadOnlyFolder ro(f.tree.root() + QStringLiteral("/b"));

    f.failureAnswer = FailureChoice::RollBack;
    f.window.actions().rename->trigger();
    QVERIFY(f.waitForBatch());
    QVERIFY(f.failure.has_value());
    QVERIFY(!f.failure->cancelled);
    QCOMPARE(f.failure->done, 1);
    QCOMPARE(f.failure->total, 2);
    QCOMPARE(f.failure->failed.size(), 1);
    QVERIFY(!f.failure->failed.first().errorText.isEmpty());

    QVERIFY(f.exists(QStringLiteral("a/old_a.txt")));
    QVERIFY(f.exists(QStringLiteral("b/old_b.txt")));
    QVERIFY(!f.exists(QStringLiteral("a/new_a.txt")));
    QVERIFY(f.report.has_value());
    QCOMPARE(f.report->state, BatchState::RolledBack);
    bool failedRow = false;
    for (const ReportRow &row : f.report->report)
        failedRow |= row.result == ReportRow::Result::Failed && !row.errorText.isEmpty();
    QVERIFY(failedRow);
    QCOMPARE(f.message(0), QStringLiteral("Rolled back to the old name"));
    QVERIFY(f.message(1).startsWith(QStringLiteral("Failed: ")));
}

void TestBatch::failureKeep()
{
    Fixture f;
    // Folders of the same depth run in name order: "a" first, then "b" fails.
    f.file(QStringLiteral("a/old_a.txt"));
    f.file(QStringLiteral("b/old_b.txt"));
    f.add({QStringLiteral("a/old_a.txt"), QStringLiteral("b/old_b.txt")});
    f.replace(QStringLiteral("old"), QStringLiteral("new"));
    ReadOnlyFolder ro(f.tree.root() + QStringLiteral("/b"));

    f.failureAnswer = FailureChoice::Keep;
    f.window.actions().rename->trigger();
    QVERIFY(f.waitForBatch());
    QVERIFY(f.failure.has_value());
    QVERIFY(f.exists(QStringLiteral("a/new_a.txt")));
    QVERIFY(f.exists(QStringLiteral("b/old_b.txt")));
    const auto list = f.ctx.history().list();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().state, BatchState::Complete);
    QVERIFY(f.report.has_value());
    QCOMPARE(f.message(0), QStringLiteral("Renamed"));
}

void TestBatch::cancelShowsDialog()
{
    Fixture f;
    QStringList names;
    for (int i = 0; i < 5; ++i) {
        names.append(QStringLiteral("old_%1.txt").arg(i));
        f.file(names.last());
    }
    f.add(names);
    f.replace(QStringLiteral("old"), QStringLiteral("new"));
    f.batch.onWorkerProgress = [&](int done, int) {
        if (done == 1)
            f.batch.cancel();
    };
    f.failureAnswer = FailureChoice::RollBack;
    f.window.actions().rename->trigger();
    QVERIFY(f.waitForBatch());
    QVERIFY(f.failure.has_value());
    QVERIFY(f.failure->cancelled);
    QCOMPARE(f.failure->done, 1);
    QCOMPARE(f.failure->total, 5);
    QVERIFY(f.failure->failed.isEmpty());
    QVERIFY(f.exists(QStringLiteral("old_0.txt")));
    QCOMPARE(f.report->state, BatchState::RolledBack);
}

void TestBatch::folderRenameUpdatesChildRows()
{
    Fixture f;
    f.file(QStringLiteral("old_dir/old_c.txt"));
    f.add({QStringLiteral("old_dir"), QStringLiteral("old_dir/old_c.txt")});
    QCOMPARE(f.window.itemModel().items().size(), 2);
    f.replace(QStringLiteral("old"), QStringLiteral("new"));
    f.window.actions().rename->trigger();
    QVERIFY(f.waitForBatch());
    QVERIFY(f.exists(QStringLiteral("new_dir/new_c.txt")));

    const QList<Item> items = f.window.itemModel().items();
    QCOMPARE(items.at(0).name, QStringLiteral("new_dir"));
    QCOMPARE(items.at(1).folder, f.tree.root() + QStringLiteral("/new_dir"));
    QCOMPARE(items.at(1).name, QStringLiteral("new_c.txt"));
    QTRY_VERIFY(f.window.previewIsCurrent());
    QCOMPARE(f.window.currentPreview().items.at(0).status, Status::Unchanged);
    QCOMPARE(f.window.currentPreview().items.at(1).status, Status::Unchanged);
}

void TestBatch::closeDuringRunCancelsFirst()
{
    Fixture f;
    f.window.show();
    QStringList names;
    for (int i = 0; i < 4; ++i) {
        names.append(QStringLiteral("old_%1.txt").arg(i));
        f.file(names.last());
    }
    f.add(names);
    f.replace(QStringLiteral("old"), QStringLiteral("new"));

    // The worker stops after the first step until the close event was sent.
    QSemaphore resume;
    std::atomic<bool> paused = false;
    f.batch.onWorkerProgress = [&](int done, int) {
        if (done == 1) {
            paused = true;
            resume.acquire();
        }
    };
    f.window.actions().rename->trigger();
    QTRY_VERIFY(paused.load());
    QVERIFY(f.batch.isRunning());

    QCloseEvent close;
    QCoreApplication::sendEvent(&f.window, &close);
    QVERIFY(!close.isAccepted());
    QVERIFY(f.window.isVisible());
    resume.release();

    QVERIFY(f.waitForBatch());
    QVERIFY(f.failure.has_value());
    QVERIFY(f.failure->cancelled);
    QTRY_VERIFY(!f.window.isVisible());
    const auto list = f.ctx.history().list();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().state, BatchState::RolledBack);
}

void TestBatch::runOffUiThread()
{
    Fixture f;
    f.file(QStringLiteral("old_a.txt"));
    f.add({QStringLiteral("old_a.txt")});
    f.replace(QStringLiteral("old"), QStringLiteral("new"));
    std::atomic<QThread *> thread = nullptr;
    f.batch.onWorkerProgress = [&](int, int) { thread = QThread::currentThread(); };
    f.window.actions().rename->trigger();
    QVERIFY(f.waitForBatch());
    QVERIFY(thread.load() != nullptr);
    QVERIFY(thread.load() != QCoreApplication::instance()->thread());
}

QTEST_MAIN(TestBatch)
#include "tst_app_batch.moc"
