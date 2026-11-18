// SPDX-License-Identifier: GPL-3.0-or-later
#include "layoutedge.h"
#include "rules/chainoptionswidget.h"
#include "rules/changecaseeditor.h"
#include "rules/changecaserule.h"
#include "rules/cleanupeditor.h"
#include "rules/extensioneditor.h"
#include "rules/extensionrule.h"
#include "rules/inserteditor.h"
#include "rules/insertrule.h"
#include "rules/numberingeditor.h"
#include "rules/numberingrule.h"
#include "rules/removeeditor.h"
#include "rules/removerule.h"
#include "rules/replaceeditor.h"
#include "rules/ruleeditorfactory.h"
#include "rules/ruleregistry.h"
#include "theme/tokens.h"
#include "widgets/formutils.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStyleOptionButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

#include <functional>
#include <memory>

using namespace ctrm;
using namespace ctrm::app;

namespace {

template <typename T>
T *child(QWidget *parent, const char *name)
{
    T *w = parent->findChild<T *>(QString::fromLatin1(name));
    if (!w)
        qWarning("no child %s", name);
    return w;
}

int rowOf(QFormLayout *form, QWidget *w)
{
    for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem *item = form->itemAt(row, QFormLayout::FieldRole);
        QWidget *field = item ? item->widget() : nullptr;
        if (field && (field == w || field->isAncestorOf(w)))
            return row;
    }
    return -1;
}

bool rowVisible(QFormLayout *form, QWidget *w)
{
    const int row = rowOf(form, w);
    return row >= 0 && form->isRowVisible(row);
}

void selectData(QComboBox *combo, const QVariant &data)
{
    combo->setCurrentIndex(combo->findData(data));
}

// The x of the first visible field of the form, relative to the left edge of the form. The page
// that holds a form sets its margins, so the label column is the part to compare.
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

template <typename Editor, typename RuleType>
struct Fixture
{
    std::unique_ptr<Editor> editor = std::make_unique<Editor>(nullptr);
    RuleType rule;

    Fixture()
    {
        editor->setRule(&rule, 1);
        editor->resize(480, 640);
        editor->show();
    }
};

} // namespace

class TestAppEditors2 : public QObject
{
    Q_OBJECT
private slots:
    void ruleTypes();
    void caseLabelAligned();
    void caseWritesRule();
    void caseLoadsRule();
    void numberingDefaults();
    void numberingWritesRule();
    void numberingExample();
    void numberingOrderNoteVisible();
    void extensionNoApplyTo();
    void extensionInlineFields();
    void extensionWritesRule();
    void inlineFieldsFullHeight();
    void tabOrderFollowsRows();
    void comboWidthsMatch();
    void formRhythm();
    void extensionFolderNote();
    void chainOptionsDefaults();
    void chainOptionsChangedCount();
    void chainOptionsHeaderChevron();
    void chainOptionsLabelsWrap();
    void sameLabelColumnAllPages();
    void mnemonicsReachAControl();
    void setRuleEmitsNothing();
    void wrongRuleTypeDisables();
};

void TestAppEditors2::ruleTypes()
{
    QCOMPARE(ChangeCaseEditor(nullptr).ruleType(), QStringLiteral("changeCase"));
    QCOMPARE(NumberingEditor(nullptr).ruleType(), QStringLiteral("numbering"));
    QCOMPARE(ExtensionEditor(nullptr).ruleType(), QStringLiteral("extension"));
}

void TestAppEditors2::caseLabelAligned()
{
    Fixture<ChangeCaseEditor, ChangeCaseRule> f;
    QVERIFY(QTest::qWaitForWindowExposed(f.editor.get()));
    auto *lower = child<QRadioButton>(f.editor.get(), "lower");
    QFormLayout *form = f.editor->form();
    QLabel *label = nullptr;
    for (int row = 0; row < form->rowCount() && !label; ++row) {
        if (ctrm::test::itemHolds(form->itemAt(row, QFormLayout::FieldRole), lower))
            label = qobject_cast<QLabel *>(form->itemAt(row, QFormLayout::LabelRole)->widget());
    }
    QVERIFY(label);
    QCOMPARE(label->text(), QStringLiteral("Case:"));
    const QFontMetrics lfm(label->font());
    const int labelBaseline = label->mapTo(f.editor.get(), QPoint(0, 0)).y() +
                              (label->height() - lfm.height()) / 2 + lfm.ascent();
    QStyleOptionButton opt;
    opt.initFrom(lower);
    opt.text = lower->text();
    const QRect r = lower->style()->subElementRect(QStyle::SE_RadioButtonContents, &opt, lower);
    const QFontMetrics rfm(lower->font());
    const int radioBaseline = lower->mapTo(f.editor.get(), QPoint(0, 0)).y() + r.center().y() -
                              rfm.height() / 2 + rfm.ascent();
    QVERIFY2(std::abs(labelBaseline - radioBaseline) <= 1,
             qPrintable(QStringLiteral("%1 vs %2").arg(labelBaseline).arg(radioBaseline)));
}

void TestAppEditors2::caseWritesRule()
{
    Fixture<ChangeCaseEditor, ChangeCaseRule> f;
    ChangeCaseEditor *e = f.editor.get();
    QSignalSpy edited(e, &RuleEditor::ruleEdited);
    QStringList labels;
    for (const char *name : {"lower", "upper", "title", "sentence", "invert"})
        labels.append(child<QRadioButton>(e, name)->text());
    QCOMPARE(labels, (QStringList{"lower case", "UPPER CASE", "Title Case", "Sentence case",
                                  "iNVERT cASE"}));
    QVERIFY(child<QRadioButton>(e, "lower")->isChecked());

    auto *separators = child<QLineEdit>(e, "separators");
    auto *more = child<QToolButton>(e, "moreOptions");
    QVERIFY(!rowVisible(e->form(), more));
    QVERIFY(!rowVisible(e->form(), separators));

    child<QRadioButton>(e, "title")->click();
    QCOMPARE(f.rule.caseMode, ChangeCaseRule::Case::Title);
    QCOMPARE(edited.count(), 1);
    QVERIFY(rowVisible(e->form(), more));
    QVERIFY(!rowVisible(e->form(), separators)); // More options is closed
    more->click();
    QVERIFY(rowVisible(e->form(), separators));
    QVERIFY(rowVisible(e->form(), child<QLineEdit>(e, "smallWords")));
    QVERIFY(rowVisible(e->form(), child<QCheckBox>(e, "builtInSmallWords")));
    // Apply to is above More options.
    QVERIFY(rowOf(e->form(), child<QComboBox>(e, "applyTo")) < rowOf(e->form(), more));

    child<QLineEdit>(e, "separators")->clear();
    QTest::keyClicks(separators, QStringLiteral(" _"));
    QTRY_COMPARE_WITH_TIMEOUT(edited.count(), 2, 1000);
    QCOMPARE(f.rule.separators, QStringLiteral(" _"));
    QTest::keyClicks(child<QLineEdit>(e, "smallWords"), QStringLiteral("a, the ,,of"));
    QTRY_COMPARE_WITH_TIMEOUT(edited.count(), 3, 1000);
    QCOMPARE(f.rule.smallWords, (QStringList{"a", "the", "of"}));
    child<QCheckBox>(e, "builtInSmallWords")->click();
    QVERIFY(f.rule.builtInSmallWords);

    child<QRadioButton>(e, "upper")->click();
    QCOMPARE(f.rule.caseMode, ChangeCaseRule::Case::Upper);
    QVERIFY(!rowVisible(e->form(), separators));
    QVERIFY(!rowVisible(e->form(), more));
    child<QRadioButton>(e, "title")->click();
    QVERIFY(rowVisible(e->form(), separators)); // still open
}

void TestAppEditors2::caseLoadsRule()
{
    Fixture<ChangeCaseEditor, ChangeCaseRule> f;
    ChangeCaseRule other;
    other.caseMode = ChangeCaseRule::Case::Sentence;
    other.smallWords = QStringList{"a", "of"};
    other.target = Target::Name;
    f.editor->setRule(&other, 4);
    QVERIFY(child<QRadioButton>(f.editor.get(), "sentence")->isChecked());
    QCOMPARE(child<QLineEdit>(f.editor.get(), "smallWords")->text(), QStringLiteral("a, of"));
    QCOMPARE(child<QComboBox>(f.editor.get(), "applyTo")->currentData().toString(),
             QStringLiteral("name"));
    QCOMPARE(child<QLabel>(f.editor.get(), "ruleEditorTitle")->text(),
             QStringLiteral("Rule 4: Change case"));
    QCOMPARE(f.rule.caseMode, ChangeCaseRule::Case::Lower);
}

void TestAppEditors2::numberingDefaults()
{
    Fixture<NumberingEditor, NumberingRule> f;
    NumberingEditor *e = f.editor.get();
    QCOMPARE(child<QSpinBox>(e, "start")->value(), 1);
    QCOMPARE(child<QSpinBox>(e, "step")->value(), 1);
    QCOMPARE(child<QSpinBox>(e, "start")->minimum(), 0);
    QCOMPARE(child<QSpinBox>(e, "step")->minimum(), 1);
    auto *padding = child<QComboBox>(e, "padding");
    QCOMPARE(padding->currentData().toInt(), -1);
    QCOMPARE(padding->currentText(), QStringLiteral("Auto"));
    QCOMPARE(padding->count(), 11);
    QCOMPARE(padding->itemData(10).toInt(), 10);
    QVERIFY(child<QRadioButton>(e, "suffix")->isChecked());
    QCOMPARE(child<QLineEdit>(e, "separator")->text(), QStringLiteral("_"));
    auto *scope = child<QComboBox>(e, "scope");
    QCOMPARE(scope->currentText(), QStringLiteral("Included items only"));
    QCOMPARE(scope->itemText(1), QStringLiteral("All items"));
    QVERIFY(!child<QSpinBox>(e, "index")->isEnabled());
    QCOMPARE(child<QRadioButton>(e, "replaceName")->text(), QStringLiteral("Replace the name"));
}

void TestAppEditors2::numberingWritesRule()
{
    Fixture<NumberingEditor, NumberingRule> f;
    NumberingEditor *e = f.editor.get();
    QSignalSpy edited(e, &RuleEditor::ruleEdited);
    child<QSpinBox>(e, "start")->setValue(5);
    child<QSpinBox>(e, "step")->setValue(2);
    selectData(child<QComboBox>(e, "padding"), 3);
    child<QRadioButton>(e, "atIndex")->click();
    QVERIFY(child<QSpinBox>(e, "index")->isEnabled());
    child<QSpinBox>(e, "index")->setValue(2);
    child<QCheckBox>(e, "fromEnd")->click();
    selectData(child<QComboBox>(e, "scope"), int(NumberingRule::Scope::All));
    QCOMPARE(f.rule.start, 5);
    QCOMPARE(f.rule.step, 2);
    QCOMPARE(f.rule.padding, 3);
    QCOMPARE(f.rule.position, NumberingRule::Position::Index);
    QCOMPARE(f.rule.index, 2);
    QVERIFY(f.rule.fromEnd);
    QCOMPARE(f.rule.scope, NumberingRule::Scope::All);
    QCOMPARE(edited.count(), 7);
    child<QRadioButton>(e, "replaceName")->click();
    QCOMPARE(f.rule.position, NumberingRule::Position::Replace);
    child<QLineEdit>(e, "separator")->clear();
    QTest::keyClicks(child<QLineEdit>(e, "separator"), QStringLiteral("-"));
    QTRY_COMPARE_WITH_TIMEOUT(f.rule.separator, QStringLiteral("-"), 1000);
    selectData(child<QComboBox>(e, "padding"), -1);
    QCOMPARE(f.rule.padding, -1);

    // A loaded padding of 0 shows as 1 digit, and the rule does not change.
    NumberingRule zero;
    zero.padding = 0;
    f.editor->setRule(&zero, 1);
    QCOMPARE(child<QComboBox>(e, "padding")->currentData().toInt(), 1);
    QCOMPARE(zero.padding, 0);
}

void TestAppEditors2::numberingExample()
{
    Fixture<NumberingEditor, NumberingRule> f;
    NumberingEditor *e = f.editor.get();
    auto *example = child<QLabel>(e, "example");
    f.editor->setNumberedCount(120);
    QCOMPARE(example->text(), QStringLiteral("Example: 001"));
    selectData(child<QComboBox>(e, "padding"), 4);
    QCOMPARE(example->text(), QStringLiteral("Example: 0001"));
    child<QSpinBox>(e, "start")->setValue(7);
    QCOMPARE(example->text(), QStringLiteral("Example: 0007"));
    selectData(child<QComboBox>(e, "padding"), -1);
    f.editor->setNumberedCount(5);
    QCOMPARE(example->text(), QStringLiteral("Example: 07")); // 7 + 4 * 1 = 11: two digits
    // The example is on the line of the padding combo box.
    QCOMPARE(rowOf(e->form(), example), rowOf(e->form(), child<QComboBox>(e, "padding")));
}

void TestAppEditors2::numberingOrderNoteVisible()
{
    Fixture<NumberingEditor, NumberingRule> f;
    auto *note = child<QLabel>(f.editor.get(), "orderNote");
    QCOMPARE(note->text(),
             QStringLiteral("Order: the table order. Sort or drag rows to change it."));
    QVERIFY(rowVisible(f.editor->form(), note));
    QCOMPARE(note->palette().color(QPalette::WindowText),
             f.editor->palette().color(QPalette::PlaceholderText));
    // The note is the last row, under Apply to.
    QCOMPARE(rowOf(f.editor->form(), note), f.editor->form()->rowCount() - 1);
    QCOMPARE(rowOf(f.editor->form(), child<QComboBox>(f.editor.get(), "applyTo")),
             f.editor->form()->rowCount() - 2);
}

void TestAppEditors2::extensionNoApplyTo()
{
    Fixture<ExtensionEditor, ExtensionRule> f;
    QFormLayout *form = f.editor->form();
    for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
        if (auto *label = item ? qobject_cast<QLabel *>(item->widget()) : nullptr)
            QVERIFY(!label->text().contains(QStringLiteral("Apply to")));
    }
    QVERIFY(!f.editor->findChild<QComboBox *>(QStringLiteral("applyTo")));
}

void TestAppEditors2::extensionInlineFields()
{
    Fixture<ExtensionEditor, ExtensionRule> f;
    ExtensionEditor *e = f.editor.get();
    auto *setValue = child<QLineEdit>(e, "setValue");
    auto *addValue = child<QLineEdit>(e, "addValue");
    QVERIFY(child<QRadioButton>(e, "lower")->isChecked());
    QVERIFY(!setValue->isEnabled());
    QVERIFY(!addValue->isEnabled());
    child<QRadioButton>(e, "set")->click();
    QVERIFY(setValue->isEnabled());
    QVERIFY(!addValue->isEnabled());
    child<QRadioButton>(e, "add")->click();
    QVERIFY(!setValue->isEnabled());
    QVERIFY(addValue->isEnabled());
    QCOMPARE(rowOf(e->form(), setValue), rowOf(e->form(), child<QRadioButton>(e, "set")));
    QStringList labels;
    for (const char *name : {"lower", "upper", "set", "add", "remove"})
        labels.append(child<QRadioButton>(e, name)->text());
    QCOMPARE(labels, (QStringList{"lower case", "UPPER CASE", "Set to", "Add", "Remove"}));
    QCOMPARE(child<QLabel>(e, "onlyForHint")->text(), QStringLiteral("Empty = all items"));
}

namespace {

// True when each named field of the editor has at least the height of its size hint.
template <typename Editor, typename RuleType>
bool fieldsFullHeight(const QStringList &names, const std::function<void(Editor *)> &setUp = {})
{
    Fixture<Editor, RuleType> f;
    Editor *e = f.editor.get();
    if (setUp)
        setUp(e);
    if (!QTest::qWaitForWindowExposed(e))
        return false;
    bool ok = true;
    for (const QString &name : names) {
        auto *field = e->template findChild<QWidget *>(name);
        // The form rhythm gives a control its fixed height (control-h, C03); without one, the
        // size hint.
        const int full = field && field->minimumHeight() == field->maximumHeight()
                                 ? field->minimumHeight()
                                 : (field ? field->sizeHint().height() : 0);
        if (!field || field->height() < full) {
            qWarning() << "squashed" << name << (field ? field->geometry() : QRect());
            ok = false;
        }
    }
    return ok;
}

} // namespace

namespace {

// The first widget of a field item: the widget, or the widget in row 0, column 0 of a group.
QLayoutItem *firstControlItem(QLayoutItem *field)
{
    if (field->widget())
        return field;
    auto *grid = qobject_cast<QGridLayout *>(field->layout());
    return grid ? grid->itemAtPosition(0, 0) : nullptr;
}

} // namespace

void TestAppEditors2::formRhythm()
{
    // C03, C04, C09 (ui-design 6.1 rules 2, 8, 15): control rows are control-h high with
    // space-2 between rows (32 px pitch); check box and radio rows in a group have a 24 px
    // pitch; all visible edges are on the field edge; a text field or combo box alone on its
    // row fills the field column up to field-w-max; the label text is centred on its first row.
    using namespace ctrm::app::tokens;
    for (const QString &type : ctrm::ruleTypes()) {
        std::unique_ptr<RuleEditor> editor(createRuleEditor(type, nullptr));
        QVERIFY(editor);
        editor->resize(520, 900);
        editor->show();
        QVERIFY(QTest::qWaitForWindowExposed(editor.get()));
        QApplication::processEvents();
        QFormLayout *form = editor->form();
        int edge = -1;
        int previousBottom = -1;
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem *field = form->itemAt(row, QFormLayout::FieldRole);
            if (!field || !form->isRowVisible(row))
                continue;
            const QString at = QStringLiteral("%1 row %2").arg(type).arg(row);
            // The visible rect: for a group, the union of its controls (the grid itself can
            // start above them, for the layout margins of the style).
            QRect rect = field->geometry();
            if (QLayout *group = field->layout()) {
                rect = QRect();
                for (int i = 0; i < group->count(); ++i) {
                    QLayoutItem *item = group->itemAt(i);
                    if (item->widget() && !item->widget()->isHidden())
                        rect |= item->geometry();
                }
            }
            if (previousBottom >= 0)
                QVERIFY2(rect.top() - previousBottom == space2,
                         qPrintable(at +
                                    QStringLiteral(": gap %1").arg(rect.top() - previousBottom)));
            previousBottom = rect.bottom() + 1;
            QLayoutItem *first = firstControlItem(field);
            QVERIFY2(first, qPrintable(at));
            if (edge < 0)
                edge = first->geometry().left();
            QVERIFY2(first->geometry().left() == edge,
                     qPrintable(at + QStringLiteral(": edge %1, not %2")
                                             .arg(first->geometry().left())
                                             .arg(edge)));
            QWidget *w = first->widget();
            const bool control = qobject_cast<QLineEdit *>(w) || qobject_cast<QComboBox *>(w) ||
                                 qobject_cast<QAbstractSpinBox *>(w) ||
                                 qobject_cast<QToolButton *>(w);
            const bool check = qobject_cast<QCheckBox *>(w) || qobject_cast<QRadioButton *>(w);
            if (control || check) {
                QCOMPARE(first->geometry().height(), control ? controlH : controlH - space1);
                if (auto *label = qobject_cast<QLabel *>(
                            form->itemAt(row, QFormLayout::LabelRole)
                                    ? form->itemAt(row, QFormLayout::LabelRole)->widget()
                                    : nullptr);
                    label && !label->text().isEmpty()) {
                    QVERIFY2(qAbs(label->geometry().center().y() -
                                  first->geometry().center().y()) <= 1,
                             qPrintable(at + QStringLiteral(": label centre %1 %2 vs %3 %4 w %5")
                                                     .arg(label->geometry().top())
                                                     .arg(label->height())
                                                     .arg(first->geometry().top())
                                                     .arg(first->geometry().height())
                                                     .arg(first->widget()->geometry().top())));
                }
            }
            if (auto *grid = qobject_cast<QGridLayout *>(field->layout())) {
                for (int r = 1; r < grid->rowCount(); ++r) {
                    QLayoutItem *a = grid->itemAtPosition(r - 1, 0);
                    QLayoutItem *b = grid->itemAtPosition(r, 0);
                    if (!a || !b || !a->widget() || !b->widget() || b->widget()->isHidden())
                        continue;
                    const bool plain = qobject_cast<QAbstractButton *>(a->widget()) &&
                                       grid->itemAtPosition(r - 1, 1) == nullptr;
                    if (plain) // a check box or radio row with no sub-field
                        QVERIFY2(b->geometry().top() - a->geometry().top() == controlH,
                                 qPrintable(at + QStringLiteral(" group row %1").arg(r)));
                }
            }
            if ((qobject_cast<QLineEdit *>(field->widget()) ||
                 qobject_cast<QComboBox *>(field->widget())) &&
                field->widget()->minimumWidth() != field->widget()->maximumWidth()) {
                const int column = form->geometry().right() + 1 - edge;
                QVERIFY2(rect.width() == std::min(column, fieldWMax),
                         qPrintable(at + QStringLiteral(": width %1").arg(rect.width())));
            }
        }
    }
}

void TestAppEditors2::comboWidthsMatch()
{
    // M25 (ui-design 6.1 rule 8): combo boxes alone on their row have one width; Padding keeps
    // its size hint; the separator field is for a few characters.
    Fixture<NumberingEditor, NumberingRule> numbering;
    NumberingEditor *n = numbering.editor.get();
    QVERIFY(QTest::qWaitForWindowExposed(n));
    auto *scope = child<QComboBox>(n, "scope");
    auto *applyTo = child<QComboBox>(n, "applyTo");
    auto *padding = child<QComboBox>(n, "padding");
    QCOMPARE(scope->width(), applyTo->width());
    QCOMPARE(padding->width(), padding->sizeHint().width());
    auto *separator = child<QLineEdit>(n, "separator");
    QCOMPARE(separator->minimumWidth(), separator->maximumWidth());
    QVERIFY(separator->width() < scope->width() / 2);

    std::unique_ptr<RuleEditor> replace(createRuleEditor(QStringLiteral("replace"), nullptr));
    replace->resize(480, 640);
    replace->show();
    QVERIFY(QTest::qWaitForWindowExposed(replace.get()));
    const int mode = child<QComboBox>(replace.get(), "mode")->width();
    QCOMPARE(child<QComboBox>(replace.get(), "occurrence")->width(), mode);
    QCOMPARE(child<QComboBox>(replace.get(), "applyTo")->width(), mode);
}

void TestAppEditors2::tabOrderFollowsRows()
{
    // ui-design 12 (with M29): Tab goes down the form rows; Apply to is not first.
    for (const QString &type :
         {"replace", "insert", "remove", "changeCase", "numbering", "extension", "cleanup"}) {
        std::unique_ptr<RuleEditor> e(createRuleEditor(type, nullptr));
        QFormLayout *form = e->form();
        const auto formRow = [form](const QWidget *w) {
            for (int row = 0; row < form->rowCount(); ++row) {
                if (ctrm::test::itemHolds(form->itemAt(row, QFormLayout::FieldRole), w))
                    return row;
            }
            return -1;
        };
        int last = -1;
        QStringList order;
        for (QWidget *w = e->nextInFocusChain(); w != e.get(); w = w->nextInFocusChain()) {
            const int row = formRow(w);
            if (row < 0 || !(w->focusPolicy() & Qt::TabFocus))
                continue;
            order.append(w->objectName());
            QVERIFY2(row >= last, qPrintable(type + u": " + order.join(u' ')));
            last = row;
        }
    }
}

// M05: an inline sub-field next to a radio button or a check box has its normal height.
void TestAppEditors2::inlineFieldsFullHeight()
{
    QVERIFY((fieldsFullHeight<ExtensionEditor, ExtensionRule>({"setValue", "addValue"})));
    QVERIFY((fieldsFullHeight<InsertEditor, InsertRule>({"index"})));
    QVERIFY((fieldsFullHeight<NumberingEditor, NumberingRule>({"index"})));
    QVERIFY((fieldsFullHeight<RemoveEditor, RemoveRule>({"customText"}, [](RemoveEditor *e) {
        selectData(child<QComboBox>(e, "mode"), int(RemoveRule::Mode::Characters));
    })));
}

void TestAppEditors2::extensionWritesRule()
{
    Fixture<ExtensionEditor, ExtensionRule> f;
    ExtensionEditor *e = f.editor.get();
    QSignalSpy edited(e, &RuleEditor::ruleEdited);
    child<QRadioButton>(e, "set")->click();
    QCOMPARE(f.rule.action, ExtensionRule::Action::Set);
    QCOMPARE(edited.count(), 1);
    QTest::keyClicks(child<QLineEdit>(e, "setValue"), QStringLiteral("jpg"));
    QTRY_COMPARE_WITH_TIMEOUT(edited.count(), 2, 1000);
    QCOMPARE(f.rule.value, QStringLiteral("jpg"));
    QTest::keyClicks(child<QLineEdit>(e, "onlyIfMatches"), QStringLiteral("jpeg;jpe"));
    QTRY_COMPARE_WITH_TIMEOUT(edited.count(), 3, 1000);
    QCOMPARE(f.rule.onlyIfMatches, QStringLiteral("jpeg;jpe"));

    // Set and Add share the value; the field of the selected action shows it.
    child<QRadioButton>(e, "add")->click();
    QCOMPARE(f.rule.action, ExtensionRule::Action::Add);
    QCOMPARE(child<QLineEdit>(e, "addValue")->text(), QStringLiteral("jpg"));
    child<QLineEdit>(e, "addValue")->clear();
    QTest::keyClicks(child<QLineEdit>(e, "addValue"), QStringLiteral("bak"));
    QTRY_COMPARE_WITH_TIMEOUT(f.rule.value, QStringLiteral("bak"), 1000);
    child<QRadioButton>(e, "remove")->click();
    QCOMPARE(f.rule.action, ExtensionRule::Action::Remove);

    ExtensionRule loaded;
    loaded.action = ExtensionRule::Action::Set;
    loaded.value = QStringLiteral("png");
    f.editor->setRule(&loaded, 6);
    QVERIFY(child<QRadioButton>(e, "set")->isChecked());
    QCOMPARE(child<QLineEdit>(e, "setValue")->text(), QStringLiteral("png"));
    QVERIFY(child<QLineEdit>(e, "setValue")->isEnabled());
}

void TestAppEditors2::extensionFolderNote()
{
    Fixture<ExtensionEditor, ExtensionRule> f;
    auto *note = child<QLabel>(f.editor.get(), "folderNote");
    QVERIFY(!rowVisible(f.editor->form(), note));
    f.editor->setFolderExtensions(true);
    QVERIFY(rowVisible(f.editor->form(), note));
    QCOMPARE(note->text(),
             QStringLiteral(
                     "Folders are changed too (Treat folder names as name + extension is on)."));
    f.editor->setFolderExtensions(false);
    QVERIFY(!rowVisible(f.editor->form(), note));
}

void TestAppEditors2::chainOptionsDefaults()
{
    ChainOptionsWidget w(nullptr);
    w.setOptions(ChainOptions{});
    auto *portable = child<QCheckBox>(&w, "portableNames");
    auto *duplicates = child<QCheckBox>(&w, "autoResolveDuplicates");
    auto *folders = child<QCheckBox>(&w, "folderExtensions");
    QVERIFY(portable->isChecked());
    QVERIFY(!duplicates->isChecked());
    QVERIFY(!folders->isChecked());
    // The text is in a wrapping label next to the check box (O08, C06); the check box has it as
    // its accessible name.
    QCOMPARE(portable->accessibleName(),
             QStringLiteral("Use portable names (check Windows rules on all systems)"));
    QCOMPARE(duplicates->accessibleName(),
             QStringLiteral("Add a number to duplicate names: x (2).txt"));
    QCOMPARE(folders->accessibleName(), QStringLiteral("Treat folder names as name + extension"));
    auto *header = child<QToolButton>(&w, "chainOptionsHeader");
    QCOMPARE(header->text(), QStringLiteral("Chain options"));
    QCOMPARE(w.options(), ChainOptions{});

    // Closed by default; setOpen shows the check boxes.
    w.show();
    QVERIFY(!portable->isVisible());
    w.setOpen(true);
    QVERIFY(portable->isVisible());
    QVERIFY(header->isChecked());
    header->click();
    QVERIFY(!portable->isVisible());
}

void TestAppEditors2::chainOptionsChangedCount()
{
    ChainOptionsWidget w(nullptr);
    w.setOptions(ChainOptions{});
    QSignalSpy changed(&w, &ChainOptionsWidget::optionsChanged);
    auto *header = child<QToolButton>(&w, "chainOptionsHeader");
    child<QCheckBox>(&w, "portableNames")->click();
    QCOMPARE(header->text(), QStringLiteral("Chain options (1 changed)"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).value<ChainOptions>().portableNames, false);
    child<QCheckBox>(&w, "folderExtensions")->click();
    QCOMPARE(header->text(), QStringLiteral("Chain options (2 changed)"));
    QCOMPARE(w.options(), (ChainOptions{false, false, true}));

    // setOptions changes no signal, and updates the header.
    w.setOptions(ChainOptions{true, true, false});
    QCOMPARE(changed.count(), 2);
    QCOMPARE(header->text(), QStringLiteral("Chain options (1 changed)"));
    QVERIFY(child<QCheckBox>(&w, "autoResolveDuplicates")->isChecked());
}

void TestAppEditors2::chainOptionsHeaderChevron()
{
    // O04 (ui-design 8.10): a 12 px chevron from the icon set, not a style arrow. The gap over the
    // header is space-4 with a 1 px separator line in the middle.
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *w = new ChainOptionsWidget(&host);
    layout->addWidget(w);
    layout->addStretch();
    host.resize(400, 300);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto *header = child<QToolButton>(w, "chainOptionsHeader");
    QCOMPARE(header->arrowType(), Qt::NoArrow);
    QCOMPARE(header->iconSize(), QSize(tokens::iconXs, tokens::iconXs));
    QVERIFY(!header->icon().isNull());
    const qint64 closed = header->icon().cacheKey();
    w->setOpen(true);
    QVERIFY(header->icon().cacheKey() != closed);
    w->setOpen(false);
    QCOMPARE(header->icon().cacheKey(), closed);

    auto *line = w->findChild<QWidget *>(QStringLiteral("chainOptionsSeparator"));
    QVERIFY(line && line->isVisible());
    QCOMPARE(line->height(), 1);
    const QRect lineRect = ctrm::test::visualRect(line, w);
    QCOMPARE(lineRect.top(), tokens::space2);
    const QRect headerRect = ctrm::test::visualRect(header, w);
    QCOMPARE(headerRect.top() - lineRect.top(), tokens::space4 - tokens::space2);
}

void TestAppEditors2::chainOptionsLabelsWrap()
{
    // O08, C06 (ui-design 8.10): the check box labels wrap in a narrow column and do not make the
    // column wider. A click on a label changes its check box.
    ChainOptionsWidget w(nullptr);
    w.setOptions(ChainOptions{});
    w.setOpen(true);
    auto *portable = child<QCheckBox>(&w, "portableNames");
    QLabel *text = nullptr;
    for (QLabel *label : w.findChildren<QLabel *>()) {
        if (label->text() == portable->accessibleName())
            text = label;
    }
    QVERIFY(text);
    QVERIFY(text->wordWrap());
    const int oneLine = text->fontMetrics().horizontalAdvance(text->text());
    QVERIFY2(w.minimumSizeHint().width() < oneLine,
             qPrintable(QStringLiteral("%1 >= %2").arg(w.minimumSizeHint().width()).arg(oneLine)));
    w.resize(oneLine / 2 + 60, 400);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QVERIFY(text->geometry().right() < w.width());
    QVERIFY(text->height() >= 2 * text->fontMetrics().lineSpacing());
    QVERIFY(text->mapTo(&w, QPoint(0, text->height())).y() <= w.height());

    // The boxes have the group height (20 px visible), as in the rule editors (C03).
    for (const char *name : {"portableNames", "autoResolveDuplicates", "folderExtensions"}) {
        auto *check = child<QCheckBox>(&w, name);
        QCOMPARE(ctrm::test::visualRect(check, &w).height(), tokens::controlH - tokens::space1);
    }

    QVERIFY(portable->isChecked());
    QTest::mouseClick(text, Qt::LeftButton, {}, QPoint(4, 4));
    QVERIFY(!portable->isChecked());
    QCOMPARE(w.options().portableNames, false);
}

void TestAppEditors2::sameLabelColumnAllPages()
{
    QWidget host;
    QList<RuleEditor *> editors{new ReplaceEditor(&host),    new InsertEditor(&host),
                                new RemoveEditor(&host),     new CleanUpEditor(&host),
                                new ChangeCaseEditor(&host), new NumberingEditor(&host),
                                new ExtensionEditor(&host)};
    auto *chainOptions = new ChainOptionsWidget(&host);
    LabelColumnSync sync;
    for (RuleEditor *e : editors)
        sync.addForm(e->form());
    sync.addForm(chainOptions->form());
    sync.sync();
    host.resize(520, 700);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QSet<int> edges;
    for (RuleEditor *e : editors) {
        e->resize(500, 680);
        e->show();
        QApplication::processEvents();
        edges.insert(fieldEdge(e->form()));
    }
    chainOptions->setOpen(true);
    chainOptions->resize(500, 200);
    chainOptions->show();
    QApplication::processEvents();
    edges.insert(fieldEdge(chainOptions->form()));
    QCOMPARE(edges.size(), 1);
    QVERIFY(*edges.begin() > 0);
}

// ui-design 6.1 rule 10: each label mnemonic moves the focus to a control.
void TestAppEditors2::mnemonicsReachAControl()
{
    QWidget host;
    const QList<RuleEditor *> editors{new ReplaceEditor(&host),    new InsertEditor(&host),
                                      new RemoveEditor(&host),     new CleanUpEditor(&host),
                                      new ChangeCaseEditor(&host), new NumberingEditor(&host),
                                      new ExtensionEditor(&host)};
    for (RuleEditor *e : editors) {
        QFormLayout *form = e->form();
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
            auto *label = item ? qobject_cast<QLabel *>(item->widget()) : nullptr;
            if (!label || !label->text().contains(QLatin1Char('&')))
                continue;
            QWidget *target = label->buddy();
            QVERIFY2(target, qPrintable(label->text()));
            while (target->focusProxy())
                target = target->focusProxy();
            QVERIFY2(target->focusPolicy() != Qt::NoFocus,
                     qPrintable(e->ruleType() + QLatin1Char(' ') + label->text()));
        }
    }
}

void TestAppEditors2::setRuleEmitsNothing()
{
    ChangeCaseRule caseRule;
    caseRule.caseMode = ChangeCaseRule::Case::Title;
    caseRule.builtInSmallWords = true;
    NumberingRule numbering;
    numbering.start = 9;
    numbering.padding = 3;
    numbering.position = NumberingRule::Position::Index;
    numbering.scope = NumberingRule::Scope::All;
    ExtensionRule extension;
    extension.action = ExtensionRule::Action::Add;
    extension.value = QStringLiteral("bak");
    const QList<std::pair<RuleEditor *, Rule *>> pairs{{new ChangeCaseEditor(nullptr), &caseRule},
                                                       {new NumberingEditor(nullptr), &numbering},
                                                       {new ExtensionEditor(nullptr), &extension}};
    for (const auto &[editor, rule] : pairs) {
        std::unique_ptr<RuleEditor> owner(editor);
        QSignalSpy edited(editor, &RuleEditor::ruleEdited);
        editor->setRule(rule, 2);
        QTest::qWait(200); // longer than the text debounce
        QCOMPARE(edited.count(), 0);
    }
    QCOMPARE(numbering.start, 9);
    QCOMPARE(numbering.padding, 3);
    QCOMPARE(extension.value, QStringLiteral("bak"));
}

void TestAppEditors2::wrongRuleTypeDisables()
{
    Fixture<NumberingEditor, NumberingRule> f;
    ExtensionRule other;
    QSignalSpy edited(f.editor.get(), &RuleEditor::ruleEdited);
    f.editor->setRule(&other, 2);
    QVERIFY(!f.editor->isEnabled());
    f.editor->setRule(nullptr, 2);
    QVERIFY(!f.editor->isEnabled());
    child<QSpinBox>(f.editor.get(), "start")->setValue(4);
    QCOMPARE(edited.count(), 0);
    QCOMPARE(f.rule.start, 1);
    f.editor->setRule(&f.rule, 1);
    QVERIFY(f.editor->isEnabled());

    Fixture<ChangeCaseEditor, ChangeCaseRule> c;
    c.editor->setRule(&other, 2);
    QVERIFY(!c.editor->isEnabled());
    Fixture<ExtensionEditor, ExtensionRule> x;
    x.editor->setRule(&f.rule, 2);
    QVERIFY(!x.editor->isEnabled());
}

QTEST_MAIN(TestAppEditors2)
#include "tst_app_editors2.moc"
