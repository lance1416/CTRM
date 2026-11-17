// SPDX-License-Identifier: GPL-3.0-or-later
#include "actions.h"
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
#include "widgets/rulepanel.h"

#include <QAction>
#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
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
        if (item && item->widget() && form->isRowVisible(row))
            return item->widget()->geometry().x() - form->geometry().x();
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
    void setChainResetsEditor();
    void numberedCountForwarded();
    void inactiveSelectionContrast();
    void listFollowsRows();
    void presetsUnderAddRule();
    void primaryAddRuleIsPlainButton();
    void editorsFitPanelWidth();
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
    Fixture f;
    QStringList sections;
    QStringList rules;
    for (QAction *a : f.actions.addRuleMenu->actions()) {
        if (a->isSeparator())
            sections.append(a->text());
        else
            rules.append(a->text());
    }
    QCOMPARE(sections, (QStringList{"Text", "Format", "Numbers"}));
    QCOMPARE(rules, (QStringList{"Replace...", "Insert...", "Remove...", "Clean up...",
                                 "Change case...", "Extension...", "Numbering..."}));
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
    auto *chain = f.panel->findChild<ChainOptionsWidget *>();
    chain->setOpen(true);
    QApplication::processEvents();
    edges.insert(fieldEdge(chain->form()));
    QCOMPARE(edges.size(), 1);
    QVERIFY(*edges.begin() > 0);
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
    QVERIFY(f.panel->presetsButton()->isVisible());
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
    QVERIFY(!f.panel->presetsButton()->isVisible());
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
             QStringLiteral("Uses the rules of this system."));
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
    option.state = QStyle::State_Enabled | QStyle::State_Selected; // no State_Active
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
    QCOMPARE(countMagenta(QStyle::State_None), 0);
    QVERIFY(countMagenta(QStyle::State_Active) > 0); // the accent selection keeps it
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
    QCOMPARE(list->minimumSizeHint().height(), 3 * row + 2 * list->frameWidth());
    QCOMPARE(list->sizeHint().height(), 3 * row + 2 * list->frameWidth());
    const int before = list->height();
    QVERIFY2(before <= list->sizeHint().height() + 2, qPrintable(QString::number(before)));
    f.window.resize(f.window.width(), f.window.height() - 200);
    QTest::qWait(50);
    QCOMPARE(list->height(), before);
    for (int i = 0; i < 3; ++i)
        f.actions.addRuleMenu->findChild<QAction *>(QStringLiteral("addRule_replace"))->trigger();
    QCOMPARE(list->sizeHint().height(), 5 * row + 2 * list->frameWidth());
}

void TestAppRulePanel::presetsUnderAddRule()
{
    // D11: the Presets button is centred under Add Rule; "or" is at its left.
    Fixture f;
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QPushButton *add = f.panel->addRuleButton();
    QPushButton *presets = f.panel->presetsButton();
    const int addCentre = add->mapTo(f.panel, add->rect().center()).x();
    const int presetsCentre = presets->mapTo(f.panel, presets->rect().center()).x();
    QVERIFY2(qAbs(addCentre - presetsCentre) <= 1,
             qPrintable(QStringLiteral("%1 %2").arg(addCentre).arg(presetsCentre)));
    QLabel *orLabel = nullptr;
    for (QLabel *l : f.panel->findChildren<QLabel *>())
        if (l->text() == QStringLiteral("or") && l->isVisible())
            orLabel = l;
    QVERIFY(orLabel);
    QVERIFY(orLabel->mapTo(f.panel, QPoint()).x() < presets->mapTo(f.panel, QPoint()).x());
}

QTEST_MAIN(TestAppRulePanel)
void TestAppRulePanel::primaryAddRuleIsPlainButton()
{
    // O1: the macOS style draws no accent on a pull-down button, so the primary Add Rule is a
    // plain default button that opens the menu below it on click (ui-design 5.1, 5.4).
    Fixture f;
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QPushButton *add = f.panel->addRuleButton();
    f.panel->setPrimaryAddRule(true);
    QCOMPARE(add->menu(), nullptr);
    QVERIFY(add->isDefault());
    QVERIFY(!f.actions.addRuleMenu->isVisible());
    QTest::mouseClick(add, Qt::LeftButton);
    QTRY_VERIFY(f.actions.addRuleMenu->isVisible());
    QCOMPARE(f.actions.addRuleMenu->pos(), add->mapToGlobal(add->rect().bottomLeft()));
    f.actions.addRuleMenu->hide();

    f.panel->setPrimaryAddRule(false);
    QCOMPARE(add->menu(), f.actions.addRuleMenu);
    QVERIFY(!add->isDefault());
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

#include "tst_app_rulepanel.moc"
