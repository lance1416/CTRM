// SPDX-License-Identifier: GPL-3.0-or-later
#include "actions.h"
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
#include "rules/chainoptionswidget.h"
#include "rules/insertrule.h"
#include "rules/numberingrule.h"
#include "rules/replacerule.h"
#include "temptree.h"
#include "theme/keyboardmode.h"
#include "theme/tokens.h"
#include "ui/windowstate.h"
#include "widgets/actionbar.h"
#include "widgets/emptystate.h"
#include "widgets/flowstrip.h"
#include "widgets/itemtableview.h"
#include "widgets/menutoolbutton.h"
#include "widgets/messagebar.h"
#include "widgets/rulepanel.h"
#include "widgets/tertiarybutton.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
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
    void programFocusKeepsKeyboardModeOff();
    void focusShortcutsTurnKeyboardModeOn();
    void contextStores();
    void editorGetsHeightAtMinimum();
    void listHeightStableOnSelect();
    void listWholeRows();
    void chainOptionsScrollsIntoViewAtSmallWindow();
    void rulePanelWidthStable();
    void editorPagesFitAtDefault();
    void splitterDoubleClickRefits();
    void splitterHandlesPlain();
    void focusEditorFirstField();
    void emptyStateShown();
    void addRulePrimaryWithItems();
    void prefixFlowUnder60s();
    void moveRowUpdatesNumbering();
    void sortByNewNameStable();
    void filterChangedOnly();
    void filterEmptyText();
    void filterBarSameHeight();
    void barsShareLeftEdge();
    void blockingReasonTooltip();
    void refreshDetectsMissing();
    void addReadsFolderAgain();
    void activationReadsFolderAgain();
    void conflictTipShown();
    void conflictTipNarrow();
    void flowStepsWhenBlocked();
    void conflictInActiveWindow();
    void activationWhileApplyingPreview();
    void tabOrder();
    void problemActionsNeedProblems();
    void noEmptyStatusBar();
    void flowStripIsThin();
    void clearListUndo();
    void removeManyUndo();
    void presetTitle();
    void listActionsAtStart();
    void pendingPreviewBlocksRename();
    void viewShowColumns();
    void tableShortcutsOnlyWithFocus();
    void toolbarPresetsArrowOnTextLine();
    void toolbarIcons();
    void toolbarSeparatorSolid();
    void tertiaryButtonsHaveNoFrame();
    void ruleMenuButtonsArePushButtons();
    void namesShownAtMinimumWidth();
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
    // S11, ui-design 9.3: "Same names:" and a link-style Add Numbering in the reason area of the
    // action bar. No bar, no layout shift, no close button.
    Fixture f;
    f.window.show();
    f.addFiles({QStringLiteral("x1.txt"), QStringLiteral("x2.txt")});
    QVERIFY(!f.child<MessageBar>("conflictTip"));
    auto *tip = f.child<QLabel>("conflictTipText");
    auto *add = f.child<QToolButton>("addNumberingButton");
    QVERIFY(tip && add);
    QVERIFY(qobject_cast<TertiaryButton *>(add));
    QVERIFY(f.window.actionBar()->isAncestorOf(tip));
    QVERIFY(f.window.actionBar()->isAncestorOf(add));
    QVERIFY(!f.shown(tip));
    QVERIFY(!f.shown(add));
    f.window.ruleModel().insertRule(0, regexRemoveDigits());
    QVERIFY(f.shown(tip));
    QVERIFY(f.shown(add));
    QVERIFY(f.shown(f.child<QLabel>("renameReason"))); // the blocked reason stays
    QCOMPARE(tip->text(), QStringLiteral("Same names:"));
    QCOMPARE(tip->toolTip(),
             QStringLiteral("The new names are the same. Add a Numbering rule, or turn on \"Add "
                            "a number to duplicate names\" in Chain options."));
    QCOMPARE(add->text(), QStringLiteral("Add Numbering"));

    // The tip does not change the height of the bar or the place of the table.
    QCoreApplication::processEvents();
    const int barHeight = f.window.actionBar()->height();
    const QRect tableRect = f.window.tableSlot()->geometry();
    f.window.actionBar()->setConflictTip(false);
    QCoreApplication::processEvents();
    QCOMPARE(f.window.actionBar()->height(), barHeight);
    QCOMPARE(f.window.tableSlot()->geometry(), tableRect);
    f.window.actionBar()->setConflictTip(true);

    add->click();
    QCOMPARE(f.window.ruleModel().rowCount(), 2);
    QCOMPARE(f.window.ruleModel().chain().at(1)->type(), QStringLiteral("numbering"));
    QCOMPARE(f.window.currentPreview().summary.conflicts, 0);
    QVERIFY(!f.shown(tip));
    QVERIFY(!f.shown(add));

    // A Numbering rule that is off: the link turns it on.
    f.window.ruleModel().setEnabled(1, false);
    QVERIFY(f.shown(add));
    QCOMPARE(add->text(), QStringLiteral("Turn On Rule 2"));
    add->click();
    QCOMPARE(f.window.ruleModel().rowCount(), 2);
    QVERIFY(f.window.ruleModel().chain().at(1)->enabled);
    QVERIFY(!f.shown(add));

    // A conflict with a name on disk is not a duplicate in the batch: no tip.
    f.window.ruleModel().removeRule(1);
    QVERIFY(f.shown(tip));
    f.tree.file(QStringLiteral("y.txt"), "x");
    f.addFiles({QStringLiteral("y1.txt")});
    f.window.actions().refresh->trigger(); // the folder cache reads y.txt
    QCOMPARE(f.window.currentPreview().summary.conflicts, 3);
    QVERIFY(!f.shown(tip));
}

void TestMainWindow::flowStepsWhenBlocked()
{
    // S01, ui-design 5.3: blocked rows with new names make step 2 done, so step 3 is current.
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.addFiles({QStringLiteral("x1.txt"), QStringLiteral("x2.txt")});
    f.window.ruleModel().insertRule(0, regexRemoveDigits());
    QCOMPARE(f.window.currentPreview().summary.willChange, 0);
    QCOMPARE(f.window.currentPreview().summary.conflicts, 2);
    QCOMPARE(f.child<FlowStrip>("flowStrip")->accessibleDescription(),
             QStringLiteral("Step 3 of 3, Rename, current"));
}

void TestMainWindow::conflictTipNarrow()
{
    // ui-design 9.3: when the action bar is too narrow, the tip text hides first.
    QWidget parent;
    const Actions actions = createActions(&parent);
    ActionBar bar(actions, &parent);
    PreviewSummary s;
    s.willChange = 2;
    s.conflicts = 2;
    WindowStateInput in;
    in.itemCount = 4;
    in.enabledRuleCount = 1;
    in.summary = s;
    bar.setState(computeWindowState(in), s);
    bar.setConflictTip(true);
    parent.resize(2100, 200);
    bar.setGeometry(0, 0, 2000, bar.sizeHint().height());
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    auto *tip = bar.findChild<QLabel *>(QStringLiteral("conflictTipText"));
    auto *add = bar.findChild<QToolButton *>(QStringLiteral("addNumberingButton"));
    QVERIFY(tip->isVisible() && add->isVisible());
    bar.resize(bar.minimumSizeHint().width() - tip->sizeHint().width(), bar.height());
    QTRY_VERIFY(!tip->isVisible());
    QVERIFY(add->isVisible());
    bar.resize(2000, bar.height());
    QTRY_VERIFY(tip->isVisible());
}

void TestMainWindow::conflictInActiveWindow()
{
    // M01: the conflict tip in an active window. Showing the tip changed the activation, and the
    // activation made a new preview while the window applied a preview (stack overflow).
    Fixture f;
    f.window.show();
    f.window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.addFiles({QStringLiteral("x1.txt"), QStringLiteral("x2.txt"), QStringLiteral("y.txt")});
    f.window.ruleModel().insertRule(0, regexRemoveDigits());
    QCOMPARE(f.window.currentPreview().summary.conflicts, 2);
    QTest::qWait(50);
    QVERIFY(f.window.isActiveWindow());
    QCOMPARE(f.window.currentPreview().summary.conflicts, 2);
}

void TestMainWindow::activationWhileApplyingPreview()
{
    // M01: an activation while the window applies a preview does not apply a preview inside it.
    Fixture f;
    f.window.show();
    f.window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.addFiles({QStringLiteral("a1.txt"), QStringLiteral("a2.txt")});
    f.window.ruleModel().insertRule(0, regexRemoveDigits());
    int depth = 0;
    int maxDepth = 0;
    connect(&f.window.itemModel(), &QAbstractItemModel::dataChanged, &f.window, [&] {
        ++depth;
        maxDepth = qMax(maxDepth, depth);
        if (depth < 5) {
            QEvent activation(QEvent::ActivationChange);
            QCoreApplication::sendEvent(&f.window, &activation);
        }
        --depth;
    });
    f.window.requestPreview(); // only the preview changes the table
    QCOMPARE(maxDepth, 1);
    // The activation reads the folders again after the preview.
    QTRY_VERIFY(f.window.previewIsCurrent());
    QCOMPARE(f.window.currentPreview().summary.conflicts, 2);
}

void TestMainWindow::problemActionsNeedProblems()
{
    // M27: Next and Previous Problem only when there are problems (ui-design 5.5).
    Fixture f;
    f.addFiles({QStringLiteral("x1.txt"), QStringLiteral("x2.txt")});
    QVERIFY(!f.window.actions().nextProblem->isEnabled());
    QVERIFY(!f.window.actions().previousProblem->isEnabled());
    f.window.ruleModel().insertRule(0, regexRemoveDigits());
    QVERIFY(f.window.currentPreview().summary.conflicts > 0);
    QVERIFY(f.window.actions().nextProblem->isEnabled());
    QVERIFY(f.window.actions().previousProblem->isEnabled());
    f.window.ruleModel().removeRule(0);
    QVERIFY(!f.window.actions().nextProblem->isEnabled());
    QVERIFY(!f.window.actions().previousProblem->isEnabled());
}

void TestMainWindow::noEmptyStatusBar()
{
    // M21, O14: the action bar is the last row. There is no status bar; a short message goes
    // to the message bar (ui-design 9.13).
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QVERIFY(f.window.findChildren<QStatusBar *>().isEmpty());
    f.addFiles({QStringLiteral("a.txt")});
    f.addFiles({QStringLiteral("a.txt")}); // already in the list: a short message
    QVERIFY(f.window.findChildren<QStatusBar *>().isEmpty());
    QVERIFY(f.window.messageBar()->isVisible());
    QVERIFY(f.window.messageBar()->isNote());
    QCOMPARE(f.window.messageBar()->findChild<QLabel *>(QStringLiteral("messageBarText"))->text(),
             QStringLiteral("1 item was already in the list."));
}

void TestMainWindow::flowStripIsThin()
{
    // M22, O01: one row of steps, inset space-1 above and below, a separator under it.
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QWidget *slot = f.window.flowStripSlot();
    auto *step = f.child<QLabel>("flowStep1");
    QVERIFY(f.child<QFrame>("flowStripSeparator"));
    QVERIFY(slot->isAncestorOf(f.child<QFrame>("flowStripSeparator")));
    QVERIFY2(slot->height() <= step->height() + 2 * tokens::space1 + 1,
             qPrintable(QStringLiteral("%1 %2").arg(slot->height()).arg(step->height())));
}

void TestMainWindow::clearListUndo()
{
    // S07: Clear List has no question. The message bar has Undo, which puts the same items back
    // in the same order with the same Include states.
    Fixture f;
    f.window.show();
    f.addFiles({QStringLiteral("c.txt"), QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    const ItemId a = f.window.itemModel().items().at(1).id;
    f.window.itemModel().setIncluded({a}, false);
    f.window.actions().clearList->trigger();
    QCOMPARE(f.window.itemModel().rowCount(), 0);
    MessageBar *bar = f.window.messageBar();
    QVERIFY(f.shown(bar));
    QCOMPARE(bar->findChild<QLabel *>(QStringLiteral("messageBarText"))->text(),
             QStringLiteral("Cleared 3 items."));
    auto *undo = bar->findChild<QPushButton *>(QStringLiteral("clearListUndo"));
    QVERIFY(undo);
    QCOMPARE(undo->text(), QStringLiteral("Undo"));
    QVERIFY(!undo->isDefault());
    undo->click();
    QCOMPARE(f.oldNames(), QStringList({QStringLiteral("c.txt"), QStringLiteral("a.txt"),
                                        QStringLiteral("b.txt")}));
    const QList<Item> items = f.window.itemModel().items();
    QCOMPARE(items.at(0).included, true);
    QCOMPARE(items.at(1).included, false);
    QCOMPARE(items.at(2).included, true);
    QVERIFY(!f.shown(bar));

    // The bar closes at the next list change.
    f.window.actions().clearList->trigger();
    QVERIFY(f.shown(bar));
    f.addFiles({QStringLiteral("d.txt")});
    QVERIFY(!f.shown(bar));
}

void TestMainWindow::removeManyUndo()
{
    // ui-design 9.12: Remove from List of 10 or more items shows "Removed N items." with Undo.
    // Undo puts the items back at their places with the same Include states.
    Fixture f;
    f.window.show();
    QStringList names;
    for (int i = 0; i < 12; ++i)
        names.append(QStringLiteral("f%1.txt").arg(i, 2, 10, QLatin1Char('0')));
    f.addFiles(names);
    const QList<Item> before = f.window.itemModel().items();
    f.window.itemModel().setIncluded({before.at(5).id}, false);
    auto *table = f.child<ItemTableView>("itemTable");
    MessageBar *bar = f.window.messageBar();

    // Fewer than 10 items: no message.
    table->selectRow(0);
    f.window.actions().removeFromList->trigger();
    QCOMPARE(f.window.itemModel().rowCount(), 11);
    QVERIFY(!f.shown(bar));

    // Rows 1 to 10 of the 11 (f01 to f10).
    table->clearSelection();
    const QModelIndex first = table->model()->index(0, 0);
    const QModelIndex last = table->model()->index(9, 0);
    table->selectionModel()->select(QItemSelection(first, last),
                                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
    f.window.actions().removeFromList->trigger();
    QCOMPARE(f.oldNames(), QStringList{QStringLiteral("f11.txt")});
    QVERIFY(f.shown(bar));
    QCOMPARE(bar->findChild<QLabel *>(QStringLiteral("messageBarText"))->text(),
             QStringLiteral("Removed 10 items."));
    auto *undo = bar->findChild<QPushButton *>(QStringLiteral("removeUndo"));
    QVERIFY(undo);
    QVERIFY(!undo->isDefault());
    undo->click();
    QCOMPARE(f.oldNames(), names.mid(1));
    QCOMPARE(f.window.itemModel().items().at(4).included, false);
    QVERIFY(!f.shown(bar));
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

    // ui-design 14, O03b: toolbar, rule list, rule editor, chain options, filter, table, action
    // bar. The flow strip is not a stop.
    QCOMPARE(f.child<FlowStrip>("flowStrip")->focusPolicy(), Qt::NoFocus);
    const QList<QWidget *> expected{f.window.rulePanel()->ruleList(),
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
    // M09: the editor scroll area is no focus stop (it shows no focus); its fields are.
    QVERIFY(!chain.contains(f.child<QWidget>("editorScroll")));
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
    // C07: the icon-only buttons are hit-min squares.
    for (QToolButton *b : std::as_const(buttons)) {
        if (b->toolButtonStyle() == Qt::ToolButtonIconOnly)
            QCOMPARE(b->size(), QSize(tokens::hitMin, tokens::hitMin));
    }
    QPalette p = f.window.palette();
    p.setColor(QPalette::WindowText, QColor(0x12, 0x34, 0x56));
    f.window.setPalette(p);
    for (QToolButton *b : std::as_const(buttons))
        QVERIFY2(b->icon().cacheKey() != keys.value(b), qPrintable(b->text()));
    // The menu bar title keeps its text: the Presets menu action has no icon.
    for (QAction *menu : f.window.menuBar()->actions())
        QVERIFY(menu->icon().isNull());
}

void TestMainWindow::toolbarSeparatorSolid()
{
    // R5-10: one solid 1 px line in the separator colour, 16 px high, between Refresh and Undo.
    // No native (dotted) separator.
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    auto *bar = f.child<QToolBar>("mainToolBar");
    for (QAction *action : bar->actions())
        QVERIFY(!action->isSeparator());
    auto *separator = f.child<QFrame>("toolBarSeparator");
    QVERIFY(separator);
    QCOMPARE(separator->height(), tokens::iconS);
    const QWidget *refresh = nullptr;
    const QWidget *undo = nullptr;
    for (QAction *action : bar->actions()) {
        if (action->objectName() == QLatin1StringView("refresh"))
            refresh = bar->widgetForAction(action);
        if (action->objectName() == QLatin1StringView("undoRename"))
            undo = bar->widgetForAction(action);
    }
    QVERIFY(refresh && undo);
    QVERIFY(refresh->geometry().right() < separator->x());
    QVERIFY(separator->geometry().right() < undo->x());

    const QColor background(0, 0, 0);
    QImage image(separator->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(background);
    separator->render(&image, {}, {}, QWidget::DrawChildren);
    const QRgb line = separator->palette().color(QPalette::Mid).rgb();
    int lineColumns = 0;
    for (int x = 0; x < image.width(); ++x) {
        int rows = 0;
        for (int y = 0; y < image.height(); ++y)
            rows += image.pixel(x, y) == line ? 1 : 0;
        if (rows == 0)
            continue;
        QCOMPARE(rows, image.height()); // solid, not dotted
        ++lineColumns;
    }
    QCOMPARE(lineColumns, 1);
}

void TestMainWindow::tertiaryButtonsHaveNoFrame()
{
    // D4: the tertiary buttons outside the toolbar draw no frame until hover.
    Fixture f;
    for (const char *name :
         {"moveUpButton", "moveDownButton", "duplicateButton", "deleteButton",
          "previousProblemButton", "nextProblemButton", "messageBarClose", "chainOptionsHeader"}) {
        auto *b = f.window.findChild<QToolButton *>(QString::fromLatin1(name));
        QVERIFY2(b, name);
        QVERIFY2(qobject_cast<TertiaryButton *>(b), name);
    }
}

void TestMainWindow::ruleMenuButtonsArePushButtons()
{
    // D5: Add Rule is a push button with a menu (the style draws the indicator).
    Fixture f;
    QPushButton *add = f.window.rulePanel()->addRuleButton();
    QVERIFY(add->menu());
    QVERIFY(!add->autoDefault());
}

void TestMainWindow::filterBarSameHeight()
{
    // C05: the filter field gets its height from the style (control-h with the app style), not
    // from the combo box. Both are centred on the header row.
    Fixture f;
    f.window.show();
    f.addFiles({QStringLiteral("a.txt")});
    auto *field = f.child<QLineEdit>("filterField");
    auto *combo = f.child<QComboBox>("showCombo");
    QVERIFY(field && combo);
    QApplication::processEvents();
    QCOMPARE(field->minimumHeight(), 0);
    QCOMPARE(field->maximumHeight(), QWIDGETSIZE_MAX);
    QCOMPARE(field->height(), field->sizeHint().height());
    auto *filterBar = f.child<QWidget>("filterBar");
    const int rowCentre = tokens::space3 + tokens::headerH / 2;
    QVERIFY(qAbs(field->geometry().center().y() + 1 - rowCentre) <= 1);
    QVERIFY(qAbs(combo->mapTo(filterBar, combo->rect().center()).y() + 1 - rowCentre) <= 1);
}

void TestMainWindow::barsShareLeftEdge()
{
    // O07, design-tokens 4.2: the first item of each bar starts at x = space-3. The filter row
    // is space-3 + header-h + space-2 high (C08).
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    f.addFiles({QStringLiteral("a.txt")});
    f.window.messageBar()->showMessage(StateIcon::Done, QStringLiteral("Done."), {});
    QApplication::processEvents();
    const auto leftIn = [](QWidget *child, QWidget *bar) {
        return child->mapTo(bar, QPoint(0, 0)).x();
    };
    auto *strip = f.child<FlowStrip>("flowStrip");
    QCOMPARE(leftIn(f.child<QLabel>("flowStep1Icon"), strip), tokens::space3);
    auto *bar = f.window.messageBar();
    QCOMPARE(leftIn(f.child<QLabel>("messageBarIcon"), bar), tokens::space3);
    QCOMPARE(bar->height(), tokens::space2 * 2 + 1 + f.child<QLabel>("messageBarText")->height());
    ActionBar *actionBar = f.window.actionBar();
    QCOMPARE(leftIn(f.child<QWidget>("countSummary"), actionBar), tokens::space3);
    QCOMPARE(actionBar->layout()->contentsMargins(),
             QMargins(tokens::space3, tokens::space3, tokens::space3, tokens::space3));
    auto *filterBar = f.child<QWidget>("filterBar");
    QVERIFY(filterBar);
    QCOMPARE(filterBar->height(), tokens::space3 + tokens::headerH + tokens::space2);
    QCOMPARE(filterBar->layout()->contentsMargins().left(), tokens::space3);
    QCOMPARE(f.window.tableView()->mapTo(f.child<QWidget>("tablePage"), QPoint(0, 0)).y(),
             filterBar->height());
}

void TestMainWindow::editorGetsHeightAtMinimum()
{
    // M06: at the minimum window the rule list gives its empty rows to the editor, and the
    // editor scrolls only over the current page and the closed Chain options (O13).
    Fixture f;
    f.window.show();
    f.window.resize(f.window.minimumSize());
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    f.addRuleAction(QStringLiteral("replace"))->trigger();
    QApplication::processEvents();
    auto *scroll = f.child<QScrollArea>("editorScroll");
    auto *list = f.child<QListView>("ruleList");
    QVERIFY(scroll && list);
    const int twoRows = 2 * list->sizeHintForRow(0) + 2 * list->frameWidth(); // R6-06
    const int page = scroll->widget()->sizeHint().height();
    QVERIFY2(page < 300, qPrintable(QString::number(page))); // the Replace page, not Numbering
    // The editor fits, or the list has no empty row.
    QTRY_VERIFY2(scroll->viewport()->height() >= page || list->height() == twoRows,
                 qPrintable(QStringLiteral("list %1, editor %2 of %3")
                                    .arg(list->height())
                                    .arg(scroll->viewport()->height())
                                    .arg(page)));
    QCOMPARE(scroll->widget()->height(), std::max(page, scroll->viewport()->height()));

    // A high window: the list has its 3 rows again.
    f.window.resize(f.window.width(), 1000);
    QTRY_COMPARE(list->height(), list->sizeHint().height());
    QCOMPARE(list->height(), 3 * list->sizeHintForRow(0) + 2 * list->frameWidth());
}

void TestMainWindow::listHeightStableOnSelect()
{
    // R1-01, M06 (ui-design 4.4): the list height uses the tallest editor page. It does not
    // change when the user selects another rule, it has 2 rows at 900x560 (R6-06), and the current
    // rule stays visible.
    Fixture f;
    f.window.show();
    f.window.resize(900, 560);
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    for (const char *type : {"numbering", "replace", "insert", "remove"})
        f.addRuleAction(QString::fromLatin1(type))->trigger();
    QApplication::processEvents();
    auto *list = f.child<QListView>("ruleList");
    const int twoRows = 2 * list->sizeHintForRow(0) + 2 * list->frameWidth(); // R6-06
    QTRY_COMPARE(list->height(), twoRows);
    for (int row : {3, 0, 2, 1, 3}) {
        list->setCurrentIndex(list->model()->index(row, 0));
        QApplication::processEvents();
        QCOMPARE(list->height(), twoRows);
        QVERIFY2(list->viewport()->rect().contains(list->visualRect(list->currentIndex())),
                 qPrintable(QString::number(row)));
    }
    // R6-06: with 5 rules and Chain options open, the list still shows 2 rows.
    f.addRuleAction(QStringLiteral("changeCase"))->trigger();
    auto *chain = f.window.rulePanel()->findChild<ChainOptionsWidget *>();
    QVERIFY(chain);
    chain->setOpen(true);
    QTRY_COMPARE(list->height(), twoRows);
    QVERIFY(list->viewport()->rect().contains(list->visualRect(list->currentIndex())));
    chain->setOpen(false);
    // A high window: 3 rows or more again, the same for each rule.
    f.window.resize(900, 1100);
    QTRY_VERIFY(list->height() >= 3 * list->sizeHintForRow(0));
    const int high = list->height();
    for (int row : {0, 1}) {
        list->setCurrentIndex(list->model()->index(row, 0));
        QApplication::processEvents();
        QCOMPARE(list->height(), high);
    }
}

void TestMainWindow::listWholeRows()
{
    // R2-02 (ui-design 4.4): the list shows whole rows only. It keeps 3 rows when the tallest
    // page is then cut by about one field row (40 px) or less.
    Fixture f;
    f.window.show();
    f.window.resize(1200, 760);
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    for (const char *type : {"numbering", "remove", "replace", "insert", "changeCase"})
        f.addRuleAction(QString::fromLatin1(type))->trigger();
    QApplication::processEvents();
    auto *list = f.child<QListView>("ruleList");
    auto *scroll = f.child<QScrollArea>("editorScroll");
    auto *pages = f.child<QStackedWidget>("editorPages");
    auto *chain = f.window.rulePanel()->findChild<ChainOptionsWidget *>();
    QVERIFY(list && pages && chain);
    const int row = list->sizeHintForRow(0);
    const int frames = 2 * list->frameWidth();
    int tallest = 0;
    for (int i = 0; i < pages->count(); ++i)
        tallest = std::max(tallest, pages->widget(i)
                                            ->sizeHint()
                                            .expandedTo(pages->widget(i)->minimumSize())
                                            .height()); // as RuleSplitter
    tallest += chain->closedHeight(); // O13: the closed Chain options is under the page
    bool keptCut = false;
    for (int h = f.window.minimumHeight(); h <= 1100; h += 7) {
        f.window.resize(1200, h);
        QApplication::processEvents();
        const QString at = QStringLiteral("height %1: list %2").arg(h).arg(list->height());
        QVERIFY2((list->height() - frames) % row == 0, qPrintable(at));
        const int rows = (list->height() - frames) / row;
        QVERIFY2(rows >= 2 && rows <= 5, qPrintable(at)); // R6-06: at least 2 rows
        const int cut = tallest + 2 * scroll->frameWidth() - scroll->height();
        if (rows < 3) // 3 rows would cut the tallest page by more than 40 px
            QVERIFY2(cut + (3 - rows) * row > 40, qPrintable(at));
        if (rows == 3 && cut > 0)
            keptCut = true; // the fade and the scroll show the rest
        if (rows > 3)
            QVERIFY2(cut <= 0, qPrintable(at)); // more rows only when the page fits
    }
    QVERIFY(keptCut);
}

void TestMainWindow::chainOptionsScrollsIntoViewAtSmallWindow()
{
    // O13 (ui-design 4.4): at 900 x 560 the page and Chain options scroll with the fade. Open
    // Chain options shows in full; the list keeps 2 rows or more and its height.
    Fixture f;
    f.window.show();
    f.window.resize(900, 560);
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    for (const char *type : {"numbering", "replace", "insert", "remove"})
        f.addRuleAction(QString::fromLatin1(type))->trigger();
    auto *list = f.child<QListView>("ruleList");
    auto *scroll = f.child<QScrollArea>("editorScroll");
    auto *chain = f.window.rulePanel()->findChild<ChainOptionsWidget *>();
    QVERIFY(list && scroll && chain);
    list->setCurrentIndex(list->model()->index(0, 0)); // Numbering, the tallest page
    QApplication::processEvents();
    QVERIFY(scroll->widget()->isAncestorOf(chain));
    QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
    QVERIFY(f.child<QWidget>("editorFade")->isVisible());
    const int listHeight = list->height();
    QVERIFY(listHeight >= 2 * list->sizeHintForRow(0) + 2 * list->frameWidth()); // R6-06
    f.child<QToolButton>("chainOptionsHeader")->click();
    auto *last = chain->findChild<QCheckBox *>(QStringLiteral("folderExtensions"));
    QVERIFY(last);
    QTRY_VERIFY(scroll->viewport()->rect().contains(
            QRect(last->mapTo(scroll->viewport(), QPoint()), last->size())));
    QCOMPARE(list->height(), listHeight);
}

void TestMainWindow::editorPagesFitAtDefault()
{
    // C16, O06a (ui-design 4.1): at window-default, each editor page fits without a scroll and
    // the list shows the 5 rules.
    Fixture f;
    f.window.show();
    f.window.resize(tokens::defaultWidth, tokens::defaultHeight);
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    const QStringList types{QStringLiteral("numbering"), QStringLiteral("remove"),
                            QStringLiteral("replace"), QStringLiteral("insert"),
                            QStringLiteral("changeCase")};
    for (const QString &type : types)
        f.addRuleAction(type)->trigger();
    QApplication::processEvents();
    auto *list = f.child<QListView>("ruleList");
    auto *scroll = f.child<QScrollArea>("editorScroll");
    QVERIFY(list && scroll);
    QTRY_COMPARE(list->height(), 5 * list->sizeHintForRow(0) + 2 * list->frameWidth());
    for (int row = 0; row < types.size(); ++row) {
        list->setCurrentIndex(list->model()->index(row, 0));
        QApplication::processEvents();
        QVERIFY2(scroll->verticalScrollBar()->maximum() == 0,
                 qPrintable(types.at(row) + QStringLiteral(": scroll %1")
                                                    .arg(scroll->verticalScrollBar()->maximum())));
    }
}

void TestMainWindow::rulePanelWidthStable()
{
    // O08 (ui-design 4.1, 4.4): the rule panel width does not change with its content: not for
    // the first rule, another page, or open Chain options.
    Fixture f;
    f.window.show();
    f.window.resize(1000, 700);
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QApplication::processEvents();
    RulePanel *panel = f.window.rulePanel();
    const int width = panel->width();
    const int minimum = panel->minimumSizeHint().width();
    QVERIFY2(minimum >= 360, qPrintable(QString::number(minimum)));
    const auto check = [&](const char *when) {
        QApplication::processEvents();
        QVERIFY2(panel->width() == width, qPrintable(QStringLiteral("%1: %2 != %3")
                                                             .arg(QLatin1String(when))
                                                             .arg(panel->width())
                                                             .arg(width)));
        QVERIFY2(panel->minimumSizeHint().width() == minimum, when);
    };
    f.addRuleAction(QStringLiteral("replace"))->trigger();
    check("first rule");
    for (const char *type : {"remove", "numbering", "cleanup", "extension"})
        f.addRuleAction(QString::fromLatin1(type))->trigger();
    check("more rules");
    auto *chain = panel->findChild<ChainOptionsWidget *>();
    QVERIFY(chain);
    chain->setOpen(true);
    check("chain options open");
    chain->setOpen(false);
    check("chain options closed");
}

void TestMainWindow::splitterDoubleClickRefits()
{
    // ui-design 4.4: a drag stops the automatic list height; a double-click on the handle
    // turns it on again at once.
    Fixture f;
    f.window.show();
    f.window.resize(900, 1100);
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    f.addRuleAction(QStringLiteral("replace"))->trigger();
    QApplication::processEvents();
    auto *list = f.child<QListView>("ruleList");
    QSplitter *splitter = nullptr;
    for (QSplitter *s : f.window.findChildren<QSplitter *>()) {
        if (s->orientation() == Qt::Vertical && s->isAncestorOf(list))
            splitter = s;
    }
    QVERIFY(splitter);
    QTRY_VERIFY(list->height() > 0);
    const QList<int> automatic = splitter->sizes();
    const QList<int> dragged{automatic.at(0) + 120, automatic.at(1) - 120};
    splitter->setSizes(dragged);
    emit splitter->splitterMoved(dragged.at(0), 1); // as a drag of the handle
    f.window.resize(900, 1080);
    QApplication::processEvents();
    QVERIFY2(splitter->sizes().at(0) > automatic.at(0) + 60, "the user sizes stay");

    QTest::mouseDClick(splitter->handle(1), Qt::LeftButton);
    QTRY_VERIFY2(splitter->sizes().at(0) <= automatic.at(0),
                 qPrintable(QString::number(splitter->sizes().at(0))));
}

void TestMainWindow::splitterHandlesPlain()
{
    // M20: the splitter handles draw no grip in the empty space between the panels.
    Fixture f;
    f.window.show();
    f.window.resize(f.window.minimumSize());
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    f.addRuleAction(QStringLiteral("replace"))->trigger();
    QApplication::processEvents();
    const QList<QSplitter *> splitters = f.window.findChildren<QSplitter *>();
    QVERIFY(splitters.size() >= 2);
    for (QSplitter *splitter : splitters) {
        for (int i = 1; i < splitter->count(); ++i) {
            QSplitterHandle *handle = splitter->handle(i);
            const QImage image = handle->grab().toImage();
            QVERIFY(!image.isNull());
            const QRgb first = image.pixel(0, 0);
            bool plain = true;
            for (int y = 0; y < image.height() && plain; ++y) {
                for (int x = 0; x < image.width() && plain; ++x)
                    plain = image.pixel(x, y) == first;
            }
            QVERIFY2(plain, qPrintable(splitter->objectName()));
        }
    }
}

void TestMainWindow::focusEditorFirstField()
{
    // M29: Cmd+3 puts the focus in the first field of the editor, not in Apply to.
    Fixture f;
    f.window.show();
    f.window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    const QList<std::pair<QString, QString>> firstFields{
            {"replace", "find"}, {"numbering", "start"}, {"cleanup", "trim"}};
    for (const auto &[type, field] : firstFields) {
        f.addRuleAction(type)->trigger();
        f.window.tableView()->setFocus();
        f.window.actions().focusEditor->trigger();
        QWidget *focused = QApplication::focusWidget();
        QVERIFY2(focused && focused->objectName() == field,
                 qPrintable(type + u' ' + (focused ? focused->objectName() : QString())));
    }
}

void TestMainWindow::namesShownAtMinimumWidth()
{
    // ui-design 4.4: the rule panel is as wide as its widest editor. At the minimum window
    // width the table still shows all of Old name and New name.
    Fixture f;
    f.addFiles({"a.txt", "b.txt"});
    f.window.show();
    f.window.resize(f.window.minimumSize());
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    for (const char *type : {"insert", "numbering", "cleanup"}) {
        f.window.actions()
                .addRuleMenu->findChild<QAction *>(QStringLiteral("addRule_") + QLatin1String(type))
                ->trigger();
    }
    QApplication::processEvents();
    QCOMPARE(f.window.width(), f.window.minimumWidth());
    QTableView *table = f.window.tableView();
    QHeaderView *header = table->horizontalHeader();
    const auto end = [&](int column) {
        return header->sectionViewportPosition(column) + header->sectionSize(column);
    };
    QTRY_VERIFY2(end(ItemTableModel::NewName) <= table->viewport()->width(),
                 qPrintable(QStringLiteral("%1 > %2")
                                    .arg(end(ItemTableModel::NewName))
                                    .arg(table->viewport()->width())));
    // M14: the two name columns are balanced.
    QCOMPARE(header->sectionSize(ItemTableModel::NewName),
             header->sectionSize(ItemTableModel::OldName));

    // A wide window: each name column is 30% of the table (mockup).
    f.window.resize(1600, f.window.height());
    QTRY_COMPARE(header->sectionSize(ItemTableModel::OldName), table->viewport()->width() * 3 / 10);
    QCOMPARE(header->sectionSize(ItemTableModel::NewName),
             header->sectionSize(ItemTableModel::OldName));

    // A column that the app shows (Folder for a second folder) is not a change by the user:
    // the names fit again at the minimum width.
    table->setColumnHidden(ItemTableModel::Folder, false);
    QApplication::processEvents();
    f.window.resize(f.window.minimumSize());
    QApplication::processEvents();
    QTRY_VERIFY2(end(ItemTableModel::NewName) <= table->viewport()->width(),
                 qPrintable(QStringLiteral("%1 > %2")
                                    .arg(end(ItemTableModel::NewName))
                                    .arg(table->viewport()->width())));
    table->setColumnHidden(ItemTableModel::Folder, true);

    // A width that the user sets with the mouse stays: drag the right edge of Old name.
    QWidget *headerView = header->viewport();
    const int edge = header->sectionViewportPosition(ItemTableModel::OldName) +
                     header->sectionSize(ItemTableModel::OldName) - 1;
    const int y = headerView->height() / 2;
    const auto send = [&](QEvent::Type type, int x, Qt::MouseButtons buttons) {
        QMouseEvent event(type, QPointF(x, y), headerView->mapToGlobal(QPointF(x, y)),
                          Qt::LeftButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(headerView, &event);
    };
    send(QEvent::MouseMove, edge, Qt::NoButton); // the cursor over the handle
    send(QEvent::MouseButtonPress, edge, Qt::LeftButton);
    send(QEvent::MouseMove, edge - 40, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, edge - 40, Qt::NoButton);
    const int userWidth = header->sectionSize(ItemTableModel::OldName);
    QVERIFY2(userWidth < edge - header->sectionViewportPosition(ItemTableModel::OldName) - 20,
             qPrintable(QString::number(userWidth)));
    f.window.resize(1600, f.window.height());
    QApplication::processEvents();
    QCOMPARE(header->sectionSize(ItemTableModel::OldName), userWidth);
}

void TestMainWindow::viewShowColumns()
{
    // M04: View > Show Columns has the four optional columns, and they show or hide them.
    Fixture f;
    auto *menu = f.child<QMenu>("showColumnsMenu");
    QVERIFY(menu);
    QStringList texts;
    for (QAction *a : menu->actions())
        texts.append(a->text());
    QCOMPARE(texts, QStringList({QStringLiteral("Message"), QStringLiteral("Folder"),
                                 QStringLiteral("Size"), QStringLiteral("Date modified")}));
    auto *table = f.child<ItemTableView>("itemTable");
    QAction *size = menu->actions().at(2);
    QVERIFY(!size->isChecked());
    QVERIFY(table->isColumnHidden(ItemTableModel::Size));
    size->trigger();
    QVERIFY(!table->isColumnHidden(ItemTableModel::Size));
    // The same items as the header menu.
    QCOMPARE(menu->actions(), table->findChild<QMenu *>(QStringLiteral("columnsMenu"))->actions());
}

void TestMainWindow::toolbarPresetsArrowOnTextLine()
{
    // M17: the Presets arrow is inside the button on the text line; toolbar text uses the body
    // font.
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    auto *presets = f.child<MenuToolButton>("toolBarPresets");
    QVERIFY(presets);
    const QRect arrow = presets->arrowRect();
    QVERIFY(presets->rect().contains(arrow));
    QVERIFY2(qAbs(arrow.center().y() - presets->rect().center().y()) <= 1,
             qPrintable(QStringLiteral("%1 %2").arg(arrow.center().y()).arg(presets->height())));
    QVERIFY(presets->sizeHint().width() >=
            presets->fontMetrics().horizontalAdvance(presets->text()) + arrow.width());
    const qreal body = QApplication::font().pointSizeF();
    auto *bar = f.child<QToolBar>("mainToolBar");
    for (QAction *a : {f.window.actions().addFiles, f.window.actions().addFolder})
        QCOMPARE(bar->widgetForAction(a)->font().pointSizeF(), body);
    QCOMPARE(presets->font().pointSizeF(), body);
}

void TestMainWindow::programFocusKeepsKeyboardModeOff()
{
    // R8-01, ui-design 14 rule 5: a focus change by the program, or after a mouse click, never
    // turns keyboard mode on (no ring, no row outline).
    KeyboardMode::instance()->setOn(false);
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    QTableView *table = f.window.tableView();
    const auto check = [&](const char *path, QWidget *focus) {
        QTRY_VERIFY2(QApplication::focusWidget() == focus, path);
        QVERIFY2(!KeyboardMode::isOn(), path);
    };

    // A drop on the empty list, Clear List, and Add Files (the dialog adds with addPaths).
    auto *empty = f.window.findChild<EmptyState *>();
    QTRY_COMPARE(QApplication::focusWidget(), empty);
    emit empty->pathsDropped(f.files({QStringLiteral("a1.txt")}));
    check("drop", table);
    f.window.actions().clearList->trigger();
    check("Clear List", empty);
    f.addFiles({QStringLiteral("a1.txt"), QStringLiteral("a2.txt"), QStringLiteral("b.txt")});
    check("Add Files", table);

    // Show: Problems with no problem hides the table: the focus goes to the Filter field.
    f.window.actions().showProblemsOnly->trigger();
    check("no match", f.window.findChild<QLineEdit *>(QStringLiteral("filterField")));
    f.window.actions().showAllItems->trigger();

    // A filter with no match, then the Clear Filter link.
    auto *filter = f.window.findChild<QLineEdit *>(QStringLiteral("filterField"));
    QTest::mouseClick(filter, Qt::LeftButton);
    QTest::keyClicks(filter, QStringLiteral("zzz"));
    check("filter with no match", filter);
    auto *clearLink = f.window.findChild<QLabel *>(QStringLiteral("clearFilterLink"));
    QTRY_VERIFY(clearLink->isVisible());
    emit clearLink->linkActivated(QStringLiteral("clear"));
    check("Clear Filter", table);

    // A mouse click on the problem arrows of the action bar. The press turns the mode off; the
    // focus change after the release must not turn it on again.
    f.window.ruleModel().insertRule(0, regexRemoveDigits());
    QTRY_VERIFY(f.window.actions().nextProblem->isEnabled());
    for (const char *name : {"nextProblemButton", "previousProblemButton"}) {
        QTest::mouseClick(filter, Qt::LeftButton);
        check("click in the filter", filter);
        auto *arrow = f.window.actionBar()->findChild<QAbstractButton *>(QLatin1StringView(name));
        QVERIFY2(arrow && arrow->isVisible(), name);
        QTest::mouseClick(arrow, Qt::LeftButton);
        check(name, table);
    }

    // The menu items of the focus actions (no shortcut key).
    for (QAction *action :
         {f.window.actions().find, f.window.actions().focusRules, f.window.actions().focusTable,
          f.window.actions().focusEditor, f.window.actions().nextProblem}) {
        action->trigger();
        QVERIFY2(!KeyboardMode::isOn(), qPrintable(action->objectName()));
    }
}

void TestMainWindow::focusShortcutsTurnKeyboardModeOn()
{
    // R8-01, ui-design 14 rule 1: a shortcut key that moves the focus turns keyboard mode on.
    KeyboardMode *mode = KeyboardMode::instance();
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.addFiles({QStringLiteral("a1.txt"), QStringLiteral("a2.txt"), QStringLiteral("b.txt")});
    f.window.ruleModel().insertRule(0, regexRemoveDigits());
    QTRY_VERIFY(f.window.actions().nextProblem->isEnabled());
    auto *filter = f.window.findChild<QLineEdit *>(QStringLiteral("filterField"));
    QTableView *table = f.window.tableView();
    struct Step
    {
        QAction *action;
        QWidget *from;
        QWidget *to;
    };
    const QList<Step> steps = {
            {f.window.actions().find, table, filter},
            {f.window.actions().focusTable, filter, table},
            {f.window.actions().focusRules, table, f.window.rulePanel()->ruleList()},
            {f.window.actions().nextProblem, filter, table},
            {f.window.actions().previousProblem, filter, table},
    };
    for (const Step &step : steps) {
        const QByteArray name = step.action->objectName().toLatin1();
        step.from->setFocus(Qt::OtherFocusReason);
        QTRY_COMPARE(QApplication::focusWidget(), step.from);
        mode->setOn(false);
        QTest::keySequence(&f.window, step.action->shortcut());
        QTRY_VERIFY2(QApplication::focusWidget() == step.to, name.constData());
        QVERIFY2(KeyboardMode::isOn(), name.constData());
    }

    // Cmd+3 puts the focus in the first field of the rule editor.
    f.window.rulePanel()->ruleList()->setCurrentIndex(f.window.ruleModel().index(0, 0));
    table->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), table);
    mode->setOn(false);
    QTest::keySequence(&f.window, f.window.actions().focusEditor->shortcut());
    QTRY_VERIFY(f.window.rulePanel()->isAncestorOf(QApplication::focusWidget()) &&
                QApplication::focusWidget() != f.window.rulePanel()->ruleList());
    QVERIFY(KeyboardMode::isOn());
    mode->setOn(false);
}

QTEST_MAIN(TestMainWindow)
void TestMainWindow::tableShortcutsOnlyWithFocus()
{
    // R1-02, same rule as M28: the menu shows Delete for Remove from List only while the table
    // has the focus. R2-03: Include and Exclude show no key, because Space toggles the state and
    // is only a key of the table. The keys still work in the table.
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.addFiles({QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")});
    const Actions &a = f.window.actions();
    auto *table = f.child<ItemTableView>("itemTable");
    QListView *rules = f.window.rulePanel()->ruleList();

    rules->setFocus();
    QTRY_VERIFY(rules->hasFocus());
    QVERIFY(a.removeFromList->shortcut().isEmpty());
    QVERIFY(a.include->shortcut().isEmpty());
    QVERIFY(a.exclude->shortcut().isEmpty());

    table->setFocus();
    QTRY_VERIFY(table->hasFocus());
    QCOMPARE(a.removeFromList->shortcut(), QKeySequence(Qt::Key_Delete));
    QVERIFY(a.include->shortcut().isEmpty());
    QVERIFY(a.exclude->shortcut().isEmpty());

    // Space toggles Include one time; Delete removes the selected row.
    table->selectRow(0);
    QTest::keyClick(table, Qt::Key_Space);
    QVERIFY(!f.window.itemModel().items().at(0).included);
    QTest::keyClick(table, Qt::Key_Space);
    QVERIFY(f.window.itemModel().items().at(0).included);
    QTest::keyClick(table, Qt::Key_Delete);
    QCOMPARE(f.oldNames(), (QStringList{QStringLiteral("b.txt"), QStringLiteral("c.txt")}));

    // An open popup menu takes the focus for a short time; the menu keeps the keys.
    QFocusEvent popupOut(QEvent::FocusOut, Qt::PopupFocusReason);
    QCoreApplication::sendEvent(table, &popupOut);
    QCOMPARE(a.removeFromList->shortcut(), QKeySequence(Qt::Key_Delete));

    rules->setFocus();
    QTRY_VERIFY(rules->hasFocus());
    QVERIFY(a.removeFromList->shortcut().isEmpty());
    QVERIFY(a.include->shortcut().isEmpty());

    // Space and Delete in the filter field edit the text; the selected rows do not change.
    table->selectRow(0);
    auto *filter = f.child<QLineEdit>("filterField");
    filter->setFocus();
    QTRY_VERIFY(filter->hasFocus());
    QTest::keyClick(filter, Qt::Key_Space);
    QTest::keyClick(filter, Qt::Key_Delete);
    QVERIFY(f.window.itemModel().items().at(0).included);
    QCOMPARE(f.window.itemModel().rowCount(), 2);
}

#include "tst_app_mainwindow.moc"
