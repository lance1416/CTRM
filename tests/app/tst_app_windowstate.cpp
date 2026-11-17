// SPDX-License-Identifier: GPL-3.0-or-later
#include "actions.h"
#include "ui/windowstate.h"
#include "widgets/actionbar.h"
#include "widgets/flowstrip.h"
#include "widgets/messagebar.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QToolButton>
#include <QtTest>

using namespace ctrm;
using namespace ctrm::app;

Q_DECLARE_METATYPE(ctrm::app::WindowState)
Q_DECLARE_METATYPE(ctrm::app::PrimaryTarget)
Q_DECLARE_METATYPE(ctrm::app::WindowStateInput)
Q_DECLARE_METATYPE(ctrm::PreviewSummary)

namespace {

PreviewSummary summary(int willChange, int conflicts = 0, int errors = 0, int warnings = 0,
                       int excluded = 0, int unchanged = 0)
{
    PreviewSummary s;
    s.willChange = willChange;
    s.conflicts = conflicts;
    s.errors = errors;
    s.warnings = warnings;
    s.excluded = excluded;
    s.unchanged = unchanged;
    return s;
}

WindowStateInput input(int items, int rules, const PreviewSummary &s)
{
    WindowStateInput in;
    in.itemCount = items;
    in.enabledRuleCount = rules;
    in.summary = s;
    return in;
}

// One input for each window state of ui-design 5.2.
QList<std::pair<const char *, WindowStateInput>> allStates()
{
    WindowStateInput running = input(10, 1, summary(10));
    running.running = true;
    WindowStateInput result = input(10, 1, summary(0, 0, 0, 0, 0, 10));
    result.resultShown = true;
    return {
            {"no items", input(0, 0, summary(0))},
            {"no rules", input(5, 0, summary(0, 0, 0, 0, 0, 5))},
            {"no change", input(5, 1, summary(0, 0, 0, 0, 0, 5))},
            {"ready", input(12, 1, summary(12))},
            {"blocked", input(6, 1, summary(2, 3, 1))},
            {"running", running},
            {"result", result},
    };
}

QLabel *visibleLabelWithText(QWidget *root, const QString &text)
{
    const auto labels = root->findChildren<QLabel *>();
    for (QLabel *label : labels) {
        if (label->text().contains(text) && label->isVisibleTo(root))
            return label;
    }
    return nullptr;
}

} // namespace

class TestAppWindowState : public QObject
{
    Q_OBJECT
private slots:
    void states_data();
    void states();
    void renameTextFilesFolders();
    void blockedReason();
    void blockedWithNothingToChange();
    void summaryText_data();
    void summaryText();
    void flowSteps();
    void flowStripLooks();
    void flowStripClick();
    void oneDefaultButton();
    void fusionAccent();
    void disabledHasReason();
    void flowStripNoMouseFocus();
    void progressAfterOneSecond();
    void problemLinks();
    void summaryAnnouncedAfterDelay();
    void messageBar();
};

void TestAppWindowState::states_data()
{
    QTest::addColumn<WindowStateInput>("in");
    QTest::addColumn<WindowState>("state");
    QTest::addColumn<PrimaryTarget>("primary");
    QTest::addColumn<bool>("renameEnabled");
    QTest::addColumn<QString>("reason");
    QTest::addColumn<bool>("skipVisible");

    const auto states = allStates();
    QTest::newRow("no items") << states[0].second << WindowState::NoItems << PrimaryTarget::AddFiles
                              << false << QStringLiteral("Add files to start.") << false;
    QTest::newRow("no rules") << states[1].second << WindowState::NoRules << PrimaryTarget::AddRule
                              << false << QStringLiteral("Add a rule to change the names.")
                              << false;
    QTest::newRow("no change") << states[2].second << WindowState::NoChange << PrimaryTarget::None
                               << false << QStringLiteral("No name changes. Edit the rules.")
                               << false;
    QTest::newRow("ready") << states[3].second << WindowState::Ready << PrimaryTarget::Rename
                           << true << QString() << false;
    QTest::newRow("blocked") << states[4].second << WindowState::Blocked << PrimaryTarget::None
                             << false << QStringLiteral("3 conflicts and 1 error block the rename.")
                             << true;
    QTest::newRow("running") << states[5].second << WindowState::Running << PrimaryTarget::None
                             << false << QString() << false;
    QTest::newRow("result") << states[6].second << WindowState::Result << PrimaryTarget::None
                            << false << QStringLiteral("Nothing to rename.") << false;
}

void TestAppWindowState::states()
{
    QFETCH(WindowStateInput, in);
    const WindowStateInfo info = computeWindowState(in);
    QTEST(info.state, "state");
    QTEST(info.primary, "primary");
    QTEST(info.renameEnabled, "renameEnabled");
    QTEST(info.reason, "reason");
    QTEST(info.skipVisible, "skipVisible");
    if (info.skipVisible)
        QCOMPARE(info.skipText, QStringLiteral("Skip 4 and Rename..."));
}

void TestAppWindowState::renameTextFilesFolders()
{
    WindowStateInput in = input(12, 1, summary(12));
    QCOMPARE(computeWindowState(in).renameText, QStringLiteral("Rename 12 Files"));

    in.allFiles = false;
    QCOMPARE(computeWindowState(in).renameText, QStringLiteral("Rename 12 Items"));

    in = input(1, 1, summary(1));
    in.allFiles = false;
    in.allFolders = true;
    QCOMPARE(computeWindowState(in).renameText, QStringLiteral("Rename 1 Folder"));

    in = input(1, 1, summary(1));
    QCOMPARE(computeWindowState(in).renameText, QStringLiteral("Rename 1 File"));

    // With no change there is no number to show.
    in = input(0, 0, summary(0));
    QCOMPARE(computeWindowState(in).renameText, QStringLiteral("Rename"));
}

void TestAppWindowState::blockedReason()
{
    QCOMPARE(computeWindowState(input(6, 1, summary(2, 3, 1))).reason,
             QStringLiteral("3 conflicts and 1 error block the rename."));
    // One part with a count of 1: singular verb.
    QCOMPARE(computeWindowState(input(6, 1, summary(2, 1, 0))).reason,
             QStringLiteral("1 conflict blocks the rename."));
    QCOMPARE(computeWindowState(input(6, 1, summary(2, 0, 1))).reason,
             QStringLiteral("1 error blocks the rename."));
    // A count above 1, or two parts: plural verb.
    QCOMPARE(computeWindowState(input(6, 1, summary(2, 2, 0))).reason,
             QStringLiteral("2 conflicts block the rename."));
    QCOMPARE(computeWindowState(input(6, 1, summary(2, 0, 2))).reason,
             QStringLiteral("2 errors block the rename."));
    QCOMPARE(computeWindowState(input(6, 1, summary(2, 1, 1))).reason,
             QStringLiteral("1 conflict and 1 error block the rename."));
}

void TestAppWindowState::blockedWithNothingToChange()
{
    // Skip shows only when one or more items will change (ui-design 9.3).
    const WindowStateInfo info = computeWindowState(input(2, 1, summary(0, 2, 0)));
    QCOMPARE(info.state, WindowState::Blocked);
    QVERIFY(!info.skipVisible);
}

void TestAppWindowState::summaryText_data()
{
    QTest::addColumn<PreviewSummary>("s");
    QTest::addColumn<QString>("text");
    QTest::newRow("example") << summary(12, 2, 1, 1, 0, 0)
                             << QStringLiteral(
                                        "12 will change · 2 conflicts · 1 error · 1 warning");
    QTest::newRow("all") << summary(1, 1, 2, 2, 3, 4)
                         << QStringLiteral("1 will change · 1 conflict · 2 errors · 2 warnings · "
                                           "3 excluded · 4 unchanged");
    QTest::newRow("zero parts left out")
            << summary(0, 0, 0, 0, 2, 5) << QStringLiteral("2 excluded · 5 unchanged");
    QTest::newRow("no items") << summary(0) << QStringLiteral("No items");
}

void TestAppWindowState::summaryText()
{
    QFETCH(PreviewSummary, s);
    QTEST(countSummaryText(s), "text");
}

void TestAppWindowState::flowSteps()
{
    WindowStateInfo info = computeWindowState(input(12, 1, summary(12)));
    QCOMPARE(info.stepDone, (std::array<bool, 4>{true, true, true, false}));
    QCOMPARE(info.currentStep, 3);

    info = computeWindowState(input(0, 0, summary(0)));
    QCOMPARE(info.stepDone, (std::array<bool, 4>{false, false, false, false}));
    QCOMPARE(info.currentStep, 0);

    // Rules but no items: step 2 is done, step 1 is current.
    info = computeWindowState(input(0, 2, summary(0)));
    QCOMPARE(info.stepDone, (std::array<bool, 4>{false, true, false, false}));
    QCOMPARE(info.currentStep, 0);

    info = computeWindowState(input(6, 1, summary(2, 3, 1)));
    QCOMPARE(info.stepDone, (std::array<bool, 4>{true, true, false, false}));
    QCOMPARE(info.currentStep, 2);

    WindowStateInput in = input(12, 1, summary(12));
    in.resultShown = true;
    info = computeWindowState(in);
    QCOMPARE(info.stepDone, (std::array<bool, 4>{true, true, true, true}));
    QCOMPARE(info.currentStep, -1);
}

void TestAppWindowState::flowStripLooks()
{
    FlowStrip strip(nullptr);
    strip.setState(computeWindowState(input(6, 1, summary(2, 3, 1))));
    const auto buttons = strip.findChildren<QToolButton *>();
    QCOMPARE(buttons.size(), 4);
    QCOMPARE(buttons[0]->text(), QStringLiteral("Add files"));
    QCOMPARE(buttons[3]->text(), QStringLiteral("Rename"));
    QCOMPARE(buttons[0]->accessibleName(), QStringLiteral("Step 1 of 4, Add files, done"));
    QCOMPARE(buttons[2]->accessibleName(), QStringLiteral("Step 3 of 4, Check preview, current"));
    QCOMPARE(buttons[3]->accessibleName(), QStringLiteral("Step 4 of 4, Rename, later"));

    QVERIFY(buttons[2]->font().weight() > buttons[0]->font().weight());
    QCOMPARE(buttons[3]->palette().color(QPalette::ButtonText),
             strip.palette().color(QPalette::PlaceholderText));
    for (QToolButton *b : buttons) {
        QVERIFY(b->autoRaise());
        QCOMPARE(b->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
        QVERIFY(!b->icon().isNull());
    }
    QCOMPARE(strip.sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
}

void TestAppWindowState::flowStripClick()
{
    FlowStrip strip(nullptr);
    QSignalSpy spy(&strip, &FlowStrip::stepClicked);
    const auto buttons = strip.findChildren<QToolButton *>();
    buttons[2]->click();
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 2);
}

void TestAppWindowState::oneDefaultButton()
{
    QWidget parent;
    const Actions actions = createActions(&parent);
    ActionBar bar(actions, &parent);

    bar.setState(computeWindowState(input(12, 1, summary(12))), summary(12));
    QVERIFY(bar.renameButton()->isDefault());
    QVERIFY(bar.renameButton()->isEnabled());
    QVERIFY(actions.rename->isEnabled());
    QCOMPARE(bar.renameButton()->text(), QStringLiteral("Rename 12 Files"));
    for (QPushButton *b : bar.findChildren<QPushButton *>())
        QVERIFY(b == bar.renameButton() || !b->isDefault());

    const PreviewSummary blocked = summary(2, 3, 1);
    bar.setState(computeWindowState(input(6, 1, blocked)), blocked);
    for (QPushButton *b : bar.findChildren<QPushButton *>())
        QVERIFY(!b->isDefault());
    QVERIFY(!bar.renameButton()->isEnabled());
    QVERIFY(!actions.rename->isEnabled());
    QVERIFY(visibleLabelWithText(&bar,
                                 QStringLiteral("3 conflicts and 1 error block the rename.")));
    QVERIFY(bar.skipButton()->isVisibleTo(&bar));
    QCOMPARE(bar.skipButton()->text(), QStringLiteral("Skip 4 and Rename..."));
    QVERIFY(actions.skipAndRename->isEnabled());

    // A click on a button triggers its action.
    QSignalSpy skipSpy(actions.skipAndRename, &QAction::triggered);
    bar.skipButton()->click();
    QCOMPARE(skipSpy.size(), 1);

    WindowStateInput running = input(10, 1, summary(10));
    running.running = true;
    bar.setState(computeWindowState(running), summary(10));
    QVERIFY(!bar.renameButton()->isVisibleTo(&bar));
    QVERIFY(!bar.skipButton()->isVisibleTo(&bar));
}

void TestAppWindowState::fusionAccent()
{
    const QString oldStyle = QApplication::style()->name();
    QApplication::setStyle(QStringLiteral("Fusion"));
    {
        QWidget parent;
        const Actions actions = createActions(&parent);
        ActionBar bar(actions, &parent);
        const QPalette normal = bar.palette();

        bar.setState(computeWindowState(input(12, 1, summary(12))), summary(12));
        QPushButton *rename = bar.renameButton();
        QCOMPARE(rename->palette().color(QPalette::Button), normal.color(QPalette::Accent));
        QCOMPARE(rename->palette().color(QPalette::ButtonText),
                 normal.color(QPalette::HighlightedText));
        QCOMPARE(bar.skipButton()->palette().color(QPalette::Button),
                 normal.color(QPalette::Button));

        // Not primary: the normal palette again.
        const PreviewSummary blocked = summary(2, 3, 1);
        bar.setState(computeWindowState(input(6, 1, blocked)), blocked);
        QCOMPARE(rename->palette().color(QPalette::Button), normal.color(QPalette::Button));
    }
    QApplication::setStyle(oldStyle);
}

void TestAppWindowState::disabledHasReason()
{
    QWidget parent;
    const Actions actions = createActions(&parent);
    ActionBar bar(actions, &parent);
    for (const auto &[name, in] : allStates()) {
        const WindowStateInfo info = computeWindowState(in);
        // In the Running state the Rename button is hidden (ui-design 5.2).
        if (info.renameEnabled || info.state == WindowState::Running)
            continue;
        QVERIFY2(!info.reason.isEmpty(), name);
        bar.setState(info, in.summary);
        QCOMPARE(bar.renameButton()->toolTip(), info.reason);
        QCOMPARE(bar.renameButton()->accessibleDescription(), info.reason);
        QVERIFY2(visibleLabelWithText(&bar, info.reason), name);
    }
}

void TestAppWindowState::flowStripNoMouseFocus()
{
    FlowStrip strip(nullptr);
    QCOMPARE(strip.focusPolicy(), Qt::TabFocus);
    for (QToolButton *b : strip.findChildren<QToolButton *>())
        QCOMPARE(b->focusPolicy(), Qt::TabFocus);
}

void TestAppWindowState::progressAfterOneSecond()
{
    QWidget parent;
    const Actions actions = createActions(&parent);
    ActionBar bar(actions, &parent);
    parent.show();
    parent.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&parent));

    auto *progress = bar.findChild<QProgressBar *>(QStringLiteral("progressBar"));
    auto *label = bar.findChild<QLabel *>(QStringLiteral("progressLabel"));
    auto *cancel = bar.findChild<QPushButton *>(QStringLiteral("cancelButton"));
    QVERIFY(progress && label && cancel);

    QElapsedTimer clock;
    clock.start();
    bar.showProgress(1, 10);
    QVERIFY(!progress->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(progress->isVisible(), 3000);
    QVERIFY(clock.elapsed() >= 1000);
    QCOMPARE(label->text(), QStringLiteral("Renaming 1 of 10..."));
    QCOMPARE(progress->value(), 1);
    QCOMPARE(progress->maximum(), 10);
    QCOMPARE(parent.focusWidget(), cancel);

    QSignalSpy cancelSpy(actions.cancelBatch, &QAction::triggered);
    cancel->click();
    QCOMPARE(cancelSpy.size(), 1);

    bar.showProgress(2, 10);
    QCOMPARE(label->text(), QStringLiteral("Renaming 2 of 10..."));

    bar.hideProgress();
    QVERIFY(!progress->isVisible());
    QVERIFY(!actions.cancelBatch->isEnabled());

    // A new batch waits 1 s again.
    bar.showProgress(0, 5);
    QVERIFY(!progress->isVisible());
    bar.hideProgress();
}

void TestAppWindowState::problemLinks()
{
    QWidget parent;
    const Actions actions = createActions(&parent);
    ActionBar bar(actions, &parent);
    QSignalSpy problems(&bar, &ActionBar::problemLinkClicked);
    QSignalSpy warnings(&bar, &ActionBar::warningsLinkClicked);

    const PreviewSummary s = summary(12, 2, 1, 1);
    bar.setState(computeWindowState(input(16, 1, s)), s);

    QLabel *conflicts = visibleLabelWithText(&bar, QStringLiteral("2 conflicts"));
    QLabel *warning = visibleLabelWithText(&bar, QStringLiteral("1 warning"));
    QVERIFY(conflicts && warning);
    emit conflicts->linkActivated(QStringLiteral("problems"));
    emit warning->linkActivated(QStringLiteral("warnings"));
    QCOMPARE(problems.size(), 1);
    QCOMPARE(warnings.size(), 1);

    // Previous and Next problem show only when there are blocking problems (7.6).
    auto *next = bar.findChild<QToolButton *>(QStringLiteral("nextProblemButton"));
    QVERIFY(next && next->isVisibleTo(&bar));
    QCOMPARE(next->defaultAction(), actions.nextProblem);
    const PreviewSummary onlyWarnings = summary(12, 0, 0, 1);
    bar.setState(computeWindowState(input(13, 1, onlyWarnings)), onlyWarnings);
    QVERIFY(!next->isVisibleTo(&bar));
}

void TestAppWindowState::summaryAnnouncedAfterDelay()
{
    QWidget parent;
    const Actions actions = createActions(&parent);
    ActionBar bar(actions, &parent);
    auto *summaryWidget = bar.findChild<QWidget *>(QStringLiteral("countSummary"));
    QVERIFY(summaryWidget);

    const PreviewSummary s = summary(12, 2, 1, 1);
    bar.setState(computeWindowState(input(16, 1, s)), s);
    QCOMPARE(summaryWidget->accessibleName(), countSummaryText(s));

    auto *timer = bar.findChild<QTimer *>(QStringLiteral("announceTimer"));
    QVERIFY(timer);
    QVERIFY(timer->isActive());
    QCOMPARE(timer->interval(), 500);
    QVERIFY(timer->isSingleShot());
}

void TestAppWindowState::messageBar()
{
    QWidget parent;
    MessageBar bar(&parent);
    QVERIFY(!bar.isVisibleTo(&parent));

    auto *undo = new QPushButton(QStringLiteral("Undo"));
    bar.showMessage(StateIcon::Done, QStringLiteral("Renamed <b>42</b> items."), {undo});
    QVERIFY(bar.isVisibleTo(&parent));
    QCOMPARE(undo->parentWidget()->window(), bar.window());
    QLabel *text = visibleLabelWithText(&bar, QStringLiteral("Renamed <b>42</b> items."));
    QVERIFY(text);
    QCOMPARE(text->textFormat(), Qt::PlainText);
    QVERIFY(!undo->isDefault());

    // A button that is in the new message too stays.
    bar.showMessage(StateIcon::Done, QStringLiteral("Again"), {undo});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(undo->isVisibleTo(&bar));
    QCOMPARE(undo->parentWidget()->window(), bar.window());

    // A new message replaces the old buttons.
    QPointer<QPushButton> old = undo;
    bar.showMessage(StateIcon::Warning, QStringLiteral("Tip"), {});
    QTRY_VERIFY(old.isNull());

    QSignalSpy closed(&bar, &MessageBar::closed);
    auto *close = bar.findChild<QToolButton *>(QStringLiteral("messageBarClose"));
    QVERIFY(close);
    QVERIFY(!close->accessibleName().isEmpty());
    close->click();
    QCOMPARE(closed.size(), 1);
    QVERIFY(!bar.isVisibleTo(&parent));

    bar.showMessage(StateIcon::Done, QStringLiteral("x"), {});
    bar.clear();
    QVERIFY(!bar.isVisibleTo(&parent));
    QCOMPARE(closed.size(), 1); // clear() is not a close by the user
}

QTEST_MAIN(TestAppWindowState)
#include "tst_app_windowstate.moc"
