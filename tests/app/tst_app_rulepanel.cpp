// SPDX-License-Identifier: GPL-3.0-or-later
#include "actions.h"
#include "layoutedge.h"
#include "models/rulelistmodel.h"
#include "rules/chainoptionswidget.h"
#include "rules/changecaserule.h"
#include "rules/cleanupeditor.h"
#include "rules/extensioneditor.h"
#include "rules/insertrule.h"
#include "rules/numberingeditor.h"
#include "rules/numberingrule.h"
#include "rules/replacerule.h"
#include "rules/ruleeditor.h"
#include "rules/ruleeditorfactory.h"
#include "rules/ruleregistry.h"
#include "rules/unsupportedrule.h"
#include "theme/tokens.h"
#include "widgets/formutils.h"
#include "widgets/rulepanel.h"
#include "widgets/tertiarybutton.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QtTest>

#include <memory>

using namespace ctrm;
using namespace ctrm::app;

namespace {

struct Fixture
{
    QWidget window;
    Actions actions = createActions(&window);
    RuleListModel model;
    RulePanel *panel = nullptr;

    explicit Fixture(const QStringList &types = {})
    {
        RuleChain chain;
        for (const QString &type : types)
            chain.insert(chain.size(), createRule(type));
        model.setChain(chain);
        auto *layout = new QVBoxLayout(&window);
        panel = new RulePanel(model, actions, &window);
        layout->addWidget(panel);
        window.resize(360, 800);
        window.show();
        window.activateWindow();
    }

    void select(int row) { panel->ruleList()->setCurrentIndex(model.index(row)); }

    int currentRow() const { return panel->ruleList()->currentIndex().row(); }
};

int fieldEdge(QFormLayout *form)
{
    for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem *item = form->itemAt(row, QFormLayout::FieldRole);
        if (!item || !form->isRowVisible(row))
            continue;
        const QList<int> lefts = ctrm::test::fieldVisualLefts(item, form->parentWidget());
        if (!lefts.isEmpty())
            return lefts.first() - form->geometry().x(); // the visible edge
    }
    return -1;
}

} // namespace

class TestAppRulePanel : public QObject
{
    Q_OBJECT
private slots:
    void factory();
    void addRuleMenuSections();
    void addInsertsBelowSelection();
    void addAtEndWithoutSelection();
    void editorFollowsSelection();
    void editorWritesRuleAndSummary();
    void keyboardMove();
    void moveUpdatesTitle();
    void spaceToggles();
    void deleteKey();
    void duplicateAction();
    void unsupportedPage();
    void editorsShareLabelColumn();
    void tertiaryButtonsAccessible();
    void emptyStates();
    void chainOptionsAtBottom();
    void chainOptionsForwarded();
    void chainOptionsStableAndTitleAligned();
    void setChainResetsEditor();
    void numberedCountForwarded();
    void inactiveSelectionContrast();
    void listFollowsRows();
    void emptyStateLeftAndTop();
    void listShortcutsOnlyWithFocus();
    void primaryAddRuleIsPlainButton();
    void editorsFitPanelWidth();
    void editorFadeWhenCut();
    void listKeepsHeightWhenChainOpens();
    void minimumWidthFromEditorsOnly();
    void editorOnPanelSurface();
    void ruleToolButtons();
    void ruleRowLayout();
    void ruleRowBoxClickAndError();
    void ruleRowDisabledDimmed();
};

void TestAppRulePanel::factory()
{
    for (const QString &type : ruleTypes()) {
        std::unique_ptr<RuleEditor> editor(createRuleEditor(type, nullptr));
        QVERIFY2(editor, qPrintable(type));
        QCOMPARE(editor->ruleType(), type);
    }
    QCOMPARE(createRuleEditor(QStringLiteral("dateTime"), nullptr), nullptr);
}

void TestAppRulePanel::addRuleMenuSections()
{
    // ui-design 8.1 (S05): three groups with plain separators (macOS shows no section titles),
    // no "...", and a one-line tooltip and status tip on each item.
    Fixture f;
    QStringList items;
    QStringList tips;
    for (QAction *a : f.actions.addRuleMenu->actions()) {
        if (a->isSeparator()) {
            QVERIFY(a->text().isEmpty());
            items.append(QStringLiteral("---"));
            continue;
        }
        items.append(a->text());
        QCOMPARE(a->toolTip(), a->statusTip());
        tips.append(a->statusTip());
    }
    QCOMPARE(items, (QStringList{"Replace", "Insert", "Remove", "Clean up", "---", "Change case",
                                 "Extension", "---", "Numbering"}));
    QCOMPARE(tips, (QStringList{"Replace text or a pattern", "Add text at a position",
                                "Remove characters, a range or text between marks",
                                "Fix spaces and invalid characters", "Change the letter case",
                                "Change the extension", "Add a number to each item"}));
    QCOMPARE(f.panel->addRuleButton()->menu(), f.actions.addRuleMenu);
}

void TestAppRulePanel::addInsertsBelowSelection()
{
    Fixture f({"replace", "numbering"});
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.select(0);
    QAction *insert = f.actions.addRuleMenu->findChild<QAction *>(QStringLiteral("addRule_insert"));
    QVERIFY(insert);
    insert->trigger();
    QCOMPARE(f.model.rowCount(), 3);
    QCOMPARE(f.model.chain().at(1)->type(), QStringLiteral("insert"));
    QCOMPARE(f.currentRow(), 1);
    auto *editor = qobject_cast<RuleEditor *>(f.panel->currentEditor());
    QVERIFY(editor);
    QCOMPARE(editor->ruleType(), QStringLiteral("insert"));
    QTRY_COMPARE(QApplication::focusWidget(),
                 static_cast<QWidget *>(editor->findChild<QLineEdit *>(QStringLiteral("text"))));
}

void TestAppRulePanel::addAtEndWithoutSelection()
{
    Fixture f({"replace", "numbering"});
    f.panel->ruleList()->setCurrentIndex({});
    f.actions.addRuleMenu->findChild<QAction *>(QStringLiteral("addRule_changeCase"))->trigger();
    QCOMPARE(f.model.chain().at(2)->type(), QStringLiteral("changeCase"));
    QCOMPARE(f.currentRow(), 2);
}

void TestAppRulePanel::editorFollowsSelection()
{
    Fixture f({"replace", "numbering"});
    f.select(1);
    auto *editor = qobject_cast<NumberingEditor *>(f.panel->currentEditor());
    QVERIFY(editor);
    QCOMPARE(editor->findChild<QLabel *>(QStringLiteral("ruleEditorTitle"))->text(),
             QStringLiteral("Rule 2: Numbering"));
    f.select(0);
    QCOMPARE(qobject_cast<RuleEditor *>(f.panel->currentEditor())->ruleType(),
             QStringLiteral("replace"));
}

void TestAppRulePanel::editorWritesRuleAndSummary()
{
    Fixture f({"replace", "numbering"});
    QSignalSpy chainChanged(&f.model, &RuleListModel::chainChanged);
    f.select(1);
    auto *start = f.panel->currentEditor()->findChild<QSpinBox *>(QStringLiteral("start"));
    start->setValue(5);
    QCOMPARE(static_cast<NumberingRule *>(f.model.chain().at(1))->start, 5);
    QVERIFY(chainChanged.count() >= 1);
    QVERIFY(f.model.index(1)
                    .data(RuleListModel::SummaryRole)
                    .toString()
                    .contains(QStringLiteral("from 5")));

    // A pending text edit goes to the old rule when the selection changes.
    f.select(0);
    auto *find = f.panel->currentEditor()->findChild<QLineEdit *>(QStringLiteral("find"));
    QTest::keyClicks(find, QStringLiteral("IMG_"));
    const int before = chainChanged.count();
    f.select(1);
    QCOMPARE(static_cast<ReplaceRule *>(f.model.chain().at(0))->find, QStringLiteral("IMG_"));
    QVERIFY(chainChanged.count() > before);
    QVERIFY(f.model.index(0)
                    .data(RuleListModel::SummaryRole)
                    .toString()
                    .contains(QStringLiteral("IMG_")));
}

void TestAppRulePanel::keyboardMove()
{
    Fixture f({"replace", "numbering"});
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.select(0);
    f.panel->ruleList()->setFocus();
    QTest::keyClick(f.panel->ruleList(), Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(f.model.chain().at(1)->type(), QStringLiteral("replace"));
    QCOMPARE(f.currentRow(), 1);
    QVERIFY(!f.actions.moveRuleDown->isEnabled());
    QVERIFY(f.actions.moveRuleUp->isEnabled());
    QTest::keyClick(f.panel->ruleList(), Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(f.model.chain().at(0)->type(), QStringLiteral("replace"));
    QCOMPARE(f.currentRow(), 0);
}

void TestAppRulePanel::moveUpdatesTitle()
{
    Fixture f({"replace", "numbering"});
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.select(1);
    // A move from the editor keeps the focus in the editor field, and Space there does not
    // turn the rule off.
    auto *separator = f.panel->currentEditor()->findChild<QLineEdit *>(QStringLiteral("separator"));
    separator->setFocus();
    QTest::keyClick(separator, Qt::Key_Space);
    QVERIFY(f.model.chain().at(1)->enabled);
    QTest::keyClick(separator, Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(f.model.chain().at(0)->type(), QStringLiteral("numbering"));
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(separator));
    QVERIFY(separator->isEnabled());
    QTRY_COMPARE(static_cast<NumberingRule *>(f.model.chain().at(0))->separator,
                 QStringLiteral("_ "));
    f.actions.moveRuleDown->trigger();
    QCOMPARE(f.currentRow(), 1);
    f.actions.moveRuleUp->trigger();
    QCOMPARE(f.currentRow(), 0);
    QCOMPARE(f.panel->currentEditor()
                     ->findChild<QLabel *>(QStringLiteral("ruleEditorTitle"))
                     ->text(),
             QStringLiteral("Rule 1: Numbering"));
}

void TestAppRulePanel::spaceToggles()
{
    Fixture f({"replace", "numbering"});
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.select(1);
    f.panel->ruleList()->setFocus();
    QTest::keyClick(f.panel->ruleList(), Qt::Key_Space);
    QVERIFY(!f.model.chain().at(1)->enabled);
    QVERIFY(f.model.index(1)
                    .data(RuleListModel::SummaryRole)
                    .toString()
                    .contains(QStringLiteral("(off)")));
    QTest::keyClick(f.panel->ruleList(), Qt::Key_Space);
    QVERIFY(f.model.chain().at(1)->enabled);
    f.actions.toggleRule->trigger();
    QVERIFY(!f.model.chain().at(1)->enabled);
}

void TestAppRulePanel::deleteKey()
{
    Fixture f({"replace", "numbering", "insert"});
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    f.select(1);
    f.panel->ruleList()->setFocus();
    QTest::keyClick(f.panel->ruleList(), Qt::Key_Delete);
    QCOMPARE(f.model.rowCount(), 2);
    QCOMPARE(f.model.chain().at(1)->type(), QStringLiteral("insert"));
    // The next rule is selected, and its editor shows it.
    QCOMPARE(f.currentRow(), 1);
    QCOMPARE(qobject_cast<RuleEditor *>(f.panel->currentEditor())->ruleType(),
             QStringLiteral("insert"));
    QCOMPARE(f.panel->currentEditor()
                     ->findChild<QLabel *>(QStringLiteral("ruleEditorTitle"))
                     ->text(),
             QStringLiteral("Rule 2: Insert"));
    // The rule list takes Delete before the window shortcut of Remove from List.
    f.window.addAction(f.actions.removeFromList);
    QSignalSpy removeItems(f.actions.removeFromList, &QAction::triggered);
    f.panel->ruleList()->setFocus();
    QTest::keyClick(f.panel->ruleList(), Qt::Key_Delete);
    QCOMPARE(f.model.rowCount(), 1);
    QCOMPARE(removeItems.count(), 0);
    f.actions.addRuleMenu->findChild<QAction *>(QStringLiteral("addRule_insert"))->trigger();
    QCOMPARE(f.model.rowCount(), 2);
    // Delete in an editor text field does not remove a rule.
    auto *text = f.panel->currentEditor()->findChild<QLineEdit *>(QStringLiteral("text"));
    text->setFocus();
    QTest::keyClick(text, Qt::Key_Delete);
    QCOMPARE(f.model.rowCount(), 2);
}

void TestAppRulePanel::duplicateAction()
{
    Fixture f({"replace", "numbering"});
    f.select(0);
    f.actions.duplicateRule->trigger();
    QCOMPARE(f.model.rowCount(), 3);
    QCOMPARE(f.model.chain().at(1)->type(), QStringLiteral("replace"));
    QCOMPARE(f.currentRow(), 1);
    f.actions.deleteRule->trigger();
    QCOMPARE(f.model.rowCount(), 2);
}

void TestAppRulePanel::unsupportedPage()
{
    Fixture f({"replace"});
    f.model.insertRule(1, std::make_unique<UnsupportedRule>(
                                  QJsonObject{{"type", "dateTime"}, {"format", "yyyy"}}));
    f.select(1);
    QWidget *page = f.panel->currentEditor();
    QVERIFY(!qobject_cast<RuleEditor *>(page));
    auto *label = page->findChild<QLabel *>(QStringLiteral("unsupportedText"));
    QVERIFY(label);
    QCOMPARE(label->text(), QStringLiteral("This rule type is not supported by this CTRM version. "
                                           "Remove it or turn it off."));
    QVERIFY(label->isVisible());
}

void TestAppRulePanel::editorsShareLabelColumn()
{
    Fixture f(ruleTypes());
    QSet<int> edges;
    for (int row = 0; row < f.model.rowCount(); ++row) {
        f.select(row);
        QApplication::processEvents();
        auto *editor = qobject_cast<RuleEditor *>(f.panel->currentEditor());
        QVERIFY(editor);
        edges.insert(fieldEdge(editor->form()));
    }
    QCOMPARE(edges.size(), 1);
    QVERIFY(*edges.begin() > 0);
    // R5-03: the Chain options boxes are on the panel content edge, not in the field column.
    auto *chain = f.panel->findChild<ChainOptionsWidget *>();
    chain->setOpen(true);
    QApplication::processEvents();
    auto *portable = chain->findChild<QCheckBox *>(QStringLiteral("portableNames"));
    QCOMPARE(ctrm::test::visualLeft(portable, f.panel), tokens::space3);
}

void TestAppRulePanel::tertiaryButtonsAccessible()
{
    Fixture f({"replace", "numbering"});
    f.select(0);
    const QList<std::pair<const char *, QAction *>> buttons{
            {"moveUpButton", f.actions.moveRuleUp},
            {"moveDownButton", f.actions.moveRuleDown},
            {"duplicateButton", f.actions.duplicateRule},
            {"deleteButton", f.actions.deleteRule}};
    for (const auto &[name, action] : buttons) {
        auto *b = f.panel->findChild<QToolButton *>(QString::fromLatin1(name));
        QVERIFY2(b, name);
        QVERIFY(b->autoRaise());
        QCOMPARE(b->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QVERIFY(!b->icon().isNull());
        QCOMPARE(b->iconSize(), QSize(16, 16));
        QVERIFY(!b->accessibleName().isEmpty());
        QVERIFY(!b->toolTip().isEmpty());
        QVERIFY(b->width() >= 24 && b->height() >= 24);
        QCOMPARE(b->isEnabled(), action->isEnabled());
    }
    QVERIFY(f.panel->findChild<QToolButton *>(QStringLiteral("moveUpButton"))
                    ->toolTip()
                    .contains(QKeySequence(QStringLiteral("Ctrl+Alt+Up"))
                                      .toString(QKeySequence::NativeText)));
    QVERIFY(!f.panel->findChild<QToolButton *>(QStringLiteral("moveUpButton"))->isEnabled());
    f.panel->findChild<QToolButton *>(QStringLiteral("moveDownButton"))->click();
    QCOMPARE(f.model.chain().at(1)->type(), QStringLiteral("replace"));
}

void TestAppRulePanel::emptyStates()
{
    Fixture f;
    auto *text = f.panel->findChild<QLabel *>(QStringLiteral("emptyText"));
    QVERIFY(text && text->isVisible());
    QCOMPARE(text->text(), QStringLiteral("Rules come next. Add files first, then add a rule to "
                                          "change their names."));
    QVERIFY(!f.panel->ruleList()->isVisible());
    QVERIFY(f.panel->addRuleButton()->isVisible());
    QVERIFY(!f.panel->findChild<QPushButton *>(QStringLiteral("presetsButton"))); // S06
    QVERIFY(!f.panel->addRuleButton()->property("primary").toBool());
    QVERIFY(!f.panel->findChild<ChainOptionsWidget *>()->isVisible());

    f.panel->setPrimaryAddRule(true);
    QCOMPARE(text->text(),
             QStringLiteral(
                     "Add a rule to change the names. The preview shows the result at once."));
    QVERIFY(f.panel->addRuleButton()->property("primary").toBool());
    QVERIFY(f.panel->addRuleButton()->isDefault()); // setPrimary (ui-design 5.1)
    if (f.panel->style()->name().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) == 0)
        QCOMPARE(f.panel->addRuleButton()->palette().color(QPalette::Button),
                 f.panel->palette().color(QPalette::Accent));

    f.actions.addRuleMenu->findChild<QAction *>(QStringLiteral("addRule_replace"))->trigger();
    QVERIFY(!text->isVisible());
    QVERIFY(f.panel->ruleList()->isVisible());
    QVERIFY(f.panel->addRuleButton()->isVisible());
    QVERIFY(f.panel->findChild<ChainOptionsWidget *>()->isVisible());

    f.panel->setPrimaryAddRule(false);
    QVERIFY(!f.panel->addRuleButton()->property("primary").toBool());
    f.actions.deleteRule->trigger();
    QVERIFY(text->isVisible());
}

void TestAppRulePanel::chainOptionsAtBottom()
{
    Fixture f({"replace"});
    f.select(0);
    QApplication::processEvents();
    auto *chain = f.panel->findChild<ChainOptionsWidget *>();
    QVERIFY(chain->isVisible());
    // Below the splitter with the list and the editor, and not in the scroll area.
    auto *splitter = f.panel->findChild<QSplitter *>();
    QVERIFY(splitter);
    QCOMPARE(splitter->orientation(), Qt::Vertical);
    QVERIFY(!splitter->isAncestorOf(chain));
    QVERIFY(chain->mapTo(f.panel, QPoint(0, 0)).y() >=
            splitter->mapTo(f.panel, QPoint(0, splitter->height())).y());
    auto *scroll = f.panel->findChild<QScrollArea *>();
    QVERIFY(scroll && splitter->isAncestorOf(scroll));
    QVERIFY(scroll->isAncestorOf(f.panel->currentEditor()));
    QVERIFY(splitter->isAncestorOf(f.panel->ruleList()));
}

void TestAppRulePanel::chainOptionsStableAndTitleAligned()
{
    // M19: Chain options has the body font and does not move when another editor shows. The
    // editor title starts on the line of the "Rules" header.
    const QStringList types{"replace",   "insert",    "remove", "changeCase",
                            "numbering", "extension", "cleanup"};
    // The macOS platform theme gives tool buttons a smaller font.
    const QFont appFont = QApplication::font();
    QFont small = appFont;
    small.setPointSizeF(appFont.pointSizeF() - 3);
    QApplication::setFont(small, "QToolButton");
    const auto restore = qScopeGuard([&] { QApplication::setFont(appFont, "QToolButton"); });
    Fixture f(types);
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    auto *header = f.panel->findChild<QToolButton *>(QStringLiteral("chainOptionsHeader"));
    QVERIFY(header);
    QCOMPARE(header->font().pointSizeF(), f.panel->font().pointSizeF());
    f.select(types.indexOf(QStringLiteral("changeCase")));
    auto *more = f.panel->currentEditor()->findChild<QToolButton *>(QStringLiteral("moreOptions"));
    QVERIFY(more);
    QCOMPARE(more->font().pointSizeF(), f.panel->font().pointSizeF());
    QLabel *rules = nullptr;
    for (QLabel *label : f.panel->findChildren<QLabel *>()) {
        if (label->text() == QStringLiteral("Rules"))
            rules = label;
    }
    QVERIFY(rules);
    const int rulesX = rules->mapTo(f.panel, QPoint()).x();
    for (const int height : {800, 420}) {
        f.window.resize(f.window.width(), height);
        QApplication::processEvents();
        int headerY = -1;
        for (int row = 0; row < types.size(); ++row) {
            f.select(row);
            QApplication::processEvents();
            const int y = header->mapTo(f.panel, QPoint()).y();
            if (headerY < 0)
                headerY = y;
            QVERIFY2(
                    y == headerY,
                    qPrintable(
                            QStringLiteral("%1: %2 != %3").arg(types.at(row)).arg(y).arg(headerY)));
            auto *title = f.panel->currentEditor()->findChild<QLabel *>(
                    QStringLiteral("ruleEditorTitle"));
            QVERIFY(title);
            QCOMPARE(title->mapTo(f.panel, QPoint()).x(), rulesX);
        }
    }
}

void TestAppRulePanel::chainOptionsForwarded()
{
    Fixture f({"cleanup", "extension"});
    QSignalSpy spy(f.panel, &RulePanel::chainOptionsChanged);
    auto *chain = f.panel->findChild<ChainOptionsWidget *>();
    chain->findChild<QCheckBox *>(QStringLiteral("portableNames"))->click();
    chain->findChild<QCheckBox *>(QStringLiteral("folderExtensions"))->click();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(f.model.chain().options, (ChainOptions{false, false, true}));
    f.select(0);
    QCOMPARE(f.panel->currentEditor()->findChild<QLabel *>(QStringLiteral("portableNote"))->text(),
             joinLastWords(QStringLiteral("Uses the rules of this system.")));
    f.select(1);
    QVERIFY(f.panel->currentEditor()
                    ->findChild<QLabel *>(QStringLiteral("folderNote"))
                    ->isVisible());
}

void TestAppRulePanel::setChainResetsEditor()
{
    Fixture f({"replace", "numbering"});
    f.select(1);
    RuleChain chain;
    chain.options.autoResolveDuplicates = true;
    chain.insert(0, createRule(QStringLiteral("insert")));
    f.model.setChain(chain);
    auto *chainWidget = f.panel->findChild<ChainOptionsWidget *>();
    QVERIFY(chainWidget->options().autoResolveDuplicates);
    // The first rule of the new chain is selected. The numbering editor holds no deleted rule.
    QCOMPARE(f.currentRow(), 0);
    QCOMPARE(qobject_cast<RuleEditor *>(f.panel->currentEditor())->ruleType(),
             QStringLiteral("insert"));
    auto *numbering = f.panel->findChild<NumberingEditor *>();
    QVERIFY(!numbering->isEnabled());
    QSignalSpy chainChanged(&f.model, &RuleListModel::chainChanged);
    numbering->findChild<QSpinBox *>(QStringLiteral("start"))->setValue(8);
    QCOMPARE(chainChanged.count(), 0);
}

void TestAppRulePanel::numberedCountForwarded()
{
    Fixture f({"numbering"});
    f.panel->setNumberedCount(120);
    f.select(0);
    QCOMPARE(f.panel->currentEditor()->findChild<QLabel *>(QStringLiteral("example"))->text(),
             QStringLiteral("Example: 001"));
}

void TestAppRulePanel::inactiveSelectionContrast()
{
    // D6: on the grey selection of an inactive window, the summary is not HighlightedText.
    Fixture f({QStringLiteral("replace")});
    QListView *list = f.panel->ruleList();
    QStyleOptionViewItem option;
    option.initFrom(list);
    option.widget = list;
    option.rect = QRect(0, 0, 300, list->sizeHintForRow(0));
    option.state = QStyle::State_Enabled; // no State_Active
    option.palette.setColor(QPalette::Highlight, QColor(0xd0, 0xd0, 0xd0));
    option.palette.setColor(QPalette::HighlightedText, QColor(0xff, 0x00, 0xff));
    const auto countMagenta = [&](QStyle::State extra) {
        QImage image(option.rect.size(), QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        QStyleOptionViewItem o = option;
        o.state |= extra;
        list->itemDelegate()->paint(&painter, o, f.model.index(0));
        painter.end();
        int n = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.red() > 200 && c.blue() > 200 && c.green() < 80)
                    ++n;
            }
        return n;
    };
    // The check mark of the check box can be HighlightedText on its accent fill (the windows11
    // style), also when the row is not selected. So compare with the row that is not selected.
    const int unselected = countMagenta(QStyle::State_None);
    option.state |= QStyle::State_Selected;
    QCOMPARE(countMagenta(QStyle::State_None), unselected);
    QVERIFY(countMagenta(QStyle::State_Active) > unselected); // the accent selection keeps it
}

void TestAppRulePanel::listFollowsRows()
{
    // D10: the list is as high as its rows (at least 3); the editor gets the rest and the
    // space taken away later.
    Fixture f({QStringLiteral("replace"), QStringLiteral("replace")});
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QListView *list = f.panel->ruleList();
    const int row = list->sizeHintForRow(0);
    QVERIFY(row > 0);
    // The minimum is 2 rows: a low panel gives the other rows to the editor, and the list still
    // shows a chain (M06, R6-06).
    QCOMPARE(list->minimumSizeHint().height(), 2 * row + 2 * list->frameWidth());
    QCOMPARE(list->sizeHint().height(), 3 * row + 2 * list->frameWidth());
    // A high panel: the tallest editor page fits also with 200 px less (R1-01).
    f.window.resize(f.window.width(), 1200);
    QTest::qWait(50);
    const int before = list->height();
    QVERIFY2(before <= list->sizeHint().height() + 2, qPrintable(QString::number(before)));
    f.window.resize(f.window.width(), f.window.height() - 200);
    QTest::qWait(50);
    QCOMPARE(list->height(), before);
    for (int i = 0; i < 3; ++i)
        f.actions.addRuleMenu->findChild<QAction *>(QStringLiteral("addRule_replace"))->trigger();
    QCOMPARE(list->sizeHint().height(), 5 * row + 2 * list->frameWidth());
}

void TestAppRulePanel::emptyStateLeftAndTop()
{
    // O02 (ui-design 5.4, design-tokens 4.6): "No rules yet" (label-strong), the dim text and
    // Add Rule, left-aligned at the panel inset and top-aligned under the header. One Add Rule.
    // S06: no "or Presets".
    Fixture f;
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    auto *title = f.panel->findChild<QLabel *>(QStringLiteral("emptyTitle"));
    auto *text = f.panel->findChild<QLabel *>(QStringLiteral("emptyText"));
    QVERIFY(title && text);
    QVERIFY(title->isVisible());
    QCOMPARE(title->text(), QStringLiteral("No rules yet"));
    QCOMPARE(title->font().weight(), QFont::DemiBold);
    QCOMPARE(text->palette().color(QPalette::WindowText),
             f.panel->palette().color(QPalette::PlaceholderText));
    QVERIFY(text->wordWrap());
    QVERIFY(text->maximumWidth() <= 40 * text->fontMetrics().averageCharWidth());
    QLabel *rules = nullptr;
    for (QLabel *label : f.panel->findChildren<QLabel *>()) {
        if (label->text() == QStringLiteral("Rules"))
            rules = label;
    }
    QVERIFY(rules);
    const QRect rulesRect = rules->parentWidget()->geometry(); // the header row
    const QRect titleRect = ctrm::test::visualRect(title, f.panel);
    const QRect textRect = ctrm::test::visualRect(text, f.panel);
    QPushButton *add = f.panel->addRuleButton();
    const QRect addRect = ctrm::test::visualRect(add, f.panel);
    QCOMPARE(titleRect.left(), tokens::space3);
    QCOMPARE(textRect.left(), tokens::space3);
    QCOMPARE(addRect.left(), tokens::space3);
    QCOMPARE(titleRect.top() - (rulesRect.bottom() + 1), tokens::space3);
    QCOMPARE(textRect.top() - (titleRect.bottom() + 1), tokens::space1);
    QCOMPARE(addRect.top() - (textRect.bottom() + 1), tokens::space3);
    // The header has no Add Rule in this state.
    QVERIFY(!rules->parentWidget()->isAncestorOf(add));
    for (QLabel *l : f.panel->findChildren<QLabel *>())
        QVERIFY(l->text() != QStringLiteral("or"));
}

void TestAppRulePanel::listShortcutsOnlyWithFocus()
{
    // M28: the menu shows Delete and Space for the rule while the rule list has the focus.
    // At other times the actions have no shortcut, so the keys go to the other widgets.
    Fixture f({"replace", "numbering"});
    auto *other = new QLineEdit(&f.window);
    f.window.layout()->addWidget(other);
    f.window.addActions({f.actions.deleteRule, f.actions.toggleRule, f.actions.removeFromList});
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    other->setFocus();
    QTRY_VERIFY(other->hasFocus());
    QVERIFY(f.actions.deleteRule->shortcut().isEmpty());
    QVERIFY(f.actions.toggleRule->shortcut().isEmpty());

    f.select(0);
    f.panel->ruleList()->setFocus();
    QTRY_VERIFY(f.panel->ruleList()->hasFocus());
    QCOMPARE(f.actions.deleteRule->shortcut(), QKeySequence(Qt::Key_Delete));
    QCOMPARE(f.actions.toggleRule->shortcut(), QKeySequence(Qt::Key_Space));
    // One key press does one thing, also with the shortcuts on the window.
    QSignalSpy removeItems(f.actions.removeFromList, &QAction::triggered);
    QTest::keyClick(f.panel->ruleList(), Qt::Key_Space);
    QVERIFY(!f.model.chain().at(0)->enabled);
    QTest::keyClick(f.panel->ruleList(), Qt::Key_Delete);
    QCOMPARE(f.model.rowCount(), 1);
    QCOMPARE(removeItems.count(), 0);

    // An open popup menu takes the focus for a short time; the menu keeps the keys.
    QFocusEvent popupOut(QEvent::FocusOut, Qt::PopupFocusReason);
    QCoreApplication::sendEvent(f.panel->ruleList(), &popupOut);
    QCOMPARE(f.actions.toggleRule->shortcut(), QKeySequence(Qt::Key_Space));

    other->setFocus();
    QTRY_VERIFY(other->hasFocus());
    QVERIFY(f.actions.deleteRule->shortcut().isEmpty());
    QVERIFY(f.actions.toggleRule->shortcut().isEmpty());
    QTest::keyClick(other, Qt::Key_Space);
    QCOMPARE(other->text(), QStringLiteral(" "));
    QCOMPARE(f.model.rowCount(), 1);
    QVERIFY(f.model.chain().at(0)->enabled);
}

void TestAppRulePanel::ruleRowLayout()
{
    // R6-02 (ui-design 4.4): a rule row is row-h-2 high with space-1 at the top and the bottom.
    // The title and the summary start at one x, space-2 after the check box; the check box is
    // centred on the row; the two lines are 2 px apart or less, so each row reads as one group.
    using namespace ctrm::app::tokens;
    Fixture f({QStringLiteral("replace"), QStringLiteral("insert")});
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QListView *list = f.panel->ruleList();
    QCOMPARE(list->sizeHintForRow(0), rowH2);
    if (qEnvironmentVariableIsSet("CTRM_GRAB")) {
        f.model.setData(f.model.index(1), Qt::Unchecked, Qt::CheckStateRole);
        QTest::qWait(200);
        list->grab().save(qEnvironmentVariable("CTRM_GRAB") + QStringLiteral("-rules.png"));
    }
    for (Qt::LayoutDirection direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        QStyleOptionViewItem option;
        option.initFrom(list->viewport());
        option.widget = list;
        option.font = list->font();
        option.direction = direction;
        option.rect = list->visualRect(f.model.index(1));
        const RuleRowLayout row = ctrm::app::ruleRowLayout(option);
        const QString at =
                direction == Qt::LeftToRight ? QStringLiteral("ltr") : QStringLiteral("rtl");
        QCOMPARE(option.rect.height(), rowH2);
        QVERIFY2(qAbs(row.check.center().y() - option.rect.center().y()) <= 0, qPrintable(at));
        if (direction == Qt::LeftToRight) {
            QCOMPARE(row.title.left(), row.check.right() + 1 + space2);
            QCOMPARE(row.summary.left(), row.title.left());
        } else {
            QCOMPARE(row.title.right(), row.check.left() - 1 - space2);
            QCOMPARE(row.summary.right(), row.title.right());
        }
        QVERIFY2(row.title.top() >= option.rect.top() + space1, qPrintable(at));
        QVERIFY2(row.summary.bottom() <= option.rect.bottom() - space1 + 1, qPrintable(at));
        const int gap = row.summary.top() - row.title.bottom() - 1;
        QVERIFY2(gap >= 0 && gap <= 2, qPrintable(at + QStringLiteral(": gap %1").arg(gap)));
    }
}

void TestAppRulePanel::ruleRowBoxClickAndError()
{
    // R6-02: a click on the painted check box turns the rule off; the error icon is at the end of
    // the title line and does not cover the title.
    Fixture f({QStringLiteral("replace"), QStringLiteral("insert")});
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QListView *list = f.panel->ruleList();
    for (Qt::LayoutDirection direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        list->setLayoutDirection(direction);
        QApplication::processEvents();
        QStyleOptionViewItem option;
        option.initFrom(list->viewport());
        option.widget = list;
        option.font = list->font();
        option.direction = direction;
        option.rect = list->visualRect(f.model.index(1));
        const RuleRowLayout row = ctrm::app::ruleRowLayout(option);
        const Qt::CheckState before =
                f.model.index(1).data(Qt::CheckStateRole).value<Qt::CheckState>();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, row.check.center());
        QVERIFY(f.model.index(1).data(Qt::CheckStateRole).value<Qt::CheckState>() != before);
        const RuleRowLayout error = ctrm::app::ruleRowLayout(option, true);
        QVERIFY(error.icon.isValid());
        QVERIFY(option.rect.contains(error.icon));
        QVERIFY(!error.icon.intersects(error.title));
        QVERIFY(qAbs(error.icon.center().y() - error.title.center().y()) <= 1);
    }
}

void TestAppRulePanel::ruleRowDisabledDimmed()
{
    // While a rename runs, the rule panel is disabled: the row text is drawn in the disabled
    // colours, not only the check box.
    Fixture f({QStringLiteral("replace"), QStringLiteral("insert")});
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QListView *list = f.panel->ruleList();
    QPalette palette = list->palette();
    palette.setColor(QPalette::Disabled, QPalette::Text, Qt::red);
    palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, Qt::red);
    list->setPalette(palette);
    f.panel->setEnabled(false);
    QApplication::processEvents();
    const QImage image = list->viewport()->grab().toImage();
    const QRect row = list->visualRect(f.model.index(1));
    const qreal dpr = image.devicePixelRatio();
    int red = 0;
    for (int y = int(row.top() * dpr); y <= int(row.bottom() * dpr); ++y)
        for (int x = int(row.left() * dpr); x <= int(row.right() * dpr) && x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            red += c.red() > 150 && c.green() < 80 && c.blue() < 80;
        }
    QVERIFY2(red > 20, qPrintable(QString::number(red)));
}

QTEST_MAIN(TestAppRulePanel)
void TestAppRulePanel::primaryAddRuleIsPlainButton()
{
    // O1: the macOS style draws no accent on a pull-down button, so the primary Add Rule is a
    // plain default button that opens the menu below it on click (ui-design 5.1, 5.4).
    Fixture f;
    // Room for the menu on both sides of the button: Qt moves a menu that goes past the screen
    // edge. The menu is as wide as its text (wider with the box glyphs of a test machine that
    // has no fonts). The window starts one menu width from the screen edge, so a button at any
    // place in the window (also in right to left) has room on its start side.
    const QRect screen = f.window.screen()->availableGeometry();
    const int menuWidth = f.actions.addRuleMenu->sizeHint().width();
    f.window.move(screen.left() + menuWidth, 100);
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QPushButton *add = f.panel->addRuleButton();
    const QRect button(add->mapToGlobal(QPoint()), add->size());
    QVERIFY2(button.left() + menuWidth - 1 <= screen.right() &&
                     button.right() - menuWidth + 1 >= screen.left(),
             qPrintable(QStringLiteral("button %1..%2, menu %3")
                                .arg(button.left())
                                .arg(button.right())
                                .arg(menuWidth)));
    f.panel->setPrimaryAddRule(true);
    QCOMPARE(add->menu(), nullptr);
    QVERIFY(add->isDefault());
    QVERIFY(!f.actions.addRuleMenu->isVisible());
    QTest::mouseClick(add, Qt::LeftButton);
    QTRY_VERIFY(f.actions.addRuleMenu->isVisible());
    QCOMPARE(f.actions.addRuleMenu->pos(), add->mapToGlobal(add->rect().bottomLeft()));
    f.actions.addRuleMenu->hide();
    // The menu action (its shortcut) opens the menu also in this state.
    f.actions.addRuleMenu->menuAction()->trigger();
    QTRY_VERIFY(f.actions.addRuleMenu->isVisible());
    f.actions.addRuleMenu->hide();
    // Right to left: the menu ends at the right edge of the button.
    f.window.setLayoutDirection(Qt::RightToLeft);
    QTest::mouseClick(add, Qt::LeftButton);
    QTRY_VERIFY(f.actions.addRuleMenu->isVisible());
    QCOMPARE(f.actions.addRuleMenu->geometry().right(),
             add->mapToGlobal(add->rect().bottomRight()).x());
    f.actions.addRuleMenu->hide();
    f.window.setLayoutDirection(Qt::LeftToRight);

    f.panel->setPrimaryAddRule(false);
    QCOMPARE(add->menu(), f.actions.addRuleMenu);
    QVERIFY(!add->isDefault());
}

void TestAppRulePanel::editorFadeWhenCut()
{
    // M06 (ui-design 4.4): a fade at the bottom of the editor while the page is cut there. It
    // takes no mouse events and goes when the page is scrolled to the end.
    Fixture f({"numbering"});
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    auto *scroll = f.panel->findChild<QScrollArea *>(QStringLiteral("editorScroll"));
    auto *fade = scroll->findChild<QWidget *>(QStringLiteral("editorFade"));
    QVERIFY(fade);
    QVERIFY(fade->testAttribute(Qt::WA_TransparentForMouseEvents));

    f.window.resize(f.window.width(), 1200);
    QApplication::processEvents();
    QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
    QVERIFY(!fade->isVisible());

    f.window.resize(f.window.width(), 1); // the minimum height
    QApplication::processEvents();
    QScrollBar *bar = scroll->verticalScrollBar();
    QTRY_VERIFY(bar->maximum() > 0);
    QTRY_VERIFY(fade->isVisible());
    const QRect viewport = scroll->viewport()->geometry();
    QCOMPARE(fade->geometry().bottom(), viewport.bottom());
    QCOMPARE(fade->width(), viewport.width());
    QVERIFY(fade->height() >= 12 && fade->height() <= 20);

    bar->setValue(bar->maximum());
    QVERIFY(!fade->isVisible());
    bar->setValue(0);
    QVERIFY(fade->isVisible());
}

void TestAppRulePanel::editorsFitPanelWidth()
{
    // O3: the editor area does not scroll to the side. The panel is never narrower than the
    // widest editor, also at a larger font (ui-design 4.4).
    Fixture f({"replace", "insert", "remove", "cleanup"});
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    auto *scroll = f.panel->findChild<QScrollArea *>(QStringLiteral("editorScroll"));
    QVERIFY(scroll);
    QCOMPARE(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);

    const auto check = [&](const char *when) {
        int widest = 0;
        for (QWidget *page : scroll->widget()->findChildren<RuleEditor *>())
            widest = std::max(widest, page->minimumSizeHint().width());
        QVERIFY2(widest > 0, when);
        f.window.resize(1, 800); // the layout keeps the minimum width
        QApplication::processEvents();
        for (int row = 0; row < f.model.rowCount(); ++row) {
            f.select(row);
            QApplication::processEvents();
            QWidget *editor = f.panel->currentEditor();
            QVERIFY2(scroll->viewport()->width() >= editor->minimumSizeHint().width(),
                     qPrintable(QStringLiteral("%1: %2 < %3")
                                        .arg(QLatin1String(when))
                                        .arg(scroll->viewport()->width())
                                        .arg(editor->minimumSizeHint().width())));
            QVERIFY2(editor->width() <= scroll->viewport()->width(), when);
        }
        QVERIFY2(f.panel->minimumSizeHint().width() >= widest, when);
    };
    check("default font");
    QFont big = f.panel->font();
    big.setPointSizeF(big.pointSizeF() * 1.6);
    f.panel->setFont(big);
    QApplication::processEvents();
    check("large font");
}

void TestAppRulePanel::listKeepsHeightWhenChainOpens()
{
    // C06 (ui-design 8.10): when Chain options opens, the editor area gives up the height; the
    // rule list keeps its height.
    Fixture f({"replace", "insert", "remove", "numbering"});
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    auto *chain = f.panel->findChild<ChainOptionsWidget *>();
    for (const int height : {800, 640}) {
        f.window.resize(f.window.width(), height);
        QApplication::processEvents();
        const int before = f.panel->ruleList()->height();
        chain->setOpen(true);
        QApplication::processEvents();
        QVERIFY2(f.panel->ruleList()->height() == before,
                 qPrintable(QStringLiteral("%1: %2 != %3")
                                    .arg(height)
                                    .arg(f.panel->ruleList()->height())
                                    .arg(before)));
        chain->setOpen(false);
        QApplication::processEvents();
        QVERIFY2(f.panel->ruleList()->height() == before,
                 qPrintable(QStringLiteral("%1 closed: %2 != %3")
                                    .arg(height)
                                    .arg(f.panel->ruleList()->height())
                                    .arg(before)));
    }
}

void TestAppRulePanel::minimumWidthFromEditorsOnly()
{
    // O08: the minimum width is max(360, the widest editor page). It does not change with the
    // rules, the current page or Chain options.
    Fixture f;
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    const int minimum = f.panel->minimumSizeHint().width();
    auto *scroll = f.panel->findChild<QScrollArea *>(QStringLiteral("editorScroll"));
    int widest = 0;
    for (QWidget *page : scroll->widget()->findChildren<RuleEditor *>())
        widest = std::max(widest, page->minimumSizeHint().width());
    QVERIFY2(minimum >= std::max(360, widest),
             qPrintable(QStringLiteral("%1 < %2").arg(minimum).arg(widest)));
    for (const QString &type : ruleTypes())
        f.actions.addRuleMenu->findChild<QAction *>(QStringLiteral("addRule_") + type)->trigger();
    for (int row = 0; row < f.model.rowCount(); ++row) {
        f.select(row);
        QApplication::processEvents();
        QCOMPARE(f.panel->minimumSizeHint().width(), minimum);
    }
    f.panel->findChild<ChainOptionsWidget *>()->setOpen(true);
    QApplication::processEvents();
    QCOMPARE(f.panel->minimumSizeHint().width(), minimum);
    QCOMPARE(f.panel->findChild<ChainOptionsWidget *>()->sizePolicy().horizontalPolicy(),
             QSizePolicy::Ignored);
}

void TestAppRulePanel::editorOnPanelSurface()
{
    // O06b: the editor area paints no box of its own; the title and the labels are on the panel
    // inset (space-3); the gap from the rule tools to the editor title is space-4.
    Fixture f({"numbering", "replace"});
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    f.window.resize(f.window.width(), 420); // the page is cut: the scroll area scrolls
    f.select(0);
    QApplication::processEvents();
    auto *scroll = f.panel->findChild<QScrollArea *>(QStringLiteral("editorScroll"));
    QVERIFY(scroll);
    QCOMPARE(scroll->frameShape(), QFrame::NoFrame);
    QVERIFY(!scroll->viewport()->autoFillBackground());
    QCOMPARE(scroll->viewport()->backgroundRole(), QPalette::Window);
    QVERIFY(!scroll->widget()->autoFillBackground());
    auto *title = f.panel->currentEditor()->findChild<QLabel *>(QStringLiteral("ruleEditorTitle"));
    QVERIFY(title);
    QCOMPARE(title->mapTo(f.panel, QPoint()).x(), tokens::space3);
    QCOMPARE(f.panel->ruleList()->mapTo(f.panel, QPoint()).x(), tokens::space3);
    // C08: the header row is space-3 + header-h + space-2, as the filter row over the table.
    QCOMPARE(f.panel->ruleList()->mapTo(f.panel, QPoint()).y(),
             tokens::space3 + tokens::headerH + tokens::space2);
    QCOMPARE(f.panel->width() - (f.panel->ruleList()->mapTo(f.panel, QPoint()).x() +
                                 f.panel->ruleList()->width()),
             tokens::space3);
    auto *tool = f.panel->findChild<QToolButton *>(QStringLiteral("deleteButton"));
    const int toolsBottom = ctrm::test::visualRect(tool, f.panel).bottom() + 1;
    QCOMPARE(title->mapTo(f.panel, QPoint()).y() - toolsBottom, tokens::space4);
}

void TestAppRulePanel::ruleToolButtons()
{
    // C07: icon buttons are hit-min squares with an icon-s icon, space-1 apart; the first is on
    // the panel inset.
    Fixture f({"replace", "numbering"});
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    int x = tokens::space3;
    for (const char *name : {"moveUpButton", "moveDownButton", "duplicateButton", "deleteButton"}) {
        auto *b = f.panel->findChild<TertiaryButton *>(QString::fromLatin1(name));
        QVERIFY2(b, name);
        QVERIFY2(b->isIconButton(), name);
        QCOMPARE(b->size(), QSize(tokens::hitMin, tokens::hitMin));
        QCOMPARE(b->iconSize(), QSize(tokens::iconS, tokens::iconS));
        QCOMPARE(b->mapTo(f.panel, QPoint()).x(), x);
        x += tokens::hitMin + tokens::space1;
    }
}

#include "tst_app_rulepanel.moc"
