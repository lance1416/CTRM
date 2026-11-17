// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"
#include "controllers/batchcontroller.h"
#include "controllers/historycontroller.h"
#include "controllers/itemadder.h"
#include "controllers/recoverycontroller.h"
#include "dialogs/historywindow.h"
#include "dialogs/recoverydialog.h"
#include "exec/batchservice.h"
#include "exec/historystore.h"
#include "exec/undo.h"
#include "mainwindow.h"
#include "models/historymodel.h"
#include "models/rulelistmodel.h"
#include "rules/replacerule.h"
#include "temptree.h"
#include "widgets/tertiarybutton.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QtTest>

#include <atomic>
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
    {
        window.itemAdder().askFolderDrop = [](const FolderDropQuestion &) {
            return FolderDropReply{FolderDropReply::Choice::RenameFolder, false, false};
        };
        batch.showReport = [](const FinishResult &) {};
        recovery.askRecovery = [this](const RecoveryInfo &info) {
            asked.append(info);
            return answer;
        };
        recovery.showReport = [this](const FinishResult &result, const QDateTime &) {
            reports.append(result);
        };
    }

    QString root() const { return tree.root(); }
    bool exists(const QString &rel) const { return QFile::exists(root() + u'/' + rel); }

    // Renames "old" to "new" in the files (one batch). Returns the batch ID.
    QString renameOld(const QStringList &rels)
    {
        QStringList paths;
        for (const QString &rel : rels)
            paths.append(tree.file(rel, "x"));
        window.itemAdder().addPaths(paths);
        auto rule = std::make_unique<ReplaceRule>();
        rule->find = QStringLiteral("old");
        rule->replace = QStringLiteral("new");
        window.ruleModel().insertRule(0, std::move(rule));
        QSignalSpy spy(&batch, &BatchController::batchFinished);
        window.actions().rename->trigger();
        if (spy.count() == 0 && !spy.wait(30'000))
            return {};
        return batch.lastBatchId();
    }

    BatchSummary summary(const QString &id)
    {
        for (const BatchSummary &s : ctx.history().list())
            if (s.batchId == id)
                return s;
        return {};
    }

    // As if CTRM stopped after the batch: removes the "complete" state line. With
    // openLastStep, also the result line of the last step (its start line stays).
    void crash(const QString &id, bool openLastStep = false)
    {
        QFile f(summary(id).filePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QList<QByteArray> lines = f.readAll().split('\n');
        f.close();
        while (!lines.isEmpty() && lines.last().isEmpty())
            lines.removeLast();
        QVERIFY(lines.last().contains("\"type\":\"state\""));
        lines.removeLast();
        if (openLastStep) {
            QVERIFY(lines.last().contains("\"type\":\"op\""));
            lines.removeLast();
        }
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(lines.join('\n') + '\n');
    }

    QString writeDamaged(const QString &id)
    {
        const QString path = ctx.historyDir() + QStringLiteral("/20261101T100000Z-") + id +
                             QStringLiteral(".jsonl");
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

    RecoveryChoice answer = RecoveryChoice::Close;
    QList<RecoveryInfo> asked;
    QList<FinishResult> reports;
};

QList<OpenStep> twoOpenSteps()
{
    OpenStep forward;
    forward.step = 3;
    forward.from = QStringLiteral("/data/Trip/IMG_0412.JPG");
    forward.to = QStringLiteral("/data/Trip/.ctrm-tmp-7f3a-2");
    forward.toIsTemp = true;
    OpenStep back;
    back.step = 1;
    back.phase = Phase::Rollback;
    back.from = QStringLiteral("/data/Trip/.ctrm-tmp-7f3a-1");
    back.to = QStringLiteral("/data/Trip/IMG_0411.JPG");
    back.fromIsTemp = true;
    return {forward, back};
}

RecoveryInfo infoWith(QList<OpenStep> steps, bool damaged = false)
{
    RecoveryInfo info;
    info.batchId = QStringLiteral("b1");
    info.started = QDateTime(QDate(2026, 11, 17), QTime(14, 2));
    info.done = 412;
    info.total = 1204;
    info.doneItems = 412;
    info.damaged = damaged;
    info.folders = {QStringLiteral("/data/Trip")};
    info.openSteps = std::move(steps);
    return info;
}

QString dayText(const QDateTime &time)
{
    return QLocale::system().toString(time.date(), QStringLiteral("d MMM yyyy"));
}

} // namespace

class TestAppRecovery : public QObject
{
    Q_OBJECT
private slots:
    void noPromptWhenClean();
    void rollBackChoice();
    void markCompleteChoice();
    void closeDoesNothing();
    void showDetailsOpensHistory();
    void openStepsShown();
    void openStepsCopyList();
    void openStepsMarkNotDefault();
    void damagedNoRollBack();
    void normalTexts();
    void quitDuringBatchAskedAtNextStart();
    void headingNextToIcon();
    void noGapAboveButtons();
};

void TestAppRecovery::noPromptWhenClean()
{
    Fixture f;
    const QString id = f.renameOld({QStringLiteral("old_a.txt")});
    QVERIFY(!id.isEmpty());
    f.recovery.checkAtStart();
    QCOMPARE(f.asked.size(), 0);
}

void TestAppRecovery::rollBackChoice()
{
    Fixture f;
    const QString id = f.renameOld({QStringLiteral("old_a.txt"), QStringLiteral("old_b.txt")});
    QVERIFY(!id.isEmpty());
    f.crash(id);
    f.answer = RecoveryChoice::RollBack;
    f.recovery.checkAtStart();
    QCOMPARE(f.asked.size(), 1);
    QCOMPARE(f.asked[0].batchId, id);
    QCOMPARE(f.asked[0].done, 2);
    QCOMPARE(f.asked[0].total, 2);
    QCOMPARE(f.asked[0].doneItems, 2);
    QVERIFY(!f.asked[0].damaged);
    QVERIFY(f.exists(QStringLiteral("old_a.txt")));
    QVERIFY(f.exists(QStringLiteral("old_b.txt")));
    QVERIFY(!f.exists(QStringLiteral("new_a.txt")));
    QCOMPARE(f.summary(id).state, BatchState::RolledBack);
    QCOMPARE(f.reports.size(), 1);
}

void TestAppRecovery::markCompleteChoice()
{
    Fixture f;
    const QString id = f.renameOld({QStringLiteral("old_a.txt"), QStringLiteral("old_b.txt")});
    QVERIFY(!id.isEmpty());
    f.crash(id, true);
    f.answer = RecoveryChoice::MarkComplete;
    f.recovery.checkAtStart();
    QCOMPARE(f.asked.size(), 1);
    QCOMPARE(f.asked[0].done, 1);
    QCOMPARE(f.asked[0].openSteps.size(), 1);
    QCOMPARE(f.summary(id).state, BatchState::Complete);

    f.history.showHistory(id);
    auto *w = f.window.findChild<HistoryWindow *>();
    QVERIFY(w);
    const int row = w->model()->rowOf(id);
    QVERIFY(w->model()->index(row, HistoryModel::State).data().toString().startsWith(u"Complete"));

    QString error;
    const UndoPreview preview = f.ctx.batchService().previewUndo(id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.items.size(), 1);

    f.recovery.checkAtStart();
    QCOMPARE(f.asked.size(), 1);
}

void TestAppRecovery::closeDoesNothing()
{
    Fixture f;
    const QString id = f.renameOld({QStringLiteral("old_a.txt")});
    QVERIFY(!id.isEmpty());
    f.crash(id);
    f.answer = RecoveryChoice::Close;
    f.recovery.checkAtStart();
    QCOMPARE(f.asked.size(), 1);
    QVERIFY(f.exists(QStringLiteral("new_a.txt")));
    QCOMPARE(f.summary(id).state, BatchState::InProgress);
    QCOMPARE(f.reports.size(), 0);
    f.recovery.checkAtStart();
    QCOMPARE(f.asked.size(), 2);
}

void TestAppRecovery::showDetailsOpensHistory()
{
    Fixture f;
    const QString id = f.renameOld({QStringLiteral("old_a.txt")});
    QVERIFY(!id.isEmpty());
    f.crash(id);
    f.answer = RecoveryChoice::ShowDetails;
    f.recovery.checkAtStart();
    auto *w = f.window.findChild<HistoryWindow *>();
    QVERIFY(w);
    QVERIFY(w->isVisible());
    QCOMPARE(w->selectedBatchId(), id);
    QCOMPARE(f.summary(id).state, BatchState::InProgress);
}

void TestAppRecovery::openStepsShown()
{
    RecoveryDialog dialog(infoWith(twoOpenSteps()), nullptr);
    auto *label = dialog.findChild<QLabel *>(QStringLiteral("openStepsLabel"));
    QVERIFY(label);
    QCOMPARE(label->text(), QStringLiteral("Check these items (2 renames not confirmed):"));
    auto *note = dialog.findChild<QLabel *>(QStringLiteral("openStepsNote"));
    QVERIFY(note);
    QVERIFY(note->text().startsWith(u"CTRM started these renames"));
    auto *table = dialog.findChild<QTableView *>(QStringLiteral("openStepsTable"));
    QVERIFY(table);
    const QAbstractItemModel *m = table->model();
    QCOMPARE(m->rowCount(), 2);
    QCOMPARE(m->columnCount(), 3);
    // Journal order; the roll back row in disk direction (from the temporary name).
    QCOMPARE(m->index(0, 1).data().toString(), QStringLiteral("IMG_0412.JPG"));
    QVERIFY(m->index(0, 2).data().toString().contains(u".ctrm-tmp-7f3a-2"));
    QVERIFY(m->index(0, 2).data().toString().contains(u"temporary name"));
    QVERIFY(!m->index(0, 1).data().toString().contains(u"temporary name"));
    QVERIFY(m->index(1, 1).data().toString().contains(u".ctrm-tmp-7f3a-1"));
    QVERIFY(m->index(1, 1).data().toString().contains(u"temporary name"));
    QCOMPARE(m->index(1, 2).data().toString(), QStringLiteral("IMG_0411.JPG"));
    QVERIFY(!table->showGrid()); // G1
    QCOMPARE(m->index(0, 0).data(Qt::AccessibleTextRole).toString(),
             QStringLiteral("/data/Trip/IMG_0412.JPG to temporary name .ctrm-tmp-7f3a-2"));
    QVERIFY(m->index(0, 1).data(Qt::ToolTipRole).toString().contains(u"/data/Trip/IMG_0412.JPG"));

    RecoveryDialog one(infoWith({twoOpenSteps().first()}), nullptr);
    QCOMPARE(one.findChild<QLabel *>(QStringLiteral("openStepsLabel"))->text(),
             QStringLiteral("Check this item (1 rename not confirmed):"));
    QVERIFY(one.findChild<QLabel *>(QStringLiteral("openStepsNote"))
                    ->text()
                    .startsWith(u"CTRM started this rename, but did not confirm it."));

    RecoveryDialog none(infoWith({}), nullptr);
    QVERIFY(!none.findChild<QTableView *>(QStringLiteral("openStepsTable")));
}

void TestAppRecovery::openStepsCopyList()
{
    RecoveryDialog dialog(infoWith(twoOpenSteps()), nullptr);
    auto *copy = dialog.findChild<QToolButton *>(QStringLiteral("copyList"));
    QVERIFY(copy);
    QCOMPARE(copy->toolTip(), QStringLiteral("Copy List"));
    QVERIFY(qobject_cast<TertiaryButton *>(copy)); // R2: no frame until hover
    QVERIFY(!static_cast<TertiaryButton *>(copy)->framed());
    copy->click();
    const QStringList lines = QApplication::clipboard()->text().split(u'\n', Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines[0], QStringLiteral("/data/Trip/IMG_0412.JPG\t/data/Trip/.ctrm-tmp-7f3a-2"));
    QCOMPARE(lines[1], QStringLiteral("/data/Trip/.ctrm-tmp-7f3a-1\t/data/Trip/IMG_0411.JPG"));
}

void TestAppRecovery::openStepsMarkNotDefault()
{
    for (const bool damaged : {false, true}) {
        RecoveryDialog dialog(infoWith(twoOpenSteps(), damaged), nullptr);
        auto *mark = dialog.findChild<QPushButton *>(QStringLiteral("markComplete"));
        QVERIFY(mark);
        QVERIFY(mark->isEnabled());
        QVERIFY(!mark->isDefault());
        auto *primary = dialog.findChild<QPushButton *>(damaged ? QStringLiteral("close")
                                                                : QStringLiteral("rollBack"));
        QVERIFY(primary);
        QVERIFY(primary->isDefault());
    }
}

void TestAppRecovery::damagedNoRollBack()
{
    Fixture f;
    QVERIFY(!f.writeDamaged(QStringLiteral("dmg1")).isEmpty());
    f.answer = RecoveryChoice::Close;
    f.recovery.checkAtStart();
    QCOMPARE(f.asked.size(), 1);
    QVERIFY(f.asked[0].damaged);
    QVERIFY(f.asked[0].filePath.endsWith(u"-dmg1.jsonl"));

    RecoveryDialog dialog(f.asked[0], nullptr);
    QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("rollBack")));
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("showFile")));
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("close"))->isDefault());

    RecoveryInfo info = infoWith({}, true);
    QVERIFY(RecoveryDialog::bodyText(info).contains(u"Check the item names in /data/Trip."));
    QVERIFY(RecoveryDialog::bodyText(info).contains(
            u"If you close this dialog, CTRM asks again at the next start."));
    info.folders.clear();
    QVERIFY(RecoveryDialog::bodyText(info).contains(
            QStringLiteral("Check the names of the items that you renamed on %1 at ")
                    .arg(dayText(info.started))));
    info.started = {};
    QVERIFY(RecoveryDialog::bodyText(info).contains(
            u"Check the names of the items that you renamed last."));

    // A roll back is never done from a damaged journal, even when asked.
    f.answer = RecoveryChoice::RollBack;
    f.recovery.checkAtStart();
    QCOMPARE(f.reports.size(), 0);

    f.answer = RecoveryChoice::MarkComplete;
    f.recovery.checkAtStart();
    const int count = int(f.asked.size());
    f.recovery.checkAtStart();
    QCOMPARE(f.asked.size(), count);
}

void TestAppRecovery::normalTexts()
{
    const RecoveryInfo info = infoWith({});
    const QString body = RecoveryDialog::bodyText(info);
    QVERIFY(body.contains(
            QStringLiteral("CTRM stopped during a batch on %1 at ").arg(dayText(info.started))));
    QVERIFY(body.contains(QStringLiteral("412 of %1 operations were done.")
                                  .arg(QLocale::system().toString(1204))));
    QVERIFY(body.contains(u"Roll back puts the 412 items back to their old names."));
    QCOMPARE(RecoveryDialog::headingText(), QStringLiteral("The last rename did not finish."));
    RecoveryInfo one = info;
    one.done = 1;
    one.total = 1;
    one.doneItems = 1;
    QVERIFY(RecoveryDialog::bodyText(one).contains(u"1 of 1 operation was done."));
    QVERIFY(RecoveryDialog::bodyText(one).contains(
            u"Roll back puts the 1 item back to its old name."));
    RecoveryDialog dialog(info, nullptr);
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("showDetails")));
    QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("showFile")));
}

// Open item from Task 14: a quit with no close event (no cancel dialog). ~BatchController
// cancels and waits; the journal stays in-progress, and the next start asks about it.
void TestAppRecovery::quitDuringBatchAskedAtNextStart()
{
    QTemporaryDir dir;
    TempTree tree;
    const QString history = dir.path() + QStringLiteral("/history");
    const QString presets = dir.path() + QStringLiteral("/presets");
    QSettings qsettings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    constexpr int kFiles = 40;
    QStringList paths;
    for (int i = 0; i < kFiles; ++i)
        paths.append(tree.file(QStringLiteral("old_%1.txt").arg(i, 2, 10, QChar(u'0')), "x"));

    {
        AppContext ctx(createNativeFileSystem(), history, presets, &qsettings);
        MainWindow window(ctx);
        auto batch = std::make_unique<BatchController>(window, ctx, nullptr);
        std::atomic<bool> started = false;
        batch->onWorkerProgress = [&started](int, int) {
            started = true;
            QThread::msleep(20);
        };
        window.itemAdder().addPaths(paths);
        auto rule = std::make_unique<ReplaceRule>();
        rule->find = QStringLiteral("old");
        rule->replace = QStringLiteral("new");
        window.ruleModel().insertRule(0, std::move(rule));
        window.actions().rename->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(started.load(), 30'000);
        batch.reset(); // the quit: no close event, no failure dialog
    }

    QStringList renamed;
    for (const QString &p : paths) {
        const QString now =
                QFileInfo(p).path() + QStringLiteral("/new_") + QFileInfo(p).fileName().mid(4);
        if (QFile::exists(now))
            renamed.append(now);
    }
    QVERIFY(!renamed.isEmpty());
    QVERIFY(renamed.size() < kFiles);

    // The next start.
    AppContext ctx(createNativeFileSystem(), history, presets, &qsettings);
    MainWindow window(ctx);
    BatchController batch(window, ctx, nullptr);
    HistoryController hist(window, ctx, batch, nullptr);
    RecoveryController recovery(window, ctx, hist, nullptr);
    QList<RecoveryInfo> asked;
    recovery.askRecovery = [&asked](const RecoveryInfo &info) {
        asked.append(info);
        return RecoveryChoice::RollBack;
    };
    int reports = 0;
    recovery.showReport = [&reports](const FinishResult &, const QDateTime &) { ++reports; };
    recovery.checkAtStart();
    QCOMPARE(asked.size(), 1);
    QVERIFY(!asked[0].damaged);
    QCOMPARE(asked[0].total, kFiles);
    QCOMPARE(asked[0].done, int(renamed.size()));
    QCOMPARE(reports, 1);
    for (const QString &p : paths)
        QVERIFY2(QFile::exists(p), qPrintable(p));
    for (const BatchSummary &s : ctx.history().list())
        QCOMPARE(s.state, BatchState::RolledBack);
}

// R3: in both dialogs the heading stays at the top, next to the icon, and the body follows
// it directly, also when the dialog is taller than it needs.
void TestAppRecovery::headingNextToIcon()
{
    for (const bool damaged : {false, true}) {
        RecoveryDialog dialog(infoWith({}, damaged), nullptr);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.resize(dialog.width(), dialog.height() + 200);
        QApplication::processEvents();
        auto *heading = dialog.findChild<QLabel *>(QStringLiteral("recoveryHeading"));
        auto *body = dialog.findChild<QLabel *>(QStringLiteral("recoveryBody"));
        const int headingTop = heading->mapTo(&dialog, QPoint()).y();
        const int bodyTop = body->mapTo(&dialog, QPoint()).y();
        QVERIFY2(headingTop < 40, qPrintable(QString::number(headingTop)));
        QVERIFY2(bodyTop - (headingTop + heading->height()) < 20,
                 qPrintable(QStringLiteral("%1 %2").arg(bodyTop).arg(headingTop)));
    }
}

// N2: the dialog height follows its contents; no empty band above the buttons.
void TestAppRecovery::noGapAboveButtons()
{
    for (const bool damaged : {false, true}) {
        for (const bool steps : {false, true}) {
            RecoveryDialog dialog(infoWith(steps ? twoOpenSteps() : QList<OpenStep>{}, damaged),
                                  nullptr);
            dialog.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            QApplication::processEvents();
            QWidget *last = dialog.findChild<QLabel *>(steps ? QStringLiteral("openStepsNote")
                                                             : QStringLiteral("recoveryBody"));
            auto *mark = dialog.findChild<QPushButton *>(QStringLiteral("markComplete"));
            const int textBottom = last->mapTo(&dialog, QPoint(0, last->height())).y();
            const int gap = mark->mapTo(&dialog, QPoint()).y() - textBottom;
            QVERIFY2(gap >= 0 && gap < 40, qPrintable(QStringLiteral("damaged %1 steps %2 gap %3")
                                                              .arg(damaged)
                                                              .arg(steps)
                                                              .arg(gap)));
        }
    }
}

QTEST_MAIN(TestAppRecovery)
#include "tst_app_recovery.moc"
