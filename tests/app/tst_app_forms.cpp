// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/ruleeditor.h"
#include "widgets/formutils.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QToolButton>
#include <QtTest>

#include <memory>

using namespace ctrm::app;

namespace {

// The "Case:" form of ui-design 6.3, on a style.
struct CaseForm
{
    std::unique_ptr<QWidget> window = std::make_unique<QWidget>();
    QFormLayout *form = nullptr;
    QLabel *caseLabel = nullptr;
    QList<QRadioButton *> radios;
    QLineEdit *text = nullptr;
    QComboBox *applyTo = nullptr;
    QCheckBox *check = nullptr;
    MoreOptions *more = nullptr;

    explicit CaseForm(QStyle *style = nullptr)
    {
        if (style)
            window->setStyle(style);
        form = makeForm(window.get());
        QList<QWidget *> controls;
        for (const char *name :
             {"lower case", "UPPER CASE", "Title Case", "Sentence case", "iNVERT cASE"}) {
            auto *radio = new QRadioButton(QString::fromLatin1(name));
            if (style)
                radio->setStyle(style);
            radios.append(radio);
            controls.append(radio);
        }
        QWidget *group = makeFieldGroup(controls);
        caseLabel = new QLabel(QStringLiteral("Case:"));
        form->addRow(caseLabel, group);
        alignLabelToFirstRow(caseLabel, radios.first());
        text = new QLineEdit;
        form->addRow(QStringLiteral("Text:"), text);
        applyTo = makeApplyToCombo(window.get());
        form->addRow(QStringLiteral("Apply to:"), applyTo);
        check = new QCheckBox(QStringLiteral("Skip folders"));
        form->addRow(QString(), check);
        more = new MoreOptions(form, QStringLiteral("More options"), window.get());
        window->resize(500, 400);
        window->show();
    }

    int labelBaseline() const
    {
        const QFontMetrics fm(caseLabel->font());
        return caseLabel->mapTo(window.get(), QPoint(0, 0)).y() +
               (caseLabel->height() - fm.height()) / 2 + fm.ascent();
    }

    int radioBaseline() const
    {
        QRadioButton *radio = radios.first();
        QStyleOptionButton opt;
        opt.initFrom(radio);
        opt.text = radio->text();
        const QRect r = radio->style()->subElementRect(QStyle::SE_RadioButtonContents, &opt, radio);
        const QFontMetrics fm(radio->font());
        return radio->mapTo(window.get(), QPoint(0, 0)).y() + r.center().y() - fm.height() / 2 +
               fm.ascent();
    }

    QList<int> fieldEdges() const
    {
        QList<int> edges;
        for (QWidget *w : QList<QWidget *>{radios.first(), text, applyTo, check, more->button()})
            edges.append(w->mapTo(window.get(), QPoint(0, 0)).x());
        return edges;
    }
};

bool allEqual(const QList<int> &values)
{
    for (const int v : values) {
        if (v != values.first())
            return false;
    }
    return true;
}

QString join(const QList<int> &values)
{
    QStringList parts;
    for (const int v : values)
        parts.append(QString::number(v));
    return parts.join(QLatin1Char(' '));
}

class TextEditor : public RuleEditor
{
public:
    explicit TextEditor(QWidget *parent = nullptr)
        : RuleEditor(parent)
        , field(new QLineEdit)
        , check(new QCheckBox(QStringLiteral("Option")))
    {
        form()->addRow(QStringLiteral("Find:"), field);
        form()->addRow(QString(), check);
        watchTextField(field);
        connect(check, &QCheckBox::toggled, this, [this] { emitEdited(); });
    }
    QString ruleType() const override { return QStringLiteral("replace"); }
    void setRule(ctrm::Rule *, int position) override
    {
        cancelPendingEdit();
        setTitle(position, QStringLiteral("Replace"));
        field->setText(QStringLiteral("loaded"));
    }

    QLineEdit *field;
    QCheckBox *check;
};

} // namespace

class TestAppForms : public QObject
{
    Q_OBJECT
private slots:
    void baselineMatches_data();
    void baselineMatches();
    void baselineAfterFontChange();
    void oneFieldEdge();
    void fieldEdgeAtLargeFont();
    void labelColumnSync();
    void moreOptionsToggles();
    void applyToComboValues();
    void editedDebounce();
    void editedAtOnce();
    void setRuleNoEdit();
    void moreOptionsAfterInsert();
    void editorTitle();
    void fieldError();
    void labelsTopAligned();
    void inlineField();
};

void TestAppForms::baselineMatches_data()
{
    QTest::addColumn<QString>("styleName");
    for (const QString &key : QStyleFactory::keys())
        QTest::newRow(qPrintable(key)) << key;
}

void TestAppForms::baselineMatches()
{
    QFETCH(QString, styleName);
    const std::unique_ptr<QStyle> style(QStyleFactory::create(styleName));
    QVERIFY(style);
    CaseForm f(style.get());
    QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
    const int diff = std::abs(f.labelBaseline() - f.radioBaseline());
    QVERIFY2(diff <= 1, qPrintable(QStringLiteral("label %1, radio %2")
                                           .arg(f.labelBaseline())
                                           .arg(f.radioBaseline())));
    f.window.reset(); // before the style
}

void TestAppForms::baselineAfterFontChange()
{
    CaseForm f;
    QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
    QFont font = f.window->font();
    font.setPointSizeF(font.pointSizeF() * 2);
    f.window->setFont(font);
    QCoreApplication::processEvents();
    QTRY_VERIFY2(std::abs(f.labelBaseline() - f.radioBaseline()) <= 1,
                 qPrintable(QStringLiteral("label %1, radio %2")
                                    .arg(f.labelBaseline())
                                    .arg(f.radioBaseline())));
    QVERIFY(f.caseLabel->minimumHeight() >= f.radios.first()->sizeHint().height());
}

void TestAppForms::oneFieldEdge()
{
    CaseForm f;
    f.more->setOpen(true);
    QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
    const QList<int> edges = f.fieldEdges();
    QVERIFY2(allEqual(edges), qPrintable(join(edges)));
}

void TestAppForms::fieldEdgeAtLargeFont()
{
    CaseForm f;
    QFont font = f.window->font();
    font.setPointSizeF(26.0);
    f.window->setFont(font);
    QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
    QCoreApplication::processEvents();
    const QList<int> edges = f.fieldEdges();
    QVERIFY2(allEqual(edges), qPrintable(join(edges)));
}

void TestAppForms::labelColumnSync()
{
    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *page1 = new QWidget;
    auto *page2 = new QWidget;
    layout->addWidget(page1);
    layout->addWidget(page2);
    QFormLayout *form1 = makeForm(page1);
    QFormLayout *form2 = makeForm(page2);
    auto *edit1 = new QLineEdit;
    auto *edit2 = new QLineEdit;
    form1->addRow(QStringLiteral("Text:"), edit1);
    form2->addRow(QStringLiteral("A much longer label text:"), edit2);

    LabelColumnSync sync;
    sync.addForm(form1);
    sync.addForm(form2);
    sync.sync();
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QWidget *label1 = form1->itemAt(0, QFormLayout::LabelRole)->widget();
    QWidget *label2 = form2->itemAt(0, QFormLayout::LabelRole)->widget();
    QCOMPARE(label1->width(), label2->width());
    QCOMPARE(edit1->mapTo(&window, QPoint(0, 0)).x(), edit2->mapTo(&window, QPoint(0, 0)).x());
}

void TestAppForms::moreOptionsToggles()
{
    QWidget window;
    QFormLayout *form = makeForm(&window);
    form->addRow(QStringLiteral("One:"), new QLineEdit);
    form->addRow(QStringLiteral("Two:"), new QLineEdit);
    MoreOptions more(form, QStringLiteral("More options"), &window);
    QCOMPARE(form->rowCount(), 3); // the button row
    form->addRow(QStringLiteral("Three:"), new QLineEdit);
    form->addRow(QStringLiteral("Four:"), new QLineEdit);
    more.addRow(3);
    more.addRow(4);
    QVERIFY(!form->isRowVisible(3));
    QVERIFY(!form->isRowVisible(4));
    QVERIFY(form->isRowVisible(2));
    QVERIFY(!more.button()->isChecked());
    QCOMPARE(more.button()->arrowType(), Qt::RightArrow);

    more.setOpen(true);
    QVERIFY(form->isRowVisible(3));
    QVERIFY(form->isRowVisible(4));
    QVERIFY(more.button()->isChecked());
    QCOMPARE(more.button()->arrowType(), Qt::DownArrow);

    more.button()->click();
    QVERIFY(!form->isRowVisible(3));
    QCOMPARE(more.button()->text(), QStringLiteral("More options"));
}

void TestAppForms::applyToComboValues()
{
    QWidget window;
    QComboBox *combo = makeApplyToCombo(&window);
    QCOMPARE(combo->parent(), &window);
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->itemText(0), QStringLiteral("Name only"));
    QCOMPARE(combo->itemText(1), QStringLiteral("Extension only"));
    QCOMPARE(combo->itemText(2), QStringLiteral("Name and extension"));
    QCOMPARE(combo->itemData(0).toString(), QStringLiteral("stem"));
    QCOMPARE(combo->itemData(1).toString(), QStringLiteral("extension"));
    QCOMPARE(combo->itemData(2).toString(), QStringLiteral("name"));
    QCOMPARE(combo->currentIndex(), 0);
}

void TestAppForms::editedDebounce()
{
    TextEditor editor;
    QSignalSpy spy(&editor, &RuleEditor::ruleEdited);
    QElapsedTimer clock;
    qint64 firstAt = -1;
    connect(&editor, &RuleEditor::ruleEdited, this, [&] {
        if (firstAt < 0)
            firstAt = clock.elapsed();
    });
    clock.start();
    QTest::keyClicks(editor.field, QStringLiteral("abc"));
    QCOMPARE(spy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 300);
    QVERIFY2(firstAt >= 140, qPrintable(QString::number(firstAt)));
    QTest::qWait(200);
    QCOMPARE(spy.count(), 1);
}

void TestAppForms::editedAtOnce()
{
    TextEditor editor;
    QSignalSpy spy(&editor, &RuleEditor::ruleEdited);
    QTest::keyClicks(editor.field, QStringLiteral("a"));
    editor.check->setChecked(true);
    // The pending text edit and the check box give one signal at once.
    QCOMPARE(spy.count(), 1);
    QTest::qWait(250);
    QCOMPARE(spy.count(), 1);
}

void TestAppForms::setRuleNoEdit()
{
    TextEditor editor;
    QSignalSpy spy(&editor, &RuleEditor::ruleEdited);
    QTest::keyClicks(editor.field, QStringLiteral("a"));
    editor.setRule(nullptr, 1); // a pending edit is dropped; setText gives no edit
    QTest::qWait(300);
    QCOMPARE(spy.count(), 0);
}

void TestAppForms::moreOptionsAfterInsert()
{
    TextEditor editor;
    QFormLayout *form = editor.form();
    MoreOptions more(form, QStringLiteral("More options"), &editor); // row 2
    auto *extra = new QLineEdit;
    form->addRow(QStringLiteral("Extra:"), extra); // row 3
    more.addRow(3);
    QVERIFY(!form->isRowVisible(extra));

    // An error row under row 0 moves the extra row to row 4.
    editor.showFieldError(0, QStringLiteral("Bad pattern."));
    more.setOpen(true);
    QVERIFY(form->isRowVisible(extra));
    QVERIFY(form->isRowVisible(1));
    more.setOpen(false);
    QVERIFY(!form->isRowVisible(extra));
    QVERIFY(form->isRowVisible(1));

    // No error row under a hidden field.
    editor.showFieldError(4, QStringLiteral("Bad value."));
    QVERIFY(!form->isRowVisible(5));
}

void TestAppForms::editorTitle()
{
    TextEditor editor;
    editor.setRule(nullptr, 2);
    auto *title = editor.findChild<QLabel *>(QStringLiteral("ruleEditorTitle"));
    QVERIFY(title);
    QCOMPARE(title->text(), QStringLiteral("Rule 2: Replace"));
    QCOMPARE(title->font().weight(), QFont::DemiBold);
    QVERIFY(qFuzzyCompare(title->font().pointSizeF(), editor.font().pointSizeF() * 1.15));
    QCOMPARE(editor.ruleType(), QStringLiteral("replace"));
    QCOMPARE(editor.form()->rowCount(), 2);
}

void TestAppForms::fieldError()
{
    TextEditor editor;
    editor.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));
    QFormLayout *form = editor.form();
    editor.showFieldError(0, QStringLiteral("The pattern is not correct."));
    QCoreApplication::processEvents();
    QCOMPARE(form->rowCount(), 3);
    QVERIFY(form->isRowVisible(1));
    QWidget *errorRow = form->itemAt(1, QFormLayout::FieldRole)->widget();
    QVERIFY(errorRow);
    auto *label = errorRow->findChild<QLabel *>(QStringLiteral("fieldErrorText"));
    QVERIFY(label);
    QCOMPARE(label->text(), QStringLiteral("The pattern is not correct."));
    // The error is in the field column, on the field edge.
    QCOMPARE(errorRow->mapTo(&editor, QPoint(0, 0)).x(),
             editor.field->mapTo(&editor, QPoint(0, 0)).x());

    editor.showFieldError(0, QStringLiteral("Other text."));
    QCOMPARE(form->rowCount(), 3);
    QCOMPARE(label->text(), QStringLiteral("Other text."));
    editor.showFieldError(0, QString());
    QVERIFY(!form->isRowVisible(1));
}

void TestAppForms::labelsTopAligned()
{
    QWidget window;
    QFormLayout *form = makeForm(&window);
    QVERIFY(form->labelAlignment() & Qt::AlignTop);
    const auto horizontal = Qt::Alignment(window.style()->styleHint(
                                    QStyle::SH_FormLayoutLabelAlignment, nullptr, &window)) &
                            Qt::AlignHorizontal_Mask;
    QCOMPARE(form->labelAlignment() & Qt::AlignHorizontal_Mask, horizontal);
    QCOMPARE(form->fieldGrowthPolicy(), QFormLayout::ExpandingFieldsGrow);
    QCOMPARE(window.layout(), form);
}

void TestAppForms::inlineField()
{
    auto *radio = new QRadioButton(QStringLiteral("At index"));
    auto *spin = new QLineEdit;
    std::unique_ptr<QWidget> field(makeInlineField(radio, {spin}));
    QCOMPARE(field->layout()->contentsMargins(), QMargins());
    QCOMPARE(radio->parent(), field.get());
    QCOMPARE(spin->parent(), field.get());
    std::unique_ptr<QWidget> group(makeFieldGroup({new QCheckBox, new QCheckBox}));
    QCOMPARE(group->layout()->contentsMargins(), QMargins());
    QVERIFY(qobject_cast<QVBoxLayout *>(group->layout()));
}

QTEST_MAIN(TestAppForms)
#include "tst_app_forms.moc"
