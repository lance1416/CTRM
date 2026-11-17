// SPDX-License-Identifier: GPL-3.0-or-later
// The main window after a batch and after a quick undo (ui-design 9.8, 10.3; audit M02, S04).
#include "appcontext.h"
#include "controllers/batchcontroller.h"
#include "controllers/historycontroller.h"
#include "controllers/itemadder.h"
#include "fs/filesystem.h"
#include "mainwindow.h"
#include "models/itemtablemodel.h"
#include "models/rulelistmodel.h"
#include "rules/replacerule.h"
#include "temptree.h"
#include "text/messages.h"
#include "widgets/actionbar.h"
#include "widgets/messagebar.h"

#include <QAction>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include <functional>
#include <memory>

using namespace ctrm;
using namespace ctrm::app;

namespace {

struct Fixture
{
    Fixture()
        : qsettings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat)
        , ctx(createNativeFileSystem(), dir.filePath(QStringLiteral("history")),
              dir.filePath(QStringLiteral("presets")), &qsettings)
        , window(ctx)
        , batch(window, ctx, nullptr)
        , history(window, ctx, batch, nullptr)
    {
        batch.showReport = [](const FinishResult &) {};
        history.showUndoPreview = [](const UndoPreview &) { return false; };
    }

    // Adds the files and renames "old" to "new".
    void renameOld(const QStringList &rels)
    {
        QStringList paths;
        for (const QString &rel : rels)
            paths.append(tree.file(rel, "x"));
        window.itemAdder().addPaths(paths);
        auto rule = std::make_unique<ReplaceRule>();
        rule->find = QStringLiteral("old");
        rule->replace = QStringLiteral("new");
        window.ruleModel().insertRule(0, std::move(rule));
        QVERIFY(run([this] { window.actions().rename->trigger(); }));
    }

    bool run(const std::function<void()> &action)
    {
        QSignalSpy spy(&batch, &BatchController::batchFinished);
        action();
        return spy.count() > 0 || spy.wait(30'000);
    }

    QPushButton *barButton(const char *name) const
    {
        return window.findChild<QPushButton *>(QLatin1StringView(name));
    }

    QToolButton *step3() const
    {
        return window.findChild<QToolButton *>(QStringLiteral("flowStep3"));
    }

    QString barText() const
    {
        return window.findChild<QLabel *>(QStringLiteral("messageBarText"))->text();
    }

    QStringList names()
    {
        QStringList result;
        for (const Item &item : window.itemModel().items())
            result.append(item.name);
        return result;
    }

    QStringList postRun()
    {
        QStringList result;
        ItemTableModel &m = window.itemModel();
        for (int row = 0; row < m.rowCount(); ++row)
            result.append(QString::number(
                    m.index(row, ItemTableModel::State).data(ItemTableModel::PostRunRole).toInt()));
        return result;
    }

    QTemporaryDir dir;
    TempTree tree;
    QSettings qsettings;
    AppContext ctx;
    MainWindow window;
    BatchController batch;
    HistoryController history;
};

} // namespace

class TestUndoResult : public QObject
{
    Q_OBJECT
private slots:
    void quickUndoShowsNamesOnDisk();
};

void TestUndoResult::quickUndoShowsNamesOnDisk()
{
    Fixture f;
    f.window.show();
    f.renameOld({QStringLiteral("old_a.txt"), QStringLiteral("old_b.txt")});
    QCOMPARE(f.barText(), QStringLiteral("Renamed 2 items."));
    QCOMPARE(f.step3()->accessibleName(), QStringLiteral("Step 3 of 3, Rename, done"));
    QPushButton *undo = f.barButton("resultUndo");
    QVERIFY(undo);

    QVERIFY(f.run([&] { undo->click(); }));
    QVERIFY(QFile::exists(f.tree.root() + QStringLiteral("/old_a.txt")));

    // S04: the message, then the items with their names on disk and a new preview.
    QCOMPARE(f.barText(), QStringLiteral("Undid the rename of 2 items."));
    QVERIFY(!f.barButton("resultUndo"));
    QVERIFY(f.barButton("resultShowReport"));
    QCOMPARE(f.names(), QStringList({QStringLiteral("old_a.txt"), QStringLiteral("old_b.txt")}));
    QCOMPARE(f.postRun(), QStringList({QString::number(int(PostRun::None)),
                                       QString::number(int(PostRun::None))}));
    QTRY_VERIFY(f.window.previewIsCurrent());
    QCOMPARE(f.window.currentPreview().summary.willChange, 2);
    QVERIFY(f.window.actionBar()->renameButton()->isEnabled());
    QCOMPARE(f.window.actionBar()->renameButton()->text(), QStringLiteral("Rename 2 Files"));
    QVERIFY(f.window.actions().rename->isEnabled());
    // S01: after the undo, step 3 is "later" again.
    QCOMPARE(f.step3()->accessibleName(), QStringLiteral("Step 3 of 3, Rename, current"));
}

QTEST_MAIN(TestUndoResult)
#include "tst_app_undoresult.moc"
