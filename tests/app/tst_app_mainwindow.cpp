// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"
#include "controllers/itemadder.h"
#include "exec/batchservice.h"
#include "exec/historystore.h"
#include "fakefilesystem.h"
#include "fs/filesystem.h"
#include "mainwindow.h"
#include "models/itemtablemodel.h"
#include "models/rulelistmodel.h"
#include "presets/presetstore.h"
#include "preview/previewscheduler.h"
#include "rules/insertrule.h"
#include "rules/numberingrule.h"
#include "rules/replacerule.h"
#include "temptree.h"
#include "widgets/actionbar.h"
#include "widgets/emptystate.h"
#include "widgets/flowstrip.h"
#include "widgets/itemtableview.h"
#include "widgets/messagebar.h"
#include "widgets/rulepanel.h"
#include "widgets/tertiarybutton.h"

#include <QAction>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>

#include <memory>

using namespace ctrm;
using namespace ctrm::app;

namespace {

// A main window on the native file system, with temporary folders and settings.
struct Fixture
{
    Fixture()
        : qsettings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat)
        , ctx(createNativeFileSystem(), dir.filePath(QStringLiteral("history")),
              dir.filePath(QStringLiteral("presets")), &qsettings)
        , window(ctx)
    {
        window.itemAdder().askFolderDrop = [](const FolderDropQuestion &) {
            return FolderDropReply{FolderDropReply::Choice::AddContents, false, false};
        };
    }

    QStringList files(const QStringList &names)
    {
        QStringList paths;
        for (const QString &name : names)
            paths.append(tree.file(name, "x"));
        return paths;
    }

    void addFiles(const QStringList &names) { window.itemAdder().addPaths(files(names)); }

    QStringList oldNames()
    {
        QStringList result;
        for (const Item &item : window.itemModel().items())
            result.append(item.name);
        return result;
    }

    QStringList newNames() const
    {
        QStringList result;
        for (const ItemPreview &p : window.currentPreview().items)
            result.append(p.newName);
        return result;
    }

    QAction *addRuleAction(const QString &type) const
    {
        return window.actions().addRuleMenu->findChild<QAction *>(QStringLiteral("addRule_") +
                                                                  type);
    }

    template <typename T>
    T *child(const char *name) const
    {
        return window.findChild<T *>(QLatin1StringView(name));
    }

    bool shown(const QWidget *w) const { return w && w->isVisibleTo(&window); }

    QTemporaryDir dir;
    TempTree tree;
    QSettings qsettings;
    AppContext ctx;
    MainWindow window;
};

std::unique_ptr<Rule> regexRemoveDigits()
{
    auto rule = std::make_unique<ReplaceRule>();
    rule->find = QStringLiteral("\\d");
    rule->mode = ReplaceRule::Mode::Regex;
    return rule;
}

} // namespace

class TestMainWindow : public QObject
{
    Q_OBJECT
private slots:
    void contextStores();
    void emptyStateShown();
    void addRulePrimaryWithItems();
    void prefixFlowUnder60s();
    void moveRowUpdatesNumbering();
    void sortByNewNameStable();
    void filterChangedOnly();
    void filterEmptyText();
    void blockingReasonTooltip();
    void refreshDetectsMissing();
    void addReadsFolderAgain();
    void activationReadsFolderAgain();
    void conflictTipShown();
    void tabOrder();
    void presetTitle();
    void listActionsAtStart();
    void pendingPreviewBlocksRename();
    void toolbarIcons();
    void tertiaryButtonsHaveNoFrame();
    void ruleMenuButtonsArePushButtons();
};

void TestMainWindow::contextStores()
{
    QTemporaryDir dir;
    QSettings qs(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    AppContext ctx(std::make_unique<FakeFileSystem>(), dir.filePath(QStringLiteral("h")),
                   dir.filePath(QStringLiteral("p")), &qs);
    QVERIFY(ctx.history().list().isEmpty());
    QCOMPARE(ctx.history().dir(), dir.filePath(QStringLiteral("h")));
    QCOMPARE(ctx.presets().builtIns().size(), 4);
    QVERIFY(ctx.batchService().pendingRecovery().isEmpty());
    QVERIFY(!ctx.appInfo().ctrmVersion.isEmpty());
}

void TestMainWindow::emptyStateShown()
{
    Fixture f;
    f.window.show();
    QVERIFY(f.shown(f.child<EmptyState>("emptyState")));
    QVERIFY(!f.shown(f.window.tableView()));
    QVERIFY(!f.window.actionBar()->renameButton()->isEnabled());
    QCOMPARE(f.child<QLabel>("renameReason")->text(), QStringLiteral("Add files to start."));
    QVERIFY(f.child<QPushButton>("emptyAddFiles")->isDefault());
    QVERIFY(!f.window.rulePanel()->addRuleButton()->property("primary").toBool());
}

void TestMainWindow::addRulePrimaryWithItems()
{
    Fixture f;
    f.window.show();
    f.addFiles({QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    QVERIFY(!f.shown(f.child<EmptyState>("emptyState")));
    QVERIFY(f.shown(f.window.tableView()));
    QVERIFY(f.window.rulePanel()->addRuleButton()->property("primary").toBool());
    QVERIFY(!f.window.actionBar()->renameButton()->isDefault());
    QCOMPARE(f.child<QLabel>("renameReason")->text(),
             QStringLiteral("Add a rule to change the names."));
}

void TestMainWindow::prefixFlowUnder60s()
{
    Fixture f;
    f.window.show();
    QStringList names;
    for (int i = 0; i < 100; ++i)
        names.append(QStringLiteral("photo%1.jpg").arg(i, 3, 10, QLatin1Char('0')));
    const QStringList paths = f.files(names);

    QElapsedTimer clock;
    clock.start();
    f.window.itemAdder().addPaths(paths);
    f.addRuleAction(QStringLiteral("insert"))->trigger();
    auto *text =
            f.window.rulePanel()->currentEditor()->findChild<QLineEdit *>(QStringLiteral("text"));
    QVERIFY(text);
    QTest::keyClicks(text, QStringLiteral("x_"));
    QTRY_COMPARE(f.window.actionBar()->renameButton()->text(), QStringLiteral("Rename 100 Files"));
    QVERIFY(clock.elapsed() < 60'000);
    QVERIFY(f.window.actionBar()->renameButton()->isEnabled());
    QVERIFY(f.window.actionBar()->renameButton()->isDefault());
    QVERIFY(f.window.previewIsCurrent());
    QCOMPARE(f.newNames().first(), QStringLiteral("x_photo000.jpg"));
    QVERIFY(!f.window.rulePanel()->addRuleButton()->property("primary").toBool());
}

void TestMainWindow::moveRowUpdatesNumbering()
{
    Fixture f;
    f.addFiles({QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt"),
                QStringLiteral("d.txt")});
    f.window.ruleModel().insertRule(0, std::make_unique<NumberingRule>());
    QCOMPARE(f.newNames(), QStringList({QStringLiteral("a_1.txt"), QStringLiteral("b_2.txt"),
                                        QStringLiteral("c_3.txt"), QStringLiteral("d_4.txt")}));
    // Drag row 3 to row 0.
    ItemTableModel &model = f.window.itemModel();
    QMimeData *mime = model.mimeData({model.index(3, ItemTableModel::OldName)});
    // The model moves the rows and returns false, so the view does not remove them.
    QVERIFY(!model.dropMimeData(mime, Qt::MoveAction, 0, 0, {}));
    delete mime;
    QCOMPARE(f.oldNames().first(), QStringLiteral("d.txt"));
    QCOMPARE(f.newNames(), QStringList({QStringLiteral("d_1.txt"), QStringLiteral("a_2.txt"),
                                        QStringLiteral("b_3.txt"), QStringLiteral("c_4.txt")}));
}

void TestMainWindow::sortByNewNameStable()
{
    Fixture f;
    f.addFiles({QStringLiteral("c.txt"), QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    f.window.ruleModel().insertRule(0, std::make_unique<NumberingRule>());
    QCOMPARE(f.newNames(), QStringList({QStringLiteral("c_1.txt"), QStringLiteral("a_2.txt"),
                                        QStringLiteral("b_3.txt")}));
    f.window.itemModel().sortOnce(ItemTableModel::NewName, Qt::AscendingOrder);
    // The sort is one time only: the new preview renumbers, but the order stays.
    QCOMPARE(f.oldNames(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                                        QStringLiteral("c.txt")}));
    QCOMPARE(f.newNames(), QStringList({QStringLiteral("a_1.txt"), QStringLiteral("b_2.txt"),
                                        QStringLiteral("c_3.txt")}));
}

void TestMainWindow::filterChangedOnly()
{
    Fixture f;
    f.addFiles({QStringLiteral("apple.txt"), QStringLiteral("berry.txt")});
    auto rule = std::make_unique<ReplaceRule>();
    rule->find = QStringLiteral("apple");
    rule->replace = QStringLiteral("pear");
    f.window.ruleModel().insertRule(0, std::move(rule));
    QAbstractItemModel *shown = f.window.tableView()->model();
    QCOMPARE(shown->rowCount(), 2);

    f.window.actions().showChangedOnly->trigger();
    QCOMPARE(shown->rowCount(), 1);
    QCOMPARE(shown->index(0, ItemTableModel::NewName).data().toString(),
             QStringLiteral("pear.txt"));
    auto *combo = f.child<QComboBox>("showCombo");
    QCOMPARE(combo->currentText(), QStringLiteral("Changed items"));

    combo->setCurrentIndex(0);
    QCOMPARE(shown->rowCount(), 2);
    QVERIFY(f.window.actions().showAllItems->isChecked());

    f.window.actions().showProblemsOnly->trigger();
    QCOMPARE(shown->rowCount(), 0);
    QCOMPARE(combo->currentText(), QStringLiteral("Problems"));
}

void TestMainWindow::filterEmptyText()
{
    Fixture f;
    f.window.show();
    f.addFiles({QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    auto *field = f.child<QLineEdit>("filterField");
    auto *combo = f.child<QComboBox>("showCombo");
    auto *noMatch = f.child<QLabel>("noMatchText");
    auto *clear = f.child<QLabel>("clearFilterLink");
    QVERIFY(field && combo && noMatch && clear);
    QVERIFY(!f.shown(noMatch));

    f.window.actions().find->trigger();
    QCOMPARE(f.window.focusWidget(), field);

    combo->setCurrentIndex(1);
    field->setText(QStringLiteral("zzz"));
    QVERIFY(f.shown(noMatch));
    QCOMPARE(noMatch->text(), QStringLiteral("No items match the filter."));
    QVERIFY(clear->text().contains(QStringLiteral("Clear Filter")));
    QVERIFY(!f.shown(f.window.tableView()));

    emit clear->linkActivated(QStringLiteral("clear"));
    QVERIFY(field->text().isEmpty());
    QCOMPARE(combo->currentIndex(), 0);
    QVERIFY(!f.shown(noMatch));
    QVERIFY(f.shown(f.window.tableView()));
    QCOMPARE(f.window.tableView()->model()->rowCount(), 2);
}

void TestMainWindow::blockingReasonTooltip()
{
    Fixture f;
    f.window.show();
    f.addFiles({QStringLiteral("x1.txt"), QStringLiteral("x2.txt")});
    f.window.ruleModel().insertRule(0, regexRemoveDigits());
    QCOMPARE(f.window.currentPreview().items.first().status, Status::Conflict);
    const QModelIndex index = f.window.tableView()->model()->index(0, ItemTableModel::OldName);
    QVERIFY(index.data(Qt::ToolTipRole).toString().contains(QStringLiteral("Same new name as")));
    QVERIFY(!f.window.actionBar()->renameButton()->isEnabled());
    QVERIFY(f.child<QLabel>("renameReason")->text().contains(QStringLiteral("block")));
}

void TestMainWindow::refreshDetectsMissing()
{
    Fixture f;
    const QStringList paths = f.files({QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    f.window.itemAdder().addPaths(paths);
    QCOMPARE(f.window.currentPreview().items.at(1).status, Status::Unchanged);
    QVERIFY(QFile::remove(paths.at(1)));
    f.window.actions().refresh->trigger();
    QCOMPARE(f.window.currentPreview().items.at(0).status, Status::Unchanged);
    QCOMPARE(f.window.currentPreview().items.at(1).status, Status::Missing);
}

void TestMainWindow::addReadsFolderAgain()
{
    Fixture f;
    f.addFiles({QStringLiteral("a1.txt")}); // the folder is now in the cache
    f.window.ruleModel().insertRule(0, regexRemoveDigits());
    QCOMPARE(f.window.currentPreview().summary.conflicts, 0);

    // Files made after the folder was read: a new item is not Missing, and a file on disk with
    // the new name of an item is a conflict.
    f.tree.file(QStringLiteral("a.txt"), "x");
    f.addFiles({QStringLiteral("b.txt")});
    QCOMPARE(f.window.currentPreview().items.at(1).status, Status::Unchanged);
    QCOMPARE(f.window.currentPreview().items.at(0).status, Status::Conflict);
}

void TestMainWindow::activationReadsFolderAgain()
{
    Fixture f;
    f.window.show();
    f.window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.addFiles({QStringLiteral("c1.txt")});
    f.window.ruleModel().insertRule(0, regexRemoveDigits());
    QCOMPARE(f.window.currentPreview().summary.conflicts, 0);

    // The user makes c.txt in another app, then comes back to CTRM.
    QWidget other;
    other.show();
    other.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&other));
    QTRY_VERIFY(!f.window.isActiveWindow());
    f.tree.file(QStringLiteral("c.txt"), "x");
    f.window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    QTRY_COMPARE(f.window.currentPreview().items.at(0).status, Status::Conflict);
}

void TestMainWindow::conflictTipShown()
{
    Fixture f;
    f.window.show();
    f.addFiles({QStringLiteral("x1.txt"), QStringLiteral("x2.txt")});
    auto *tip = f.child<MessageBar>("conflictTip");
    QVERIFY(tip);
    QVERIFY(!f.shown(tip));
    f.window.ruleModel().insertRule(0, regexRemoveDigits());
    QVERIFY(f.shown(tip));
    QCOMPARE(tip->findChild<QLabel *>(QStringLiteral("messageBarText"))->text(),
             QStringLiteral("Tip: names are the same. Add a Numbering rule, or turn on \"Add a "
                            "number to duplicate names\"."));
    QPushButton *add = nullptr;
    for (QPushButton *b : tip->findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("Add Numbering"))
            add = b;
    QVERIFY(add);
    QVERIFY(!add->isDefault());
    add->click();
    QCOMPARE(f.window.ruleModel().rowCount(), 2);
    QCOMPARE(f.window.ruleModel().chain().at(1)->type(), QStringLiteral("numbering"));
    QCOMPARE(f.window.currentPreview().summary.conflicts, 0);
    QVERIFY(!f.shown(tip));

    // A conflict with a name on disk is not a duplicate in the batch: no tip.
    f.window.ruleModel().removeRule(1);
    QVERIFY(f.shown(tip));
    f.tree.file(QStringLiteral("y.txt"), "x");
    f.addFiles({QStringLiteral("y1.txt")});
    f.window.actions().refresh->trigger(); // the folder cache reads y.txt
    QCOMPARE(f.window.currentPreview().summary.conflicts, 3);
    QVERIFY(!f.shown(tip));
}

void TestMainWindow::tabOrder()
{
    Fixture f;
    f.window.show();
    f.addFiles({QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    f.addRuleAction(QStringLiteral("insert"))->trigger();
    auto *text =
            f.window.rulePanel()->currentEditor()->findChild<QLineEdit *>(QStringLiteral("text"));
    QVERIFY(text);

    // ui-design 14: toolbar, flow strip, rule list, rule editor, chain options, filter, table,
    // action bar.
    const QList<QWidget *> expected{f.child<FlowStrip>("flowStrip"),
                                    f.window.rulePanel()->ruleList(),
                                    text,
                                    f.child<QToolButton>("chainOptionsHeader"),
                                    f.child<QLineEdit>("filterField"),
                                    f.child<QComboBox>("showCombo"),
                                    f.window.tableView(),
                                    f.window.actionBar()->renameButton()};
    for (QWidget *w : expected)
        QVERIFY(w);

    QList<QWidget *> chain;
    QWidget *w = &f.window;
    do {
        if ((w->focusPolicy() & Qt::TabFocus) && w->isVisibleTo(&f.window))
            chain.append(w);
        w = w->nextInFocusChain();
    } while (w && w != &f.window);

    qsizetype last = -1;
    for (QWidget *e : expected) {
        const qsizetype at = chain.indexOf(e);
        QVERIFY2(at > last, qPrintable(e->objectName()));
        last = at;
    }
    // Toolbar widgets come before the flow strip.
    for (QWidget *t : f.child<QToolBar>("mainToolBar")->findChildren<QWidget *>()) {
        const qsizetype at = chain.indexOf(t);
        if (at >= 0)
            QVERIFY(at < chain.indexOf(expected.first()));
    }
}

void TestMainWindow::presetTitle()
{
    Fixture f;
    QCOMPARE(f.window.windowTitle(), QStringLiteral("CTRM"));
    f.window.setPresetTitle(QStringLiteral("Photo Trip"), true);
    QCOMPARE(f.window.windowTitle(), QStringLiteral("CTRM - Photo Trip[*]"));
    QVERIFY(f.window.isWindowModified());
    f.window.setPresetTitle(QString(), false);
    QCOMPARE(f.window.windowTitle(), QStringLiteral("CTRM"));
    QVERIFY(!f.window.isWindowModified());
}

void TestMainWindow::listActionsAtStart()
{
    Fixture f;
    const Actions &a = f.window.actions();
    for (QAction *action :
         {a.removeFromList, a.include, a.exclude, a.clearList, a.refresh, a.clearRules})
        QVERIFY2(!action->isEnabled(), qPrintable(action->objectName()));
    f.addFiles({QStringLiteral("a.txt")});
    QVERIFY(a.clearList->isEnabled());
    QVERIFY(a.refresh->isEnabled());
}

void TestMainWindow::pendingPreviewBlocksRename()
{
    // More than kWorkerThreshold items: the preview runs on the worker thread.
    Fixture f;
    QStringList names;
    for (int i = 0; i <= PreviewScheduler::kWorkerThreshold; ++i)
        names.append(QStringLiteral("f%1.txt").arg(i));
    f.addFiles(names);
    auto rule = std::make_unique<InsertRule>();
    rule->text = QStringLiteral("x_");
    f.window.ruleModel().insertRule(0, std::move(rule));
    QTRY_VERIFY_WITH_TIMEOUT(f.window.previewIsCurrent(), 30'000);
    QVERIFY(f.window.actionBar()->renameButton()->isEnabled());

    f.window.ruleModel().ruleChanged(0);
    QVERIFY(!f.window.previewIsCurrent());
    QVERIFY(!f.window.actionBar()->renameButton()->isEnabled());
    QVERIFY(!f.window.actionBar()->renameButton()->isDefault());
    QCOMPARE(f.child<QLabel>("renameReason")->text(), QStringLiteral("Checking the new names..."));
    QTRY_VERIFY_WITH_TIMEOUT(f.window.previewIsCurrent(), 30'000);
    QVERIFY(f.window.actionBar()->renameButton()->isEnabled());
    QVERIFY(f.window.actionBar()->renameButton()->isDefault());
}

void TestMainWindow::toolbarIcons()
{
    // D3: each toolbar button has an icon, and the icon follows the palette.
    Fixture f;
    auto *bar = f.child<QToolBar>("mainToolBar");
    QList<QToolButton *> buttons;
    for (QAction *action : bar->actions()) {
        if (auto *b = qobject_cast<QToolButton *>(bar->widgetForAction(action)))
            buttons.append(b);
    }
    QCOMPARE(buttons.size(), 8);
    QCOMPARE(bar->iconSize(), QSize(16, 16));
    QVERIFY(buttons.contains(f.child<QToolButton>("toolBarPresets")));
    QHash<QToolButton *, qint64> keys;
    for (QToolButton *b : std::as_const(buttons)) {
        QVERIFY2(!b->icon().isNull(), qPrintable(b->text()));
        keys.insert(b, b->icon().cacheKey());
    }
    QVERIFY(f.child<QToolButton>("toolBarPresets")->menu());
    QPalette p = f.window.palette();
    p.setColor(QPalette::WindowText, QColor(0x12, 0x34, 0x56));
    f.window.setPalette(p);
    for (QToolButton *b : std::as_const(buttons))
        QVERIFY2(b->icon().cacheKey() != keys.value(b), qPrintable(b->text()));
    // The menu bar title keeps its text: the Presets menu action has no icon.
    for (QAction *menu : f.window.menuBar()->actions())
        QVERIFY(menu->icon().isNull());
}

void TestMainWindow::tertiaryButtonsHaveNoFrame()
{
    // D4: the tertiary buttons outside the toolbar draw no frame until hover.
    Fixture f;
    for (const char *name : {"moveUpButton", "moveDownButton", "duplicateButton", "deleteButton",
                             "previousProblemButton", "nextProblemButton", "messageBarClose",
                             "chainOptionsHeader", "flowStep1", "flowStep4"}) {
        auto *b = f.window.findChild<QToolButton *>(QString::fromLatin1(name));
        QVERIFY2(b, name);
        QVERIFY2(qobject_cast<TertiaryButton *>(b), name);
    }
}

void TestMainWindow::ruleMenuButtonsArePushButtons()
{
    // D5: Add Rule and Presets are push buttons with a menu (the style draws the indicator).
    Fixture f;
    QPushButton *add = f.window.rulePanel()->addRuleButton();
    QPushButton *presets = f.window.rulePanel()->presetsButton();
    QVERIFY(add->menu());
    QVERIFY(presets->menu());
    QVERIFY(!add->autoDefault());
    QVERIFY(!presets->autoDefault());
}

QTEST_MAIN(TestMainWindow)
#include "tst_app_mainwindow.moc"
