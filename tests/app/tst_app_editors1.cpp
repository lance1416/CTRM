// SPDX-License-Identifier: GPL-3.0-or-later
#include "layoutedge.h"
#include "rules/cleanupeditor.h"
#include "rules/cleanuprule.h"
#include "rules/inserteditor.h"
#include "rules/insertrule.h"
#include "rules/removeeditor.h"
#include "rules/removerule.h"
#include "rules/replaceeditor.h"
#include "rules/replacerule.h"
#include "theme/typography.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QtTest>

#include <memory>

using namespace ctrm;
using namespace ctrm::app;

namespace {

template <typename T>
T *child(QWidget *editor, const char *name)
{
    T *w = editor->findChild<T *>(QString::fromLatin1(name));
    if (!w)
        qWarning("no child %s", name);
    return w;
}

// The row of the form that holds w (as the field or inside the field), or -1.
int rowOf(QFormLayout *form, QWidget *w)
{
    for (int row = 0; row < form->rowCount(); ++row) {
        if (ctrm::test::itemHolds(form->itemAt(row, QFormLayout::FieldRole), w))
            return row;
    }
    return -1;
}

bool rowVisible(RuleEditor *editor, QWidget *w)
{
    const int row = rowOf(editor->form(), w);
    return row >= 0 && editor->form()->isRowVisible(row);
}

// The left edges of the fields of all visible rows, relative to the editor.
QList<int> fieldEdges(RuleEditor *editor)
{
    QList<int> edges;
    QFormLayout *form = editor->form();
    for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem *item = form->itemAt(row, QFormLayout::FieldRole);
        if (item && form->isRowVisible(row))
            edges.append(ctrm::test::fieldVisualLefts(item, editor));
    }
    return edges;
}

bool oneFieldEdge(RuleEditor *editor)
{
    const QList<int> edges = fieldEdges(editor);
    for (const int e : edges) {
        if (e != edges.first())
            return false;
    }
    return edges.size() >= 2;
}

void selectData(QComboBox *combo, const QVariant &data)
{
    combo->setCurrentIndex(combo->findData(data));
}

template <typename Editor, typename RuleType>
struct Fixture
{
    std::unique_ptr<Editor> editor = std::make_unique<Editor>(nullptr);
    RuleType rule;

    Fixture()
    {
        editor->setRule(&rule, 1);
        editor->resize(480, 600);
        editor->show();
    }
};

} // namespace

class TestAppEditors1 : public QObject
{
    Q_OBJECT
private slots:
    void ruleTypes();
    void titles();
    void replaceWritesRule();
    void replaceDefaults();
    void replaceLoadsRule();
    void replaceWholeWordOnlyLiteral();
    void replaceHints();
    void replaceRegexError();
    void replaceMonospaceInRegex();
    void insertIndexEnabled();
    void insertWritesRule();
    void removeModeRows();
    void removeWritesRule();
    void cleanupWritesRule();
    void cleanupNoteFollowsPortable();
    void cleanupReplacementFits();
    void applyToWritesTarget();
    void setRuleFlushesPendingEdit();
    void wrongRuleTypeDisables();
    void fieldEdgeAllEditors();
};

void TestAppEditors1::ruleTypes()
{
    QCOMPARE(ReplaceEditor(nullptr).ruleType(), QStringLiteral("replace"));
    QCOMPARE(InsertEditor(nullptr).ruleType(), QStringLiteral("insert"));
    QCOMPARE(RemoveEditor(nullptr).ruleType(), QStringLiteral("remove"));
    QCOMPARE(CleanUpEditor(nullptr).ruleType(), QStringLiteral("cleanup"));
}

void TestAppEditors1::titles()
{
    Fixture<CleanUpEditor, CleanUpRule> f;
    f.editor->setRule(&f.rule, 7);
    QCOMPARE(child<QLabel>(f.editor.get(), "ruleEditorTitle")->text(),
             QStringLiteral("Rule 7: Clean up"));
}

void TestAppEditors1::replaceWritesRule()
{
    Fixture<ReplaceEditor, ReplaceRule> f;
    QSignalSpy edited(f.editor.get(), &RuleEditor::ruleEdited);
    QTest::keyClicks(child<QLineEdit>(f.editor.get(), "find"), QStringLiteral("IMG_"));
    QCOMPARE(edited.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(edited.count(), 1, 1000);
    QCOMPARE(f.rule.find, QStringLiteral("IMG_"));
    QTest::qWait(200);
    QCOMPARE(edited.count(), 1);

    QTest::keyClicks(child<QLineEdit>(f.editor.get(), "replace"), QStringLiteral("x"));
    QTRY_COMPARE_WITH_TIMEOUT(edited.count(), 2, 1000);
    QCOMPARE(f.rule.replace, QStringLiteral("x"));

    selectData(child<QComboBox>(f.editor.get(), "occurrence"), int(ReplaceRule::Occurrence::Last));
    QCOMPARE(edited.count(), 3); // at once
    QCOMPARE(f.rule.occurrence, ReplaceRule::Occurrence::Last);
    child<QCheckBox>(f.editor.get(), "matchCase")->click();
    QCOMPARE(f.rule.caseSensitive, false);
    child<QCheckBox>(f.editor.get(), "wholeWord")->click();
    QCOMPARE(f.rule.wholeWord, true);
    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Wildcard));
    QCOMPARE(f.rule.mode, ReplaceRule::Mode::Wildcard);
    QCOMPARE(edited.count(), 6);
}

void TestAppEditors1::replaceDefaults()
{
    Fixture<ReplaceEditor, ReplaceRule> f;
    QCOMPARE(child<QComboBox>(f.editor.get(), "mode")->currentData().toInt(),
             int(ReplaceRule::Mode::Literal));
    QCOMPARE(child<QComboBox>(f.editor.get(), "occurrence")->currentData().toInt(),
             int(ReplaceRule::Occurrence::All));
    QVERIFY(child<QCheckBox>(f.editor.get(), "matchCase")->isChecked());
    QVERIFY(!child<QCheckBox>(f.editor.get(), "wholeWord")->isChecked());
    QCOMPARE(child<QComboBox>(f.editor.get(), "applyTo")->currentData().toString(),
             QStringLiteral("stem"));
    QStringList modes;
    auto *mode = child<QComboBox>(f.editor.get(), "mode");
    for (int i = 0; i < mode->count(); ++i)
        modes.append(mode->itemText(i));
    QCOMPARE(modes, (QStringList{"Literal", "Wildcard", "Regular expression"}));
}

void TestAppEditors1::replaceLoadsRule()
{
    Fixture<ReplaceEditor, ReplaceRule> f;
    ReplaceRule other;
    other.find = QStringLiteral("a");
    other.replace = QStringLiteral("b");
    other.mode = ReplaceRule::Mode::Regex;
    other.occurrence = ReplaceRule::Occurrence::First;
    other.caseSensitive = false;
    other.target = Target::Name;
    f.editor->setRule(&other, 2);
    QCOMPARE(child<QLineEdit>(f.editor.get(), "find")->text(), QStringLiteral("a"));
    QCOMPARE(child<QLineEdit>(f.editor.get(), "replace")->text(), QStringLiteral("b"));
    QCOMPARE(child<QComboBox>(f.editor.get(), "mode")->currentData().toInt(),
             int(ReplaceRule::Mode::Regex));
    QCOMPARE(child<QComboBox>(f.editor.get(), "occurrence")->currentData().toInt(),
             int(ReplaceRule::Occurrence::First));
    QVERIFY(!child<QCheckBox>(f.editor.get(), "matchCase")->isChecked());
    QCOMPARE(child<QComboBox>(f.editor.get(), "applyTo")->currentData().toString(),
             QStringLiteral("name"));
    QCOMPARE(f.rule.find, QString()); // the first rule did not change
}

void TestAppEditors1::replaceWholeWordOnlyLiteral()
{
    Fixture<ReplaceEditor, ReplaceRule> f;
    auto *wholeWord = child<QCheckBox>(f.editor.get(), "wholeWord");
    QVERIFY(rowVisible(f.editor.get(), wholeWord));
    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Regex));
    QVERIFY(!rowVisible(f.editor.get(), wholeWord));
    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Wildcard));
    QVERIFY(!rowVisible(f.editor.get(), wholeWord));
    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Literal));
    QVERIFY(rowVisible(f.editor.get(), wholeWord));
}

void TestAppEditors1::replaceHints()
{
    Fixture<ReplaceEditor, ReplaceRule> f;
    auto *hint = child<QLabel>(f.editor.get(), "hint");
    QVERIFY(!rowVisible(f.editor.get(), hint));
    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Wildcard));
    QVERIFY(rowVisible(f.editor.get(), hint));
    QVERIFY(hint->text().contains(QStringLiteral("= any text")));
    // The hint row is directly under Find.
    QCOMPARE(rowOf(f.editor->form(), hint),
             rowOf(f.editor->form(), child<QLineEdit>(f.editor.get(), "find")) + 1);

    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Regex));
    QVERIFY(hint->text().contains(QStringLiteral("$0 for the full match")));
    QSignalSpy help(f.editor.get(), &ReplaceEditor::syntaxHelpRequested);
    emit hint->linkActivated(QStringLiteral("help"));
    QCOMPARE(help.count(), 1);
}

void TestAppEditors1::replaceRegexError()
{
    Fixture<ReplaceEditor, ReplaceRule> f;
    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Regex));
    QTest::keyClicks(child<QLineEdit>(f.editor.get(), "find"), QStringLiteral("("));
    auto *error = child<QLabel>(f.editor.get(), "fieldErrorText");
    QVERIFY(error);
    QVERIFY(rowVisible(f.editor.get(), error));
    QVERIFY2(error->text().endsWith(QStringLiteral(" at position 1.")), qPrintable(error->text()));
    QVERIFY(error->text().at(0).isUpper());
    // The error row is directly under Find, above the hint.
    QCOMPARE(rowOf(f.editor->form(), error),
             rowOf(f.editor->form(), child<QLineEdit>(f.editor.get(), "find")) + 1);

    QTest::keyClicks(child<QLineEdit>(f.editor.get(), "find"), QStringLiteral(")"));
    QVERIFY(!rowVisible(f.editor.get(), error));
    QTest::keyClick(child<QLineEdit>(f.editor.get(), "find"), Qt::Key_Backspace);
    QVERIFY(rowVisible(f.editor.get(), error));
    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Literal));
    QVERIFY(!rowVisible(f.editor.get(), error));
}

void TestAppEditors1::replaceMonospaceInRegex()
{
    Fixture<ReplaceEditor, ReplaceRule> f;
    const QString fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    auto *find = child<QLineEdit>(f.editor.get(), "find");
    auto *replace = child<QLineEdit>(f.editor.get(), "replace");
    QVERIFY(find->font().family() != fixed);
    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Regex));
    QCOMPARE(find->font().family(), fixed);
    QCOMPARE(replace->font().family(), fixed);
    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Wildcard));
    QCOMPARE(find->font().family(), fixed);
    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Literal));
    QVERIFY(find->font().family() != fixed);
    QCOMPARE(find->font(), f.editor->font());
    // The pattern font has the size of the editor font, also after a font change.
    selectData(child<QComboBox>(f.editor.get(), "mode"), int(ReplaceRule::Mode::Regex));
    QFont big = f.editor->font();
    big.setPointSizeF(22);
    f.editor->setFont(big);
    QCOMPARE(find->font().pointSizeF(), 22.0);
}

void TestAppEditors1::insertIndexEnabled()
{
    Fixture<InsertEditor, InsertRule> f;
    auto *index = child<QSpinBox>(f.editor.get(), "index");
    auto *fromEnd = child<QCheckBox>(f.editor.get(), "fromEnd");
    QVERIFY(child<QRadioButton>(f.editor.get(), "prefix")->isChecked());
    QVERIFY(!index->isEnabled());
    QVERIFY(!fromEnd->isEnabled());
    QCOMPARE(index->minimum(), 0);
    child<QRadioButton>(f.editor.get(), "atIndex")->click();
    QVERIFY(index->isEnabled());
    QVERIFY(fromEnd->isEnabled());
    child<QRadioButton>(f.editor.get(), "suffix")->click();
    QVERIFY(!index->isEnabled());
    // The index field is on the line of its radio button.
    QCOMPARE(rowOf(f.editor->form(), index),
             rowOf(f.editor->form(), child<QRadioButton>(f.editor.get(), "atIndex")));
}

void TestAppEditors1::insertWritesRule()
{
    Fixture<InsertEditor, InsertRule> f;
    QSignalSpy edited(f.editor.get(), &RuleEditor::ruleEdited);
    QTest::keyClicks(child<QLineEdit>(f.editor.get(), "text"), QStringLiteral("Trip_"));
    QTRY_COMPARE_WITH_TIMEOUT(edited.count(), 1, 1000);
    QCOMPARE(f.rule.text, QStringLiteral("Trip_"));
    child<QRadioButton>(f.editor.get(), "atIndex")->click();
    QCOMPARE(f.rule.position, InsertRule::Position::Index);
    child<QSpinBox>(f.editor.get(), "index")->setValue(3);
    QCOMPARE(f.rule.index, 3);
    child<QCheckBox>(f.editor.get(), "fromEnd")->click();
    QVERIFY(f.rule.fromEnd);
    QCOMPARE(edited.count(), 4);

    InsertRule loaded;
    loaded.position = InsertRule::Position::Suffix;
    f.editor->setRule(&loaded, 1);
    QVERIFY(child<QRadioButton>(f.editor.get(), "suffix")->isChecked());
    QCOMPARE(child<QSpinBox>(f.editor.get(), "index")->value(), 0);
}

void TestAppEditors1::removeModeRows()
{
    Fixture<RemoveEditor, RemoveRule> f;
    RemoveEditor *e = f.editor.get();
    auto *from = child<QSpinBox>(e, "from");
    auto *count = child<QSpinBox>(e, "count");
    auto *digits = child<QCheckBox>(e, "digits");
    auto *open = child<QLineEdit>(e, "open");
    auto *keep = child<QCheckBox>(e, "keepDelimiters");
    auto *applyTo = child<QComboBox>(e, "applyTo");
    QVERIFY(rowVisible(e, from) && rowVisible(e, count));
    QVERIFY(!rowVisible(e, digits) && !rowVisible(e, open) && !rowVisible(e, keep));
    QVERIFY(QTest::qWaitForWindowExposed(e));
    const int edge = ctrm::test::visualLeft(applyTo, e);

    selectData(child<QComboBox>(e, "mode"), int(RemoveRule::Mode::Characters));
    QVERIFY(!rowVisible(e, from) && !rowVisible(e, count));
    QVERIFY(rowVisible(e, digits) && !rowVisible(e, open));
    QVERIFY(rowVisible(e, applyTo));
    QApplication::processEvents();
    QCOMPARE(ctrm::test::visualLeft(applyTo, e), edge);
    QVERIFY(oneFieldEdge(e));

    selectData(child<QComboBox>(e, "mode"), int(RemoveRule::Mode::Between));
    QVERIFY(!rowVisible(e, digits) && rowVisible(e, open) && rowVisible(e, keep));
    QApplication::processEvents();
    QVERIFY(oneFieldEdge(e));
}

void TestAppEditors1::removeWritesRule()
{
    Fixture<RemoveEditor, RemoveRule> f;
    RemoveEditor *e = f.editor.get();
    QSignalSpy edited(e, &RuleEditor::ruleEdited);
    child<QSpinBox>(e, "from")->setValue(2);
    child<QSpinBox>(e, "count")->setValue(5);
    child<QCheckBox>(e, "rangeFromEnd")->click();
    QCOMPARE(f.rule.from, 2);
    QCOMPARE(f.rule.count, 5);
    QVERIFY(f.rule.fromEnd);

    selectData(child<QComboBox>(e, "mode"), int(RemoveRule::Mode::Characters));
    QCOMPARE(f.rule.mode, RemoveRule::Mode::Characters);
    child<QCheckBox>(e, "digits")->click();
    child<QCheckBox>(e, "nonAscii")->click();
    QCOMPARE(f.rule.classes, quint32(RemoveRule::Digits | RemoveRule::NonAscii));
    auto *customText = child<QLineEdit>(e, "customText");
    QVERIFY(!customText->isEnabled());
    child<QCheckBox>(e, "custom")->click();
    QVERIFY(customText->isEnabled());
    QCOMPARE(f.rule.classes,
             quint32(RemoveRule::Digits | RemoveRule::NonAscii | RemoveRule::Custom));
    QTest::keyClicks(customText, QStringLiteral("#"));
    QTRY_COMPARE_WITH_TIMEOUT(f.rule.custom, QStringLiteral("#"), 1000);

    selectData(child<QComboBox>(e, "mode"), int(RemoveRule::Mode::Between));
    child<QLineEdit>(e, "open")->clear();
    QTest::keyClicks(child<QLineEdit>(e, "open"), QStringLiteral("["));
    QCOMPARE(f.rule.open, QStringLiteral("["));
    selectData(child<QComboBox>(e, "betweenOccurrence"), int(RemoveRule::Occurrence::First));
    QCOMPARE(f.rule.occurrence, RemoveRule::Occurrence::First);
    child<QCheckBox>(e, "keepDelimiters")->click();
    QVERIFY(f.rule.keepDelimiters);
    QVERIFY(edited.count() >= 9);

    RemoveRule loaded;
    loaded.mode = RemoveRule::Mode::Characters;
    loaded.classes = RemoveRule::Letters;
    f.editor->setRule(&loaded, 1);
    QVERIFY(child<QCheckBox>(e, "letters")->isChecked());
    QVERIFY(!child<QCheckBox>(e, "digits")->isChecked());
    QVERIFY(rowVisible(e, child<QCheckBox>(e, "letters")));
    QCOMPARE(child<QLineEdit>(e, "open")->text(), QStringLiteral("("));
}

void TestAppEditors1::cleanupWritesRule()
{
    Fixture<CleanUpEditor, CleanUpRule> f;
    CleanUpEditor *e = f.editor.get();
    QVERIFY(child<QCheckBox>(e, "trim")->isChecked());
    QVERIFY(child<QCheckBox>(e, "collapseSpaces")->isChecked());
    QVERIFY(!child<QCheckBox>(e, "replaceInvalid")->isChecked());
    QVERIFY(!child<QLineEdit>(e, "replacement")->isEnabled());
    QCOMPARE(child<QLineEdit>(e, "replacement")->text(), QStringLiteral("_"));
    QStringList spaces;
    auto *combo = child<QComboBox>(e, "spacesTo");
    for (int i = 0; i < combo->count(); ++i)
        spaces.append(combo->itemText(i));
    QCOMPARE(spaces, (QStringList{"Keep", "Change to _", "Change to -", "Change to .", "Remove"}));

    child<QCheckBox>(e, "trim")->click();
    child<QCheckBox>(e, "collapseSpaces")->click();
    selectData(combo, int(CleanUpRule::Spaces::Dash));
    child<QCheckBox>(e, "replaceInvalid")->click();
    QVERIFY(child<QLineEdit>(e, "replacement")->isEnabled());
    QVERIFY(!f.rule.trim);
    QVERIFY(!f.rule.collapseSpaces);
    QCOMPARE(f.rule.spacesTo, CleanUpRule::Spaces::Dash);
    QVERIFY(f.rule.replaceInvalid);
    child<QLineEdit>(e, "replacement")->clear();
    QTest::keyClicks(child<QLineEdit>(e, "replacement"), QStringLiteral("-"));
    QCOMPARE(f.rule.replacement, QStringLiteral("-"));
}

void TestAppEditors1::cleanupNoteFollowsPortable()
{
    Fixture<CleanUpEditor, CleanUpRule> f;
    auto *note = child<QLabel>(f.editor.get(), "portableNote");
    QCOMPARE(note->text(), QStringLiteral("Uses the Windows rules (portable names is on)."));
    f.editor->setPortableNames(false);
    QCOMPARE(note->text(), QStringLiteral("Uses the rules of this system."));
    f.editor->setPortableNames(true);
    QCOMPARE(note->text(), QStringLiteral("Uses the Windows rules (portable names is on)."));
    // The note is dimmed and in the caption style.
    QCOMPARE(note->palette().color(QPalette::WindowText),
             f.editor->palette().color(QPalette::PlaceholderText));
    QCOMPARE(note->font(), typeStyle(TypeToken::Caption, f.editor->font()));
    QFont big = f.editor->font();
    big.setPointSizeF(24);
    f.editor->setFont(big);
    QCOMPARE(note->font(), typeStyle(TypeToken::Caption, big));
}

void TestAppEditors1::applyToWritesTarget()
{
    Fixture<ReplaceEditor, ReplaceRule> replace;
    Fixture<InsertEditor, InsertRule> insert;
    Fixture<RemoveEditor, RemoveRule> remove;
    Fixture<CleanUpEditor, CleanUpRule> cleanup;
    const QList<std::pair<RuleEditor *, Rule *>> pages{{replace.editor.get(), &replace.rule},
                                                       {insert.editor.get(), &insert.rule},
                                                       {remove.editor.get(), &remove.rule},
                                                       {cleanup.editor.get(), &cleanup.rule}};
    for (const auto &[editor, rule] : pages) {
        QSignalSpy edited(editor, &RuleEditor::ruleEdited);
        auto *applyTo = child<QComboBox>(editor, "applyTo");
        selectData(applyTo, QStringLiteral("extension"));
        QCOMPARE(rule->target, Target::Extension);
        QCOMPARE(edited.count(), 1);
        // Apply to is the last row (no More options in these rules).
        QCOMPARE(rowOf(editor->form(), applyTo), editor->form()->rowCount() - 1);
    }
}

void TestAppEditors1::setRuleFlushesPendingEdit()
{
    Fixture<ReplaceEditor, ReplaceRule> f;
    QSignalSpy edited(f.editor.get(), &RuleEditor::ruleEdited);
    ReplaceRule other;
    other.mode = ReplaceRule::Mode::Regex;
    other.target = Target::Name;

    // No pending edit: loading a rule emits nothing.
    f.editor->setRule(&other, 2);
    f.editor->setRule(&f.rule, 1);
    QCOMPARE(edited.count(), 0);

    // A pending edit of the old rule is emitted at once, then nothing for the new rule.
    QTest::keyClicks(child<QLineEdit>(f.editor.get(), "find"), QStringLiteral("a"));
    QCOMPARE(edited.count(), 0);
    f.editor->setRule(&other, 2);
    QCOMPARE(edited.count(), 1);
    QTest::qWait(250);
    QCOMPARE(edited.count(), 1);
    QCOMPARE(f.rule.find, QStringLiteral("a"));
    QCOMPARE(other.find, QString());
    QCOMPARE(other.mode, ReplaceRule::Mode::Regex);
}

void TestAppEditors1::wrongRuleTypeDisables()
{
    Fixture<InsertEditor, InsertRule> f;
    QVERIFY(f.editor->isEnabled());
    ReplaceRule wrong;
    f.editor->setRule(&wrong, 1);
    QVERIFY(!f.editor->isEnabled());
    QTest::keyClicks(child<QLineEdit>(f.editor.get(), "text"), QStringLiteral("x"));
    QCOMPARE(f.rule.text, QString());
    f.editor->setRule(&f.rule, 1);
    QVERIFY(f.editor->isEnabled());
}

void TestAppEditors1::fieldEdgeAllEditors()
{
    Fixture<ReplaceEditor, ReplaceRule> replace;
    Fixture<InsertEditor, InsertRule> insert;
    Fixture<RemoveEditor, RemoveRule> remove;
    Fixture<CleanUpEditor, CleanUpRule> cleanup;
    for (RuleEditor *editor : QList<RuleEditor *>{replace.editor.get(), insert.editor.get(),
                                                  remove.editor.get(), cleanup.editor.get()}) {
        QVERIFY(QTest::qWaitForWindowExposed(editor));
        QVERIFY2(oneFieldEdge(editor), qPrintable(editor->ruleType()));
        QFont big = editor->font();
        big.setPointSizeF(26);
        editor->setFont(big);
        QApplication::processEvents();
        QVERIFY2(oneFieldEdge(editor), qPrintable(editor->ruleType() + " at 26 pt"));
    }
}

QTEST_MAIN(TestAppEditors1)
void TestAppEditors1::cleanupReplacementFits()
{
    // D8, O2: the field is for a few characters and is on its own row under the check box, at
    // the field edge (ui-design 6.1 rules 2 and 7).
    Fixture<CleanUpEditor, CleanUpRule> f;
    QVERIFY(QTest::qWaitForWindowExposed(f.editor.get()));
    auto *box = child<QCheckBox>(f.editor.get(), "replaceInvalid");
    auto *field = child<QLineEdit>(f.editor.get(), "replacement");
    QCOMPARE(field->minimumWidth(), field->maximumWidth());
    QVERIFY2(field->width() < box->sizeHint().width() / 3,
             qPrintable(QString::number(field->width())));
    const QPoint boxAt = box->mapTo(f.editor.get(), QPoint());
    const QPoint fieldAt = field->mapTo(f.editor.get(), QPoint());
    QVERIFY(fieldAt.y() >= boxAt.y() + box->height());
    // The layout items, not the widgets: the macOS style moves a widget inside its item.
    QFormLayout *form = f.editor->form();
    const int boxRow = rowOf(form, box);
    const int fieldRow = rowOf(form, field);
    QVERIFY(boxRow >= 0 && fieldRow == boxRow + 1);
    QCOMPARE(form->itemAt(fieldRow, QFormLayout::FieldRole)->geometry().x(),
             form->itemAt(boxRow, QFormLayout::FieldRole)->geometry().x());
    QCOMPARE(form->itemAt(fieldRow, QFormLayout::FieldRole)->widget(), field);
    QVERIFY(fieldAt.x() + field->width() <= boxAt.x() + box->sizeHint().width());
}

#include "tst_app_editors1.moc"
