// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"
#include "controllers/batchcontroller.h"
#include "controllers/historycontroller.h"
#include "controllers/itemadder.h"
#include "dialogs/historywindow.h"
#include "dialogs/undopreviewdialog.h"
#include "exec/batchservice.h"
#include "exec/historystore.h"
#include "fakefilesystem.h"
#include "fs/filesystem.h"
#include "mainwindow.h"
#include "models/historymodel.h"
#include "models/itemtablemodel.h"
#include "models/rulelistmodel.h"
#include "rules/replacerule.h"
#include "temptree.h"
#include "text/messages.h"
#include "widgets/itemtableview.h"

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <memory>
#include <optional>

using namespace ctrm;
using namespace ctrm::app;

namespace {

std::unique_ptr<Rule> replaceRule(const QString &find, const QString &with)
{
    auto rule = std::make_unique<ReplaceRule>();
    rule->find = find;
    rule->replace = with;
    return rule;
}

// Main window, batch and history controllers on one file system, with temporary folders.
struct Fixture
{
    explicit Fixture(std::unique_ptr<FileSystem> fs = createNativeFileSystem(),
                     const QString &base = {})
        : qsettings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat)
        , ctx(std::move(fs), (base.isEmpty() ? dir.path() : base) + QStringLiteral("/history"),
              (base.isEmpty() ? dir.path() : base) + QStringLiteral("/presets"), &qsettings)
        , window(ctx)
        , batch(window, ctx, nullptr)
        , history(window, ctx, batch, nullptr)
    {
        window.itemAdder().askFolderDrop = [](const FolderDropQuestion &) {
            return FolderDropReply{FolderDropReply::Choice::RenameFolder, false, false};
        };
        batch.askFailure = [](const FailureInfo &) { return FailureChoice::RollBack; };
        batch.showReport = [](const FinishResult &) {};
        history.showUndoPreview = [this](const UndoPreview &p) {
            undoPreview = p;
            ++previewCalls;
            return previewAnswer;
        };
        history.confirmClearHistory = [this](int damaged) {
            clearDamagedCount = damaged;
            return clearAnswer;
        };
    }

    QString root() const { return tree.root(); }
    bool exists(const QString &rel) const { return QFile::exists(root() + u'/' + rel); }

    // Makes the files, adds them and renames "old" to "new" (one batch). Returns the batch ID.
    QString renameOld(const QStringList &rels)
    {
        QStringList paths;
        for (const QString &rel : rels)
            paths.append(tree.file(rel, "x"));
        window.itemAdder().addPaths(paths);
        if (window.ruleModel().rowCount() == 0)
            window.ruleModel().insertRule(
                    0, replaceRule(QStringLiteral("old"), QStringLiteral("new")));
        if (!run([this] { window.actions().rename->trigger(); }))
            return {};
        return batch.lastBatchId();
    }

    // Runs the action and waits for the end of the batch. False when no batch ran.
    bool run(const std::function<void()> &action, int timeout = 30'000)
    {
        QSignalSpy spy(&batch, &BatchController::batchFinished);
        action();
        return spy.count() > 0 || spy.wait(timeout);
    }

    BatchSummary summary(const QString &id) const
    {
        for (const BatchSummary &s : const_cast<Fixture *>(this)->ctx.history().list())
            if (s.batchId == id)
                return s;
        return {};
    }

    void writeDamaged(const QString &id)
    {
        QFile f(ctx.historyDir() + QStringLiteral("/20261101T100000Z-") + id +
                QStringLiteral(".jsonl"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not json\n{\"type\":\n");
    }

    QTemporaryDir dir;
    TempTree tree;
    QSettings qsettings;
    AppContext ctx;
    MainWindow window;
    BatchController batch;
    HistoryController history;

    bool previewAnswer = false;
    int previewCalls = 0;
    std::optional<UndoPreview> undoPreview;
    bool clearAnswer = true;
    int clearDamagedCount = -1;
};

BatchSummary summaryWith(BatchState state, int failed = 0)
{
    BatchSummary s;
    s.batchId = QStringLiteral("b%1").arg(int(state));
    s.started = QDateTime::currentDateTime();
    s.itemCount = 2;
    s.folders = {QStringLiteral("/tmp/x")};
    s.state = state;
    s.failedCount = failed;
    return s;
}

QPushButton *button(QWidget *w, const char *name)
{
    return w->findChild<QPushButton *>(QLatin1StringView(name));
}

bool shown(QWidget *w, const char *name)
{
    QPushButton *b = button(w, name);
    return b && b->isVisibleTo(w);
}

} // namespace

class TestHistory : public QObject
{
    Q_OBJECT
private slots:
    void quickUndoNoPreview();
    void quickUndoOpensPreview();
    void quickUndoWeakOpensPreview();
    void quickUndoUnavailableAfterListChange();
    void ctrlZInTextField();
    void historyStatesShown();
    void whenText();
    void undoBatchButtonByState();
    void damagedRow();
    void undoOfLabel();
    void undoOlderBatch();
    void partlyUndoneUi();
    void loadRules();
    void clearHistoryConfirms();
    void clearKeepsDamaged();
    void clearTextDamaged();
    void undoAfterRestart();
    void undoPreviewTexts();
    void tablesLayout();
};

void TestHistory::quickUndoNoPreview()
{
    Fixture f;
    QVERIFY(!f.renameOld({"old_a.txt", "old_b.txt", "old_c.txt"}).isEmpty());
    QVERIFY(f.exists(QStringLiteral("new_c.txt")));
    QVERIFY(f.history.quickUndoAvailable());
    QVERIFY(f.run([&] { f.history.quickUndo(); }));
    QCOMPARE(f.previewCalls, 0);
    QVERIFY(f.exists(QStringLiteral("old_a.txt")));
    QVERIFY(f.exists(QStringLiteral("old_b.txt")));
    QVERIFY(f.exists(QStringLiteral("old_c.txt")));
    QVERIFY(!f.history.quickUndoAvailable());
}

void TestHistory::quickUndoOpensPreview()
{
    Fixture f;
    QVERIFY(!f.renameOld({"old_a.txt", "old_b.txt"}).isEmpty());
    QVERIFY(QFile::rename(f.root() + QStringLiteral("/new_a.txt"),
                          f.root() + QStringLiteral("/z.txt")));
    f.previewAnswer = false;
    f.history.quickUndo();
    QCOMPARE(f.previewCalls, 1);
    QCOMPARE(f.undoPreview->passCount, 1);
    QTest::qWait(50);
    QVERIFY(!f.batch.isRunning());
    QVERIFY(f.exists(QStringLiteral("new_b.txt")));
    QVERIFY(f.exists(QStringLiteral("z.txt")));
}

void TestHistory::quickUndoWeakOpensPreview()
{
    auto fake = std::make_unique<FakeFileSystem>();
    fake->addFolder(QStringLiteral("/w"), {CaseSensitivity::Insensitive, false, IdentityType::Weak,
                                           QStringLiteral("exfat")});
    fake->addFile(QStringLiteral("/w/old_a.txt"), "x");
    Fixture f(std::move(fake));
    f.window.itemAdder().addPaths({QStringLiteral("/w/old_a.txt")});
    f.window.ruleModel().insertRule(0, replaceRule(QStringLiteral("old"), QStringLiteral("new")));
    QVERIFY(f.run([&] { f.window.actions().rename->trigger(); }));
    QVERIFY(f.ctx.fileSystem().exists(QStringLiteral("/w/new_a.txt")));

    f.history.quickUndo();
    QCOMPARE(f.previewCalls, 1);
    QVERIFY(f.undoPreview->weak());
    UndoPreviewDialog dialog(*f.undoPreview, nullptr);
    auto *weak = dialog.findChild<QLabel *>(QStringLiteral("weakBar"));
    QVERIFY(weak);
    QVERIFY(weak->isVisibleTo(&dialog));
    QVERIFY(weak->text().startsWith(QStringLiteral("Weak identity check.")));
    QVERIFY(weak->text().contains(QStringLiteral("exfat")));
}

void TestHistory::quickUndoUnavailableAfterListChange()
{
    Fixture f;
    QVERIFY(!f.renameOld({"old_a.txt"}).isEmpty());
    QVERIFY(f.history.quickUndoAvailable());
    QVERIFY(f.window.actions().undoRename->isEnabled());
    QCOMPARE(f.window.actions().undoRename->text(), QStringLiteral("Undo Rename of 1 Item"));
    // A rule edit is not a list change (E6).
    f.window.ruleModel().insertRule(1, replaceRule(QStringLiteral("q"), QStringLiteral("r")));
    QVERIFY(f.history.quickUndoAvailable());
    f.window.itemAdder().addPaths({f.tree.file(QStringLiteral("other.txt"), "x")});
    QVERIFY(!f.history.quickUndoAvailable());
    QVERIFY(!f.window.actions().undoRename->isEnabled());
    QCOMPARE(f.window.actions().undoRename->text(), QStringLiteral("Undo Rename"));
}

void TestHistory::ctrlZInTextField()
{
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    QVERIFY(!f.renameOld({"old_a.txt", "old_b.txt"}).isEmpty());
    f.window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.window.tableView()->setFocus();
    QTRY_VERIFY(f.window.actions().undoRename->isEnabled());
    QCOMPARE(f.window.actions().undoRename->text(), QStringLiteral("Undo Rename of 2 Items"));
    auto *filter = f.window.findChild<QLineEdit *>(QStringLiteral("filterField"));
    QVERIFY(filter);
    filter->setFocus();
    QTRY_VERIFY(!f.window.actions().undoRename->isEnabled());
    QVERIFY(!f.history.quickUndoAvailable());
    f.window.tableView()->setFocus();
    QTRY_VERIFY(f.window.actions().undoRename->isEnabled());
}

void TestHistory::historyStatesShown()
{
    HistoryModel model;
    model.setBatches({summaryWith(BatchState::InProgress), summaryWith(BatchState::Complete, 3),
                      summaryWith(BatchState::RolledBack), summaryWith(BatchState::Incomplete),
                      summaryWith(BatchState::PartlyUndone), summaryWith(BatchState::Undone),
                      summaryWith(BatchState::Complete, 1)});
    const auto state = [&](int row) {
        return model.data(model.index(row, HistoryModel::State)).toString();
    };
    QCOMPARE(state(0), QStringLiteral("In progress"));
    QCOMPARE(state(1), QStringLiteral("Complete, 3 failed"));
    QCOMPARE(state(2), QStringLiteral("Rolled back"));
    QCOMPARE(state(3), QStringLiteral("Incomplete"));
    QCOMPARE(state(4), QStringLiteral("Partly undone"));
    QCOMPARE(state(5), QStringLiteral("Undone"));
    QCOMPARE(state(6), QStringLiteral("Complete, 1 failed"));
    QVERIFY(!model.data(model.index(0, HistoryModel::State), Qt::DecorationRole).isNull());
    QVERIFY(!model.data(model.index(3, HistoryModel::State), Qt::DecorationRole).isNull());
    QVERIFY(model.data(model.index(2, HistoryModel::State), Qt::DecorationRole).isNull());
    QCOMPARE(model.data(model.index(0, HistoryModel::Items)).toString(), QStringLiteral("2"));
}

void TestHistory::whenText()
{
    const QDateTime now(QDate(2026, 11, 17), QTime(15, 0));
    QCOMPARE(HistoryModel::whenText(QDateTime(QDate(2026, 11, 17), QTime(14, 2)), now),
             QStringLiteral("Today %1")
                     .arg(QLocale::system().toString(QTime(14, 2), QLocale::ShortFormat)));
    QVERIFY(HistoryModel::whenText(QDateTime(QDate(2026, 11, 16), QTime(18, 15)), now)
                    .startsWith(QStringLiteral("Yesterday ")));
    const QString older = HistoryModel::whenText(QDateTime(QDate(2026, 11, 14), QTime(10, 5)), now);
    QVERIFY(!older.startsWith(QStringLiteral("Today")));
    QVERIFY(older.contains(QStringLiteral("14")));

    BatchSummary s = summaryWith(BatchState::Complete);
    s.folders = {QDir::homePath() + QStringLiteral("/Pictures")};
    QCOMPARE(HistoryModel::foldersText(s, {}),
             QDir::toNativeSeparators(QStringLiteral("~/Pictures")));
    s.folders = {QStringLiteral("/a"), QStringLiteral("/b"), QStringLiteral("/c")};
    QCOMPARE(HistoryModel::foldersText(s, {}), QStringLiteral("3 folders"));
}

void TestHistory::undoBatchButtonByState()
{
    Fixture f;
    const QString id = f.renameOld({"old_a.txt"});
    QVERIFY(!id.isEmpty());
    HistoryWindow w(f.ctx.history(), nullptr);
    w.refresh();
    w.selectBatch(id);
    QVERIFY(shown(&w, "undoBatch"));
    QVERIFY(button(&w, "undoBatch")->isDefault());
    QCOMPARE(button(&w, "undoBatch")->text(), QStringLiteral("Undo Batch..."));
    QVERIFY(!shown(&w, "retryRollBack"));
    QVERIFY(!shown(&w, "markComplete"));
    QVERIFY(shown(&w, "loadRules"));

    QVERIFY(f.ctx.history().appendState(
            id, {BatchState::Incomplete, QDateTime::currentDateTimeUtc(), {}}, nullptr));
    w.refresh();
    w.selectBatch(id);
    QVERIFY(!shown(&w, "undoBatch"));
    QVERIFY(shown(&w, "retryRollBack"));
    QCOMPARE(button(&w, "retryRollBack")->text(), QStringLiteral("Retry Roll Back..."));

    QVERIFY(f.ctx.history().appendState(
            id, {BatchState::InProgress, QDateTime::currentDateTimeUtc(), {}}, nullptr));
    w.refresh();
    w.selectBatch(id);
    QVERIFY(shown(&w, "rollBack"));
    QCOMPARE(button(&w, "rollBack")->text(), QStringLiteral("Roll Back..."));
    QVERIFY(shown(&w, "markComplete"));
    QVERIFY(!shown(&w, "undoBatch"));

    QVERIFY(f.ctx.history().appendState(
            id, {BatchState::RolledBack, QDateTime::currentDateTimeUtc(), {}}, nullptr));
    w.refresh();
    w.selectBatch(id);
    QVERIFY(!shown(&w, "undoBatch"));
    QVERIFY(!shown(&w, "retryRollBack"));
    QVERIFY(!shown(&w, "rollBack"));
}

void TestHistory::damagedRow()
{
    Fixture f;
    QVERIFY(!f.renameOld({"old_a.txt"}).isEmpty());
    f.writeDamaged(QStringLiteral("dmg1"));
    HistoryWindow w(f.ctx.history(), nullptr);
    w.refresh();
    w.selectBatch(QStringLiteral("dmg1"));
    const HistoryModel *model = w.model();
    int row = -1;
    for (int i = 0; i < model->rowCount(); ++i)
        if (model->batch(i).batchId == QStringLiteral("dmg1"))
            row = i;
    QVERIFY(row >= 0);
    QCOMPARE(model->data(model->index(row, HistoryModel::State)).toString(),
             QStringLiteral("Damaged"));
    QVERIFY(!model->data(model->index(row, HistoryModel::State), Qt::DecorationRole).isNull());
    QVERIFY(shown(&w, "showFile"));
    QCOMPARE(button(&w, "showFile")->text(), showInFileManagerText());
    QVERIFY(!shown(&w, "undoBatch"));
    QVERIFY(!shown(&w, "loadRules"));
    QVERIFY(!shown(&w, "rollBack"));
    QVERIFY(!shown(&w, "markComplete"));
    QVERIFY(!shown(&w, "retryRollBack"));
    auto *detail = w.findChild<QLabel *>(QStringLiteral("historyDetail"));
    QVERIFY(detail);
    QVERIFY(detail->text().startsWith(QStringLiteral(
            "CTRM cannot read the history file of this batch. It cannot undo or roll back this "
            "batch.")));
    auto *keep = w.findChild<QLabel *>(QStringLiteral("historyDetailKeep"));
    QVERIFY(keep && keep->isVisibleTo(&w));
    QCOMPARE(keep->text(), QStringLiteral("CTRM keeps this file. To remove it, show it in the file "
                                          "manager and delete it there."));
    // When from the file name (no milliseconds), Items empty, no operations table (H2).
    const QDateTime stamp(QDate(2026, 11, 1), QTime(10, 0), QTimeZone::UTC);
    QCOMPARE(model->data(model->index(row, HistoryModel::When)).toString(),
             HistoryModel::whenText(stamp, QDateTime::currentDateTime()));
    QCOMPARE(model->data(model->index(row, HistoryModel::Items)).toString(), QString());
    QVERIFY(!w.findChild<QTableView *>(QStringLiteral("historyOperations"))->isVisibleTo(&w));
    w.selectBatch(model->batch(row == 0 ? 1 : 0).batchId);
    QVERIFY(w.findChild<QTableView *>(QStringLiteral("historyOperations"))->isVisibleTo(&w));

    // No time in the file name: "Unknown".
    BatchSummary noTime;
    noTime.damaged = true;
    noTime.batchId = QStringLiteral("x");
    noTime.filePath = QStringLiteral("/h/x.jsonl");
    HistoryModel m;
    m.setBatches({noTime});
    QCOMPARE(m.data(m.index(0, HistoryModel::When)).toString(), QStringLiteral("Unknown"));
}

void TestHistory::tablesLayout()
{
    Fixture f;
    HistoryWindow w(f.ctx.history(), nullptr);
    auto *batches = w.findChild<QTableView *>(QStringLiteral("historyBatches"));
    auto *operations = w.findChild<QTableView *>(QStringLiteral("historyOperations"));
    QVERIFY(batches && operations);
    // G1: no grid, as in the main table.
    QVERIFY(!batches->showGrid());
    QVERIFY(!operations->showGrid());
    // H1: the folder gets the free width and keeps its end.
    QCOMPARE(batches->textElideMode(), Qt::ElideMiddle);
    QVERIFY(!batches->horizontalHeader()->stretchLastSection());
    QCOMPARE(batches->horizontalHeader()->sectionResizeMode(HistoryModel::Folders),
             QHeaderView::Stretch);
    QCOMPARE(batches->horizontalHeader()->sectionResizeMode(HistoryModel::State),
             QHeaderView::ResizeToContents);
    QCOMPARE(HistoryModel::displayPath(QDir::homePath() + QStringLiteral("/Pictures/Trip")),
             QDir::toNativeSeparators(QStringLiteral("~/Pictures/Trip")));
}

void TestHistory::undoOfLabel()
{
    Fixture f;
    const QString original = f.renameOld({"old_a.txt"});
    QVERIFY(f.run([&] { f.history.quickUndo(); }));
    const QList<BatchSummary> all = f.ctx.history().list();
    QCOMPARE(all.size(), 2);
    const BatchSummary undo = all.first().undoOf.isEmpty() ? all.last() : all.first();
    QCOMPARE(undo.undoOf, original);
    QCOMPARE(HistoryModel::foldersText(undo, all),
             QStringLiteral("Undo of %1")
                     .arg(HistoryModel::whenText(f.summary(original).started,
                                                 QDateTime::currentDateTime())));
    HistoryWindow w(f.ctx.history(), nullptr);
    w.refresh();
    w.selectBatch(undo.batchId);
    QVERIFY(!shown(&w, "undoBatch"));
    QCOMPARE(w.findChild<QLabel *>(QStringLiteral("historyDetail"))->text(),
             QStringLiteral("This batch is an undo. It cannot be undone."));
    QCOMPARE(f.summary(original).state, BatchState::Undone);
}

void TestHistory::undoOlderBatch()
{
    Fixture f;
    // The second file exists before the first batch: the folder cache has it.
    f.tree.file(QStringLiteral("old_b.txt"), "x");
    const QString first = f.renameOld({"old_a.txt"});
    const QString second = f.renameOld({"old_b.txt"});
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QVERIFY(first != second);
    f.previewAnswer = true;
    QVERIFY(f.run([&] { f.history.undoBatch(first); }));
    QCOMPARE(f.previewCalls, 1);
    QVERIFY(f.exists(QStringLiteral("old_a.txt")));
    QVERIFY(f.exists(QStringLiteral("new_b.txt")));
    QCOMPARE(f.summary(first).state, BatchState::Undone);
    QCOMPARE(f.summary(second).state, BatchState::Complete);
}

void TestHistory::partlyUndoneUi()
{
    Fixture f;
    const QString id = f.renameOld({"old_a.txt", "old_b.txt"});
    // Another file takes the old name of a: a cannot go back.
    f.tree.file(QStringLiteral("old_a.txt"), "other");
    f.previewAnswer = true;
    QVERIFY(f.run([&] { f.history.undoBatch(id); }));
    QCOMPARE(f.undoPreview->passCount, 1);
    QVERIFY(f.exists(QStringLiteral("old_b.txt")));
    QVERIFY(f.exists(QStringLiteral("new_a.txt")));
    QCOMPARE(f.summary(id).state, BatchState::PartlyUndone);
    HistoryWindow w(f.ctx.history(), nullptr);
    w.refresh();
    w.selectBatch(id);
    const HistoryModel *model = w.model();
    bool found = false;
    for (int i = 0; i < model->rowCount(); ++i)
        if (model->batch(i).batchId == id)
            found = model->data(model->index(i, HistoryModel::State)).toString() ==
                    QStringLiteral("Partly undone");
    QVERIFY(found);
    QVERIFY(shown(&w, "undoBatch"));
}

void TestHistory::loadRules()
{
    Fixture f;
    const QString id = f.renameOld({"old_a.txt"});
    f.window.ruleModel().setChain(RuleChain());
    QCOMPARE(f.window.ruleModel().rowCount(), 0);
    f.history.loadRules(id);
    QCOMPARE(f.window.ruleModel().rowCount(), 1);
    const auto *rule = dynamic_cast<const ReplaceRule *>(f.window.ruleModel().chain().at(0));
    QVERIFY(rule);
    QCOMPARE(rule->find, QStringLiteral("old"));
    QCOMPARE(rule->replace, QStringLiteral("new"));
}

void TestHistory::clearHistoryConfirms()
{
    Fixture f;
    QVERIFY(!f.renameOld({"old_a.txt"}).isEmpty());
    f.clearAnswer = false;
    f.history.clearHistory();
    QCOMPARE(f.clearDamagedCount, 0);
    QCOMPARE(f.ctx.history().list().size(), 1);
}

void TestHistory::clearKeepsDamaged()
{
    Fixture f;
    QVERIFY(!f.renameOld({"old_a.txt"}).isEmpty());
    f.writeDamaged(QStringLiteral("dmg1"));
    f.clearAnswer = true;
    f.history.clearHistory();
    QCOMPARE(f.clearDamagedCount, 1);
    const QList<BatchSummary> all = f.ctx.history().list();
    QCOMPARE(all.size(), 1);
    QVERIFY(all.first().damaged);
    QVERIFY(!f.history.quickUndoAvailable());
}

void TestHistory::clearTextDamaged()
{
    Fixture f;
    f.writeDamaged(QStringLiteral("dmg1"));
    f.writeDamaged(QStringLiteral("dmg2"));
    f.clearAnswer = false;
    f.history.clearHistory();
    QCOMPARE(f.clearDamagedCount, 2);
    QCOMPARE(HistoryController::clearHistoryInformativeText(2),
             QStringLiteral(
                     "You cannot undo the batches in it after this.\nCTRM keeps the 2 damaged "
                     "batches. You can remove their files by hand."));
    QCOMPARE(HistoryController::clearHistoryInformativeText(1),
             QStringLiteral("You cannot undo the batches in it after this.\nCTRM keeps the damaged "
                            "batch. You can remove its file by hand."));
    QCOMPARE(HistoryController::clearHistoryInformativeText(0),
             QStringLiteral("You cannot undo the batches in it after this."));
}

void TestHistory::undoAfterRestart()
{
    QTemporaryDir base;
    TempTree tree;
    QString id;
    const QString a = tree.file(QStringLiteral("old_a.txt"), "x");
    {
        Fixture f(createNativeFileSystem(), base.path());
        f.window.itemAdder().addPaths({a});
        f.window.ruleModel().insertRule(0,
                                        replaceRule(QStringLiteral("old"), QStringLiteral("new")));
        QVERIFY(f.run([&] { f.window.actions().rename->trigger(); }));
        id = f.batch.lastBatchId();
    }
    QVERIFY(QFile::exists(tree.root() + QStringLiteral("/new_a.txt")));
    Fixture g(createNativeFileSystem(), base.path());
    QVERIFY(!g.history.quickUndoAvailable());
    QCOMPARE(g.ctx.history().list().size(), 1);
    QCOMPARE(g.ctx.history().list().first().batchId, id);
    g.previewAnswer = true;
    QVERIFY(g.run([&] { g.history.undoBatch(id); }));
    QVERIFY(QFile::exists(a));
}

void TestHistory::undoPreviewTexts()
{
    UndoPreview p;
    p.originalStarted = QDateTime::currentDateTime();
    for (int i = 0; i < 3; ++i) {
        UndoItem item;
        item.item = ItemId(i + 1);
        item.currentFolder = QStringLiteral("/f");
        item.currentName = QStringLiteral("new_%1").arg(i);
        item.originalName = QStringLiteral("old_%1").arg(i);
        item.preview.newName = item.originalName;
        item.preview.status = i == 0 ? Status::Missing : Status::Changed;
        p.items.append(item);
    }
    p.passCount = 2;
    UndoPreviewDialog dialog(p, nullptr);
    QVERIFY(dialog.windowTitle().startsWith(QStringLiteral("Undo Rename of 3 Items - ")));
    QPushButton *run = dialog.findChild<QPushButton *>(QStringLiteral("undoRun"));
    QVERIFY(run);
    QCOMPARE(run->text(), QStringLiteral("Undo 2 Items"));
    QVERIFY(run->isEnabled());
    QVERIFY(run->isDefault());
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("undoFooter"))->text(),
             QStringLiteral("1 item cannot go back. It keeps its name."));
    QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("weakBar"))->isVisibleTo(&dialog));
    QVERIFY(!dialog.findChild<QTableView *>(QStringLiteral("undoTable"))->showGrid());
    // U1: the undo preview has no Refresh or Remove.
    const QString missing = dialog.findChild<QTableView *>(QStringLiteral("undoTable"))
                                    ->model()
                                    ->index(0, 3)
                                    .data()
                                    .toString();
    QCOMPARE(missing, QStringLiteral("Missing: not on disk, or changed after the batch."));
    QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("undoFooter"))->isVisibleTo(&dialog));
    QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("undoReason"))->isVisibleTo(&dialog));

    p.passCount = 0;
    UndoPreviewDialog none(p, nullptr);
    run = none.findChild<QPushButton *>(QStringLiteral("undoRun"));
    QVERIFY(!run->isEnabled());
    auto *reason = none.findChild<QLabel *>(QStringLiteral("undoReason"));
    QCOMPARE(reason->text(), QStringLiteral("No item can go back."));
    // U2: one line only, and the reason is directly to the left of the buttons.
    QVERIFY(reason->isVisibleTo(&none));
    QVERIFY(!none.findChild<QLabel *>(QStringLiteral("undoFooter"))->isVisibleTo(&none));
    none.show();
    QVERIFY(QTest::qWaitForWindowExposed(&none));
    auto *cancel = none.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel);
    const int gap = std::min(cancel->mapTo(&none, QPoint()).x(), run->mapTo(&none, QPoint()).x()) -
                    reason->mapTo(&none, QPoint(reason->width(), 0)).x();
    QVERIFY2(gap >= 0 && gap < 60, qPrintable(QString::number(gap)));
}

QTEST_MAIN(TestHistory)
#include "tst_app_history.moc"
