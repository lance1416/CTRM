// SPDX-License-Identifier: GPL-3.0-or-later
#include "actions.h"
#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/tokens.h"
#include "ui/windowstate.h"
#include "widgets/actionbar.h"
#include "widgets/flowstrip.h"
#include "widgets/messagebar.h"

#include <QAbstractButton>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QEnterEvent>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
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
    void flowStripSpacing();
    void flowStripPassive();
    void oneDefaultButton();
    void actionBarHeightStays();
    void blockedBarNotCut_data();
    void blockedBarNotCut();
    void barFitsAfterContentChange();
    void tipLinkHasBodyFont();
    void resultSummary();
    void fusionAccent();
    void disabledHasReason();
    void progressAfterOneSecond();
    void problemLinks();
    void summaryAnnouncedAfterDelay();
    void messageBar();
    void messageBarOneLine();
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
    // S01: three steps. 1 = the list has items; 2 = one or more items will change; 3 = the
    // result state shows. The current step is the first step that is not done.
    using Steps = std::array<bool, 3>;
    WindowStateInfo info = computeWindowState(input(12, 1, summary(12)));
    QCOMPARE(info.stepDone, (Steps{true, true, false}));
    QCOMPARE(info.currentStep, 2);

    info = computeWindowState(input(0, 0, summary(0)));
    QCOMPARE(info.stepDone, (Steps{false, false, false}));
    QCOMPARE(info.currentStep, 0);

    // Rules but no items: nothing will change, so step 2 is not done.
    info = computeWindowState(input(0, 2, summary(0)));
    QCOMPARE(info.stepDone, (Steps{false, false, false}));
    QCOMPARE(info.currentStep, 0);

    // A rule that changes nothing (an empty Insert): step 2 is current.
    info = computeWindowState(input(6, 1, summary(0)));
    QCOMPARE(info.stepDone, (Steps{true, false, false}));
    QCOMPARE(info.currentStep, 1);

    // Problems are not a step.
    WindowStateInput in;
    info = computeWindowState(input(6, 1, summary(2, 3, 1)));
    QCOMPARE(info.stepDone, (Steps{true, true, false}));
    QCOMPARE(info.currentStep, 2);

    // All changes are blocked (2 items get the same name): step 2 is done, step 3 is current.
    in = input(2, 1, summary(0, 2));
    in.blockedChanges = 2;
    info = computeWindowState(in);
    QCOMPARE(info.stepDone, (Steps{true, true, false}));
    QCOMPARE(info.currentStep, 2);

    // After a batch nothing will change, but all steps are done.
    in = input(12, 1, summary(0));
    in.resultShown = true;
    info = computeWindowState(in);
    QCOMPARE(info.stepDone, (Steps{true, true, true}));
    QCOMPARE(info.currentStep, -1);
}

void TestAppWindowState::flowStripLooks()
{
    // O03b, O01, C13 (ui-design 5.3): labels only; one accessible element "Steps" with the
    // current step as its description.
    FlowStrip strip(nullptr);
    strip.setState(computeWindowState(input(6, 1, summary(0))));
    QVERIFY(strip.findChildren<QAbstractButton *>().isEmpty());
    const auto text = [&](int step) {
        return strip.findChild<QLabel *>(QStringLiteral("flowStep%1").arg(step));
    };
    QCOMPARE(text(1)->text(), QStringLiteral("Add files"));
    QCOMPARE(text(2)->text(), QStringLiteral("Add rules"));
    QCOMPARE(text(3)->text(), QStringLiteral("Rename"));
    QCOMPARE(strip.accessibleName(), QStringLiteral("Steps"));
    QCOMPARE(strip.accessibleDescription(), QStringLiteral("Step 2 of 3, Add rules, current"));
    QAccessibleInterface *a = QAccessible::queryAccessibleInterface(&strip);
    QVERIFY(a);
    QCOMPARE(a->role(), QAccessible::Grouping);
    QCOMPARE(a->text(QAccessible::Name), QStringLiteral("Steps"));
    QCOMPARE(a->text(QAccessible::Description), QStringLiteral("Step 2 of 3, Add rules, current"));
    QCOMPARE(a->childCount(), 0);

    QVERIFY(text(2)->font().weight() > text(1)->font().weight());
    QCOMPARE(text(3)->palette().color(QPalette::WindowText),
             strip.palette().color(QPalette::PlaceholderText));
    for (int step = 1; step <= 3; ++step) {
        auto *icon = strip.findChild<QLabel *>(QStringLiteral("flowStep%1Icon").arg(step));
        QVERIFY(icon);
        QVERIFY(!icon->pixmap().isNull());
        QCOMPARE(icon->pixmap().deviceIndependentSize().toSize(),
                 QSize(tokens::iconS, tokens::iconS));
    }
    const auto separators = strip.findChildren<QLabel *>(QStringLiteral("flowSeparator"));
    QCOMPARE(separators.size(), 2);
    for (QLabel *separator : separators)
        QCOMPARE(separator->pixmap().deviceIndependentSize().toSize(),
                 QSize(tokens::iconXs, tokens::iconXs));
    QCOMPARE(strip.sizePolicy().verticalPolicy(), QSizePolicy::Fixed);

    // R5-01: the current step number is on-accent (white) on accent-fill in both modes, also
    // when the palette gives a near-black highlighted text (macOS light mode).
    for (const bool dark : {false, true}) {
        QPalette p;
        p.setColor(QPalette::Window, dark ? QColor(0x2B2B2D) : QColor(0xFFFFFF));
        p.setColor(QPalette::Base, dark ? QColor(0x1E1E1E) : QColor(0xFFFFFF));
        p.setColor(QPalette::Text, dark ? QColor(0xE3E3E3) : QColor(0x1D1D1F));
        p.setColor(QPalette::WindowText, p.color(QPalette::Text));
        p.setColor(QPalette::Accent, dark ? QColor(0x4C9AFF) : QColor(0x0A60FF));
        p.setColor(QPalette::HighlightedText, dark ? QColor(0xFFFFFF) : QColor(0x020F27));
        strip.setPalette(p);
        strip.setState(computeWindowState(input(6, 1, summary(0))));
        const CtrmColors c = CtrmColors::fromPalette(p);
        const QImage icon = strip.findChild<QLabel *>(QStringLiteral("flowStep2Icon"))
                                    ->pixmap()
                                    .toImage()
                                    .convertToFormat(QImage::Format_ARGB32);
        const auto close = [](const QColor &a, const QColor &b) {
            return qAbs(a.red() - b.red()) <= 24 && qAbs(a.green() - b.green()) <= 24 &&
                   qAbs(a.blue() - b.blue()) <= 24;
        };
        bool fill = false;
        bool white = false;
        for (int y = 0; y < icon.height(); ++y) {
            for (int x = 0; x < icon.width(); ++x) {
                const QColor px = icon.pixelColor(x, y);
                if (px.alpha() < 250)
                    continue;
                fill = fill || close(px, c.accentFill);
                white = white || close(px, QColor(Qt::white));
            }
        }
        QVERIFY2(fill, dark ? "dark" : "light");
        QVERIFY2(white, dark ? "dark" : "light");
    }

    // All steps done: the description names the last step.
    WindowStateInput in = input(6, 1, summary(0));
    in.resultShown = true;
    strip.setState(computeWindowState(in));
    QCOMPARE(strip.accessibleDescription(), QStringLiteral("Step 3 of 3, Rename, done"));
}

void TestAppWindowState::flowStripSpacing()
{
    // O01 (5.3, design-tokens 4.2): inset space-3 and space-1; icon to text space-1; space-2 on
    // each side of each chevron.
    QWidget parent;
    auto *strip = new FlowStrip(&parent);
    strip->setState(computeWindowState(input(6, 1, summary(6))));
    strip->setGeometry(0, 0, 900, strip->sizeHint().height());
    parent.resize(1000, 100);
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    const auto geometry = [&](const QString &name) {
        return strip->findChild<QLabel *>(name)->geometry();
    };
    QCOMPARE(geometry(QStringLiteral("flowStep1Icon")).left(), tokens::space3);
    QCOMPARE(geometry(QStringLiteral("flowStep1Icon")).top(), tokens::space1);
    QCOMPARE(strip->height(), tokens::space1 * 2 + geometry(QStringLiteral("flowStep1")).height());
    const auto separators = strip->findChildren<QLabel *>(QStringLiteral("flowSeparator"));
    for (int step = 1; step <= 3; ++step) {
        const QRect icon = geometry(QStringLiteral("flowStep%1Icon").arg(step));
        const QRect text = geometry(QStringLiteral("flowStep%1").arg(step));
        QCOMPARE(text.left() - icon.right() - 1, tokens::space1);
        if (step < 3) {
            const QRect chevron = separators.at(step - 1)->geometry();
            const QRect next = geometry(QStringLiteral("flowStep%1Icon").arg(step + 1));
            QCOMPARE(chevron.left() - text.right() - 1, tokens::space2);
            QCOMPARE(next.left() - chevron.right() - 1, tokens::space2);
        }
    }
}

void TestAppWindowState::flowStripPassive()
{
    // O03b: no click, no hover, arrow cursor, no focus stop. A step change sends a name-change
    // event.
    QWidget parent;
    auto *field = new QLineEdit(&parent);
    auto *strip = new FlowStrip(&parent);
    strip->move(0, 40);
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    QCOMPARE(strip->focusPolicy(), Qt::NoFocus);
    for (QWidget *child : strip->findChildren<QWidget *>()) {
        QCOMPARE(child->focusPolicy(), Qt::NoFocus);
        QCOMPARE(child->cursor().shape(), Qt::ArrowCursor);
    }
    QCOMPARE(strip->cursor().shape(), Qt::ArrowCursor);
    QVERIFY(!strip->testAttribute(Qt::WA_Hover));

    field->setFocus();
    QTest::mouseClick(strip->findChild<QLabel *>(QStringLiteral("flowStep2")), Qt::LeftButton);
    QCOMPARE(QApplication::focusWidget() == field || QApplication::focusWidget() == nullptr, true);

    static QList<QAccessible::Event> events;
    events.clear();
    QAccessible::installUpdateHandler([](QAccessibleEvent *event) {
        if (event->object() && event->object()->objectName() == u"flowStrip")
            events.append(event->type());
    });
    const auto restore = qScopeGuard([] { QAccessible::installUpdateHandler(nullptr); });
    strip->setState(computeWindowState(input(6, 1, summary(0))));
    QVERIFY(events.contains(QAccessible::NameChanged));
    events.clear();
    strip->setState(computeWindowState(input(6, 1, summary(0)))); // the same step
    QVERIFY(!events.contains(QAccessible::NameChanged));
}

void TestAppWindowState::resultSummary()
{
    // M10: the result state says how many items were renamed (ui-design 9.8).
    QWidget parent;
    const Actions actions = createActions(&parent);
    ActionBar bar(actions, &parent);
    WindowStateInput in = input(10, 1, summary(0, 0, 0, 0, 1, 9));
    in.resultShown = true;
    in.renamedCount = 6;
    bar.setState(computeWindowState(in), in.summary);
    QCOMPARE(bar.findChild<QWidget *>(QStringLiteral("countSummary"))->accessibleName(),
             QStringLiteral("6 renamed"));
    QStringList texts;
    for (QLabel *label :
         bar.findChild<QWidget *>(QStringLiteral("countSummary"))->findChildren<QLabel *>())
        if (!label->isHidden())
            texts.append(label->text());
    QCOMPARE(texts, QStringList{QStringLiteral("6 renamed")});
}

void TestAppWindowState::actionBarHeightStays()
{
    // R1-03 (5.5, 9.3): the action bar has one height in all states. The problem buttons and
    // the conflict tip do not move the table.
    QWidget parent;
    auto *layout = new QVBoxLayout(&parent);
    const Actions actions = createActions(&parent);
    auto *bar = new ActionBar(actions, &parent);
    layout->addWidget(new QWidget(&parent), 1);
    layout->addWidget(bar);
    parent.resize(1200, 600);
    bar->setState(computeWindowState(input(12, 1, summary(12))), summary(12));
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    const int ready = bar->height();

    const PreviewSummary blocked = summary(2, 3, 1);
    bar->setState(computeWindowState(input(6, 1, blocked)), blocked);
    bar->setConflictTip(true);
    QCoreApplication::processEvents();
    QCOMPARE(bar->height(), ready);

    bar->setState(computeWindowState(input(0, 0, summary(0))), summary(0));
    QCoreApplication::processEvents();
    QCOMPARE(bar->height(), ready);
}

void TestAppWindowState::blockedBarNotCut_data()
{
    QTest::addColumn<int>("width");
    QTest::newRow("1200") << 1200;
    QTest::newRow("900") << 900;
}

void TestAppWindowState::blockedBarNotCut()
{
    // R2-01 (5.2, 9.3): with conflicts and errors the count summary, Skip and Rename are never
    // cut. The bar gives up space in this order: "Same names:", then the reason text (elided,
    // then hidden), then the reason icon.
    QFETCH(int, width);
    QWidget parent;
    parent.resize(width + 100, 200);
    const Actions actions = createActions(&parent);
    auto *bar = new ActionBar(actions, &parent);
    const PreviewSummary blocked = summary(2, 6, 1, 0, 0, 1);
    bar->setState(computeWindowState(input(10, 1, blocked)), blocked);
    bar->setConflictTip(true);
    bar->setGeometry(0, 0, width, bar->sizeHint().height());
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    QCoreApplication::processEvents();

    auto *summaryWidget = bar->findChild<QWidget *>(QStringLiteral("countSummary"));
    QStringList cut;
    for (QLabel *label : summaryWidget->findChildren<QLabel *>()) {
        if (label->isVisible() && label->width() < label->sizeHint().width())
            cut.append(label->text());
    }
    QVERIFY2(cut.isEmpty(), qPrintable(cut.join(u", ")));
    QVERIFY(summaryWidget->geometry().right() <
            bar->findChild<QWidget *>(QStringLiteral("previousProblemButton"))->geometry().left());
    for (const char *name : {"skipButton", "renameButton"}) {
        auto *button = bar->findChild<QPushButton *>(QLatin1StringView(name));
        QVERIFY(button->isVisible());
        QVERIFY2(button->width() >= button->sizeHint().width(), name);
        QVERIFY(button->mapTo(bar, QPoint(button->width(), 0)).x() <= bar->width());
    }
    auto *link = bar->findChild<QToolButton *>(QStringLiteral("addNumberingButton"));
    QVERIFY(link->isVisible());
    QVERIFY(link->width() >= link->sizeHint().width());

    // The order: the reason text gives up space only when "Same names:" is hidden.
    auto *reason = bar->findChild<QLabel *>(QStringLiteral("renameReason"));
    auto *tip = bar->findChild<QLabel *>(QStringLiteral("conflictTipText"));
    const QString full = computeWindowState(input(10, 1, blocked)).reason;
    const bool reasonShort = !reason->isVisible() || reason->text() != full;
    if (reasonShort)
        QVERIFY(!tip->isVisible());
    if (!bar->findChild<QLabel *>(QStringLiteral("renameReasonIcon"))->isVisible())
        QVERIFY(!reason->isVisible());
    if (reason->isVisible())
        QVERIFY(reason->width() >= reason->sizeHint().width());
    QCOMPARE(bar->findChild<QPushButton *>(QStringLiteral("renameButton"))->toolTip(), full);
}

void TestAppWindowState::barFitsAfterContentChange()
{
    // R3-01 (5.2): the bar runs the space order also when its content changes at the same
    // width (for example, Refresh adds an error).
    QWidget parent;
    parent.resize(1300, 200);
    const Actions actions = createActions(&parent);
    auto *bar = new ActionBar(actions, &parent);
    const PreviewSummary conflicts = summary(2, 6, 0, 0, 0, 1);
    bar->setState(computeWindowState(input(9, 1, conflicts)), conflicts);
    bar->setConflictTip(true);
    bar->setGeometry(0, 0, 1000, bar->sizeHint().height());
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    QCoreApplication::processEvents();

    const PreviewSummary both = summary(2, 6, 1, 0, 0, 1);
    bar->setState(computeWindowState(input(10, 1, both)), both);
    bar->setConflictTip(true);
    QCoreApplication::processEvents();

    auto *summaryWidget = bar->findChild<QWidget *>(QStringLiteral("countSummary"));
    QStringList cut;
    for (QLabel *label : summaryWidget->findChildren<QLabel *>()) {
        if (label->isVisible() && label->width() < label->sizeHint().width())
            cut.append(label->text());
    }
    QVERIFY2(cut.isEmpty(), qPrintable(cut.join(u", ")));
    for (const char *name : {"skipButton", "renameButton"}) {
        auto *button = bar->findChild<QPushButton *>(QLatin1StringView(name));
        QVERIFY2(button->width() >= button->sizeHint().width(), name);
    }
}

void TestAppWindowState::tipLinkHasBodyFont()
{
    // R1-04 (9.3): "Add Numbering" has the body font, also when the style gives tool buttons a
    // smaller font (macOS).
    const QFont toolFont = QApplication::font("QToolButton");
    QFont small = QApplication::font();
    small.setPointSizeF(QApplication::font().pointSizeF() * 0.75);
    QApplication::setFont(small, "QToolButton");
    const auto restore = qScopeGuard([&] { QApplication::setFont(toolFont, "QToolButton"); });

    QWidget parent;
    const Actions actions = createActions(&parent);
    ActionBar bar(actions, &parent);
    const auto *tip = bar.findChild<QLabel *>(QStringLiteral("conflictTipText"));
    const auto *link = bar.findChild<QToolButton *>(QStringLiteral("addNumberingButton"));
    QVERIFY(tip && link);
    QCOMPARE(link->font().pointSizeF(), tip->font().pointSizeF());
    QCOMPARE(tip->font().pointSizeF(), QApplication::font().pointSizeF());
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

    // R5-06 (design-tokens 4.5): a link has no underline at rest; the underline shows on hover.
    const auto underlined = [](const QLabel *label) {
        return !label->text().contains(QLatin1StringView("text-decoration:none"));
    };
    QVERIFY(!underlined(conflicts));
    QEnterEvent enter(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
    QCoreApplication::sendEvent(conflicts, &enter);
    QVERIFY(underlined(conflicts));
    QVERIFY(conflicts->text().contains(QLatin1StringView("text-decoration:underline")));
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(conflicts, &leave);
    QVERIFY(!underlined(conflicts));
    QCOMPARE(visibleLabelWithText(&bar, QStringLiteral("2 conflicts")), conflicts);

    // Previous and Next problem show only when there are blocking problems (7.6).
    auto *next = bar.findChild<QToolButton *>(QStringLiteral("nextProblemButton"));
    QVERIFY(next && next->isVisibleTo(&bar));
    QCOMPARE(next->defaultAction(), actions.nextProblem);
    // C07: icon buttons of hit-min with a designer icon of icon-s, also after the action
    // changes (the button copies the empty action icon then).
    auto *previous = bar.findChild<QToolButton *>(QStringLiteral("previousProblemButton"));
    for (QToolButton *button : {previous, next}) {
        QCOMPARE(button->size(), QSize(tokens::hitMin, tokens::hitMin));
        QCOMPARE(button->iconSize(), QSize(tokens::iconS, tokens::iconS));
        QCOMPARE(button->arrowType(), Qt::NoArrow);
        QVERIFY(button->property("iconButton").toBool());
        QVERIFY(!button->icon().isNull());
    }
    actions.nextProblem->setEnabled(!actions.nextProblem->isEnabled());
    QVERIFY(!next->icon().isNull());
    QVERIFY(actions.nextProblem->icon().isNull());
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
    // C15: a light action: a borderless button with `text` (see tst_app_messagebar).
    QVERIFY(undo->isFlat());
    QCOMPARE(undo->palette().color(QPalette::ButtonText), bar.palette().color(QPalette::Text));

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
    // C07: an icon button of hit-min with an icon of the set.
    QCOMPARE(close->size(), QSize(tokens::hitMin, tokens::hitMin));
    QCOMPARE(close->iconSize(), QSize(tokens::iconS, tokens::iconS));
    QVERIFY(close->property("iconButton").toBool());
    QVERIFY(!close->icon().isNull());
    // R5-07: the plain x (tool-close), not the x in a circle (tool-clear).
    const auto image = [](const QIcon &icon) {
        return icon.pixmap(QSize(tokens::iconS, tokens::iconS), 1.0).toImage();
    };
    QCOMPARE(image(close->icon()), image(toolIcon(ToolIcon::Close, bar.palette())));
    QVERIFY(image(close->icon()) != image(toolIcon(ToolIcon::Clear, bar.palette())));
    close->click();
    QCOMPARE(closed.size(), 1);
    QVERIFY(!bar.isVisibleTo(&parent));

    bar.showMessage(StateIcon::Done, QStringLiteral("x"), {});
    bar.clear();
    QVERIFY(!bar.isVisibleTo(&parent));
    QCOMPARE(closed.size(), 1); // clear() is not a close by the user
}

void TestAppWindowState::messageBarOneLine()
{
    // M11, O07: icon, text, buttons and close on one centre line; inset space-2.
    QWidget parent;
    auto *layout = new QVBoxLayout(&parent);
    auto *bar = new MessageBar(&parent);
    layout->addWidget(bar);
    layout->addStretch(1);
    parent.resize(800, 300);
    auto *undo = new QPushButton(QStringLiteral("Undo"));
    bar->showMessage(StateIcon::Done, QStringLiteral("Renamed 6 items."), {undo});
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));

    auto *icon = bar->findChild<QLabel *>(QStringLiteral("messageBarIcon"));
    auto *text = bar->findChild<QLabel *>(QStringLiteral("messageBarText"));
    auto *close = bar->findChild<QToolButton *>(QStringLiteral("messageBarClose"));
    const auto centreY = [bar](QWidget *w) { return w->mapTo(bar, w->rect().center()).y(); };
    const auto textCentre = [&] {
        // The centre of the text line, not of the label.
        const int line = text->fontMetrics().height();
        const QRect r = QStyle::alignedRect(text->layoutDirection(), text->alignment(),
                                            QSize(text->width(), line), text->contentsRect());
        return text->mapTo(bar, r.center()).y();
    };
    QVERIFY(qAbs(centreY(icon) - textCentre()) <= 1);
    QVERIFY(qAbs(centreY(undo) - textCentre()) <= 1);
    QVERIFY(qAbs(centreY(close) - textCentre()) <= 1);
    const int tallest = qMax(undo->height(), close->height());
    QVERIFY2(bar->height() <= tallest + 2 * tokens::space2 + 1,
             qPrintable(QString::number(bar->height())));
}

QTEST_MAIN(TestAppWindowState)
#include "tst_app_windowstate.moc"
