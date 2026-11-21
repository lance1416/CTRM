// SPDX-License-Identifier: GPL-3.0-or-later
#include "actions.h"
#include "appcontext.h"
#include "fakefilesystem.h"
#include "mainwindow.h"
#include "settings.h"
#include "widgets/messagebar.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest>

#include <memory>

using namespace ctrm::app;

namespace {

QList<QAction *> allActions(const Actions &a)
{
    return {a.addFiles,
            a.addFolder,
            a.removeFromList,
            a.clearList,
            a.refresh,
            a.closeWindow,
            a.quit,
            a.undoRename,
            a.include,
            a.exclude,
            a.selectAll,
            a.find,
            a.duplicateRule,
            a.deleteRule,
            a.moveRuleUp,
            a.moveRuleDown,
            a.toggleRule,
            a.clearRules,
            a.saveAsPreset,
            a.updatePreset,
            a.managePresets,
            a.importPreset,
            a.exportRules,
            a.showSteps,
            a.highlightChanged,
            a.showAllItems,
            a.showChangedOnly,
            a.showProblemsOnly,
            a.showToolbar,
            a.rename,
            a.skipAndRename,
            a.nextProblem,
            a.previousProblem,
            a.history,
            a.cancelBatch,
            a.focusRules,
            a.focusTable,
            a.focusEditor,
            a.preferences,
            a.help,
            a.regexHelp,
            a.reportProblem,
            a.about,
            a.addRuleMenu->menuAction()};
}

// A context on a fake file system and temporary folders and settings.
struct TestContext
{
    QTemporaryDir dir;
    std::unique_ptr<QSettings> qsettings;
    FakeFileSystem *fs = nullptr;
    std::unique_ptr<AppContext> ctx;

    TestContext()
    {
        qsettings = std::make_unique<QSettings>(dir.filePath("settings.ini"), QSettings::IniFormat);
        auto fake = std::make_unique<FakeFileSystem>();
        fs = fake.get();
        ctx = std::make_unique<AppContext>(std::move(fake), dir.filePath("data/history"),
                                           dir.filePath("config/presets"), qsettings.get());
    }
};

} // namespace

class TestAppShell : public QObject
{
    Q_OBJECT
private slots:
    void fontsInstalled();
    void messageBarWrapsLongText();
    void testContextUsesDirs();
    void settingsDefaults();
    void settingsRoundTrip();
    void actionsShortcuts();
    void actionsHaveText();
    void windowMinimumSize();
    void windowDefaultSize();
    void splitterRatio();
    void menusExist();
    void toolbarHasNoRename();
    void stateSavedOnClose();
    void viewTogglesPersist();
    void editActionsUseFocusWidget();
    void noNetworkModule();
};

void TestAppShell::fontsInstalled()
{
    // The layout tests measure text. With no fonts, Qt draws each glyph as a box as wide as the
    // font size, so text is about two times wider and those tests fail for the wrong reason.
    // On Windows the offscreen platform needs QT_QPA_FONTDIR (the CI sets it).
    QVERIFY2(!QFontDatabase::families().isEmpty(),
             "No fonts: set QT_QPA_FONTDIR to a folder with fonts (Windows: %WINDIR%\\Fonts).");
}

void TestAppShell::testContextUsesDirs()
{
    TestContext t;
    QVERIFY(QDir(t.dir.filePath("data/history")).exists());
    QVERIFY(QDir(t.dir.filePath("config/presets")).exists());
    QCOMPARE(t.ctx->historyDir(), t.dir.filePath("data/history"));
    QCOMPARE(t.ctx->presetsDir(), t.dir.filePath("config/presets"));
    QCOMPARE(&t.ctx->fileSystem(), static_cast<ctrm::FileSystem *>(t.fs));
    t.ctx->settings().setRetentionDays(7);
    QCOMPARE(t.qsettings->allKeys().size(), 1);
}

void TestAppShell::settingsDefaults()
{
    QTemporaryDir dir;
    QSettings qs(dir.filePath("s.ini"), QSettings::IniFormat);
    const Settings s(&qs);
    QCOMPARE(s.folderDropAnswer(), FolderDropAnswer::Ask);
    QCOMPARE(s.includeHidden(), false);
    QCOMPARE(s.highlightChanged(), false);
    QCOMPARE(s.retentionCount(), 500);
    QCOMPARE(s.retentionDays(), 180);
    QCOMPARE(s.showSteps(), true);
    QVERIFY(s.windowGeometry().isEmpty());
    QVERIFY(s.windowState().isEmpty());
    QVERIFY(s.splitterState("main").isEmpty());
}

void TestAppShell::settingsRoundTrip()
{
    QTemporaryDir dir;
    const QString file = dir.filePath("s.ini");
    {
        QSettings qs(file, QSettings::IniFormat);
        Settings s(&qs);
        s.setFolderDropAnswer(FolderDropAnswer::AddContents);
        s.setIncludeHidden(true);
        s.setHighlightChanged(true);
        s.setRetentionCount(20);
        s.setRetentionDays(3);
        s.setShowSteps(false);
        s.setWindowGeometry("geo");
        s.setWindowState("state");
        s.setSplitterState("main", "a");
        s.setSplitterState("rules", "b");
    }
    QSettings qs(file, QSettings::IniFormat);
    const Settings s(&qs);
    QCOMPARE(s.folderDropAnswer(), FolderDropAnswer::AddContents);
    QCOMPARE(s.includeHidden(), true);
    QCOMPARE(s.highlightChanged(), true);
    QCOMPARE(s.retentionCount(), 20);
    QCOMPARE(s.retentionDays(), 3);
    QCOMPARE(s.showSteps(), false);
    QCOMPARE(s.windowGeometry(), QByteArray("geo"));
    QCOMPARE(s.windowState(), QByteArray("state"));
    QCOMPARE(s.splitterState("main"), QByteArray("a"));
    QCOMPARE(s.splitterState("rules"), QByteArray("b"));

    // A bad stored value gives the default.
    qs.setValue("general/folderDropAnswer", "sometimes");
    QCOMPARE(s.folderDropAnswer(), FolderDropAnswer::Ask);
}

void TestAppShell::actionsShortcuts()
{
    QObject parent;
    const Actions a = createActions(&parent);
    QCOMPARE(a.rename->shortcut(), QKeySequence("Ctrl+Return"));
    QCOMPARE(a.addFiles->shortcut(), QKeySequence("Ctrl+O"));
    QCOMPARE(a.addFolder->shortcut(), QKeySequence("Ctrl+Shift+O"));
    QCOMPARE(a.nextProblem->shortcut(), QKeySequence("F8"));
    QCOMPARE(a.previousProblem->shortcut(), QKeySequence("Shift+F8"));
    QCOMPARE(a.undoRename->shortcut(), QKeySequence("Ctrl+Z"));
    QCOMPARE(a.undoRename->shortcutContext(), Qt::WindowShortcut);
    QCOMPARE(a.find->shortcut(), QKeySequence("Ctrl+F"));
    QCOMPARE(a.duplicateRule->shortcut(), QKeySequence("Ctrl+D"));
    QCOMPARE(a.moveRuleUp->shortcut(), QKeySequence("Ctrl+Alt+Up"));
    QCOMPARE(a.moveRuleDown->shortcut(), QKeySequence("Ctrl+Alt+Down"));
    QCOMPARE(a.focusRules->shortcut(), QKeySequence("Ctrl+1"));
    QCOMPARE(a.focusTable->shortcut(), QKeySequence("Ctrl+2"));
    QCOMPARE(a.focusEditor->shortcut(), QKeySequence("Ctrl+3"));
    QCOMPARE(a.saveAsPreset->shortcut(), QKeySequence("Ctrl+S"));
    QCOMPARE(a.cancelBatch->shortcut(), QKeySequence("Escape"));
    QCOMPARE(a.removeFromList->shortcut(), QKeySequence("Del"));
    QCOMPARE(a.addRuleMenu->menuAction()->shortcut(), QKeySequence("Ctrl+Shift+N"));
#ifdef Q_OS_MACOS
    QCOMPARE(a.history->shortcut(), QKeySequence("Ctrl+Y"));
    QCOMPARE(a.refresh->shortcut(), QKeySequence("Ctrl+R"));
    QCOMPARE(a.clearList->shortcut(), QKeySequence("Ctrl+Shift+Backspace"));
#else
    QCOMPARE(a.history->shortcut(), QKeySequence("Ctrl+H"));
    QCOMPARE(a.refresh->shortcut(), QKeySequence("F5"));
    QCOMPARE(a.clearList->shortcut(), QKeySequence("Ctrl+Shift+Delete"));
#endif
    QCOMPARE(a.about->menuRole(), QAction::AboutRole);
    QCOMPARE(a.preferences->menuRole(), QAction::PreferencesRole);
    QCOMPARE(a.quit->menuRole(), QAction::QuitRole);
    // Only explicit roles move to the macOS application menu.
    QCOMPARE(a.addFiles->menuRole(), QAction::NoRole);

    // The three Show actions are exclusive; All items is on.
    QVERIFY(a.showAllItems->isChecked());
    a.showProblemsOnly->trigger();
    QVERIFY(!a.showAllItems->isChecked());
    QVERIFY(a.showProblemsOnly->isChecked());

    // All shortcuts are distinct.
    QSet<QString> seen;
    for (QAction *action : allActions(a)) {
        for (const QKeySequence &k : action->shortcuts()) {
            const QString key = k.toString();
            QVERIFY2(!seen.contains(key), qPrintable(key));
            seen.insert(key);
        }
    }
}

void TestAppShell::actionsHaveText()
{
    QObject parent;
    const Actions a = createActions(&parent);
    const QList<QAction *> actions = allActions(a);
    QCOMPARE(actions.size(), 44);
    for (QAction *action : actions) {
        QVERIFY(action);
        QVERIFY2(!action->text().isEmpty(), qPrintable(action->objectName()));
        QVERIFY2(!action->toolTip().isEmpty(), qPrintable(action->objectName()));
        QVERIFY2(!action->objectName().isEmpty(), qPrintable(action->text()));
    }
}

void TestAppShell::windowMinimumSize()
{
    TestContext t;
    MainWindow w(*t.ctx);
    QCOMPARE(w.minimumSize(), QSize(900, 560));
    QVERIFY(w.flowStripSlot());
    QVERIFY(w.rulePanelSlot());
    QVERIFY(w.tableSlot());
    QVERIFY(w.actionBarSlot());
    QVERIFY(w.messageBarSlot());
    QCOMPARE(w.windowTitle(), QStringLiteral("CTRM"));
}

void TestAppShell::windowDefaultSize()
{
    // O06a: a new install opens at window-default; a saved smaller size is kept.
    TestContext t;
    QByteArray saved;
    {
        MainWindow w(*t.ctx);
        QCOMPARE(w.size(), QSize(1280, 860));
        const QRect screen = w.screen()->availableGeometry();
        if (screen.width() < 1000 || screen.height() < 700)
            QSKIP("The screen is too small for a saved size of 1000 x 700.");
        w.resize(1000, 700);
        saved = w.saveGeometry();
    }
    t.ctx->settings().setWindowGeometry(saved);
    MainWindow w(*t.ctx);
    QCOMPARE(w.size(), QSize(1000, 700));
}

void TestAppShell::splitterRatio()
{
    TestContext t;
    MainWindow w(*t.ctx);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QSplitter *s = w.mainSplitter();
    QCOMPARE(s->orientation(), Qt::Horizontal);
    QVERIFY(!s->childrenCollapsible());
    QCOMPARE(s->widget(0), w.rulePanelSlot());
    QVERIFY(s->widget(1)->isAncestorOf(w.tableSlot()));
    const QList<int> sizes = s->sizes();
    QCOMPARE(sizes.size(), 2);
    const double ratio = double(sizes.at(1)) / sizes.at(0);
    QVERIFY2(ratio > 1.9 && ratio < 2.1, qPrintable(QString::number(ratio)));
}

void TestAppShell::menusExist()
{
    TestContext t;
    MainWindow w(*t.ctx);
    QStringList titles;
    for (QAction *a : w.menuBar()->actions())
        titles.append(a->text().remove('&'));
    QCOMPARE(titles, QStringList({"File", "Edit", "Rules", "Presets", "View", "Rename", "Help"}));
    QVERIFY(w.menuBar()->actions().at(2)->menu()->actions().contains(
            w.actions().addRuleMenu->menuAction()));
}

void TestAppShell::toolbarHasNoRename()
{
    TestContext t;
    MainWindow w(*t.ctx);
    const QList<QToolBar *> bars = w.findChildren<QToolBar *>();
    QCOMPARE(bars.size(), 1);
    const QList<QAction *> actions = bars.first()->actions();
    QVERIFY(!actions.contains(w.actions().rename));
    QVERIFY(actions.contains(w.actions().addFiles));
    QVERIFY(actions.contains(w.actions().addFolder));
    QVERIFY(actions.contains(w.actions().history));
    QVERIFY(actions.contains(w.actions().undoRename));
    QVERIFY(actions.contains(w.actions().removeFromList));
    QVERIFY(actions.contains(w.actions().clearList));
    QVERIFY(actions.contains(w.actions().refresh));
    QCOMPARE(w.actions().showToolbar->isChecked(), bars.first()->isVisible() || !w.isVisible());
}

void TestAppShell::stateSavedOnClose()
{
    TestContext t;
    QList<int> saved;
    {
        MainWindow w(*t.ctx);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        // The narrowest rule panel (O08: at least 360 px), not the 1:2 default.
        const int total = w.mainSplitter()->sizes().at(0) + w.mainSplitter()->sizes().at(1);
        const int panel = w.mainSplitter()->widget(0)->minimumSizeHint().width();
        w.mainSplitter()->setSizes({panel, total - panel});
        saved = w.mainSplitter()->sizes();
        QVERIFY2(saved.at(1) > 2 * saved.at(0), qPrintable(QString::number(saved.at(0))));
        w.close();
    }
    QVERIFY(!t.ctx->settings().windowGeometry().isEmpty());
    QVERIFY(!t.ctx->settings().windowState().isEmpty());
    QVERIFY(!t.ctx->settings().splitterState("main").isEmpty());

    MainWindow again(*t.ctx);
    again.show();
    QVERIFY(QTest::qWaitForWindowExposed(&again));
    // The rule panel keeps its saved width (the window size can differ offscreen).
    QCOMPARE(again.mainSplitter()->sizes().at(0), saved.at(0));
}

void TestAppShell::viewTogglesPersist()
{
    TestContext t;
    {
        MainWindow w(*t.ctx);
        QVERIFY(w.actions().showSteps->isChecked());
        QVERIFY(!w.actions().highlightChanged->isChecked());
        w.actions().showSteps->trigger();
        w.actions().highlightChanged->trigger();
    }
    QCOMPARE(t.ctx->settings().showSteps(), false);
    QCOMPARE(t.ctx->settings().highlightChanged(), true);
    MainWindow again(*t.ctx);
    QVERIFY(!again.actions().showSteps->isChecked());
    QVERIFY(again.actions().highlightChanged->isChecked());
}

void TestAppShell::editActionsUseFocusWidget()
{
    TestContext t;
    MainWindow w(*t.ctx);
    auto *edit = new QLineEdit(QStringLiteral("hello"), w.tableSlot());
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.activateWindow();
    edit->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), edit);
    edit->selectAll();

    QAction *cut = nullptr;
    for (QAction *a : w.menuBar()->actions().at(1)->menu()->actions()) {
        if (a->text() == QStringLiteral("Cut"))
            cut = a;
    }
    QVERIFY(cut);
    cut->trigger();
    QCOMPARE(edit->text(), QString());
}

void TestAppShell::noNetworkModule()
{
#ifdef QT_NETWORK_LIB
    QFAIL("Qt Network must not be linked");
#endif
    const QStringList before = QCoreApplication::libraryPaths();
    TestContext t;
    MainWindow w(*t.ctx);
    QCOMPARE(QCoreApplication::libraryPaths(), before);
}

void TestAppShell::messageBarWrapsLongText()
{
    // A long message wraps at the real text width: no cut line and no empty line, in a narrow
    // and a wide window. The text label has a new QSizePolicy with no height-for-width flag, but
    // QLabel::setText sets the flag again for a word-wrapped label, so the bar layout uses
    // heightForWidth.
    TestContext t;
    MainWindow w(*t.ctx);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    MessageBar *bar = w.messageBar();
    auto *text = bar->findChild<QLabel *>(QStringLiteral("messageBarText"));
    QVERIFY(text);
    const QString message = QStringLiteral(
            "Could not rename 3 items because the files are open in another program or the folder "
            "is read-only. Close the program and try again, or choose another folder. Could not "
            "rename 3 items because the files are open in another program or the folder is "
            "read-only. Close the program and try again, or choose another folder.");
    bar->showMessage(StateIcon::Error, message, {});
    for (int width : {900, 1600}) {
        w.resize(width, 860);
        QTRY_COMPARE(w.width(), width);
        QCoreApplication::processEvents();
        const QFontMetrics fm(text->font());
        const int lines =
                fm.boundingRect(QRect(0, 0, text->contentsRect().width(), QWIDGETSIZE_MAX),
                                Qt::TextWordWrap, message)
                        .height() /
                fm.lineSpacing();
        const QString info = QStringLiteral("window %1, text %2 x %3, %4 lines of %5")
                                     .arg(width)
                                     .arg(text->width())
                                     .arg(text->height())
                                     .arg(lines)
                                     .arg(fm.lineSpacing());
        QVERIFY2(lines > 1, qPrintable(info));
        QCOMPARE(text->height(), text->heightForWidth(text->width()));
        QVERIFY2(text->contentsRect().height() >= lines * fm.lineSpacing(), qPrintable(info));
        QVERIFY2(text->contentsRect().height() < (lines + 1) * fm.lineSpacing(), qPrintable(info));
    }
}

QTEST_MAIN(TestAppShell)
#include "tst_app_shell.moc"
