// SPDX-License-Identifier: GPL-3.0-or-later
#include "layoutedge.h"
#include "rules/chainoptionswidget.h"
#include "rules/ruleeditor.h"
#include "theme/ctrmstyle.h"
#include "theme/tokens.h"
#include "widgets/formutils.h"
#include "widgets/itemviewpaint.h"
#include "widgets/tertiarybutton.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProxyStyle>
#include <QRadioButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QToolButton>
#include <QVBoxLayout>
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
        QLayout *group = makeFieldGroup(controls);
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
            edges.append(ctrm::test::visualLeft(w, window.get()));
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
    void formStartsAtLeft();
    void syncedLabelTextAlignment();
    void inlineSubFieldsAligned();
    void tertiaryButtonFrame();
    void itemCheckAtOrigin();
    void toggleBoxOnFieldEdge();
    void cocoaPaintedEdges();
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
    // The label is as high as the visible part of the radio (without the margins of the style).
    QVERIFY(f.caseLabel->minimumHeight() >= QWidgetItem(f.radios.first()).sizeHint().height());
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
    QCOMPARE(ctrm::test::visualLeft(edit1, &window), ctrm::test::visualLeft(edit2, &window));
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
    // LabelStrong: not larger than the "Rules" header of the panel (C14).
    QVERIFY(qFuzzyCompare(title->font().pointSizeF(), editor.font().pointSizeF()));
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
    QCOMPARE(ctrm::test::visualLeft(errorRow, &editor),
             ctrm::test::visualLeft(editor.field, &editor));

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
    QWidget holder;
    std::unique_ptr<QLayout> group(
            makeFieldGroup({new QCheckBox(&holder), new QCheckBox(&holder)}));
    QCOMPARE(group->contentsMargins(), QMargins());
    QVERIFY(qobject_cast<QGridLayout *>(group.get()));
}

void TestAppForms::formStartsAtLeft()
{
    // D1: no field grows, and the form must not be centered.
    QWidget window;
    QFormLayout *form = makeForm(&window);
    QCOMPARE(form->formAlignment(), Qt::AlignLeft | Qt::AlignTop);
    auto *check = new QCheckBox(QStringLiteral("Skip"));
    form->addRow(QStringLiteral("Case:"), check);
    window.resize(600, 200);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QWidget *label = form->itemAt(0, QFormLayout::LabelRole)->widget();
    QVERIFY2(label->x() < 30, qPrintable(QString::number(label->x())));
}

void TestAppForms::syncedLabelTextAlignment()
{
    // D2: a label wider than its text puts the text on the side of the label column.
    QWidget window;
    QFormLayout *form = makeForm(&window);
    form->addRow(QStringLiteral("Text:"), new QLineEdit);
    form->addRow(QStringLiteral("A much longer label:"), new QLineEdit);
    LabelColumnSync sync;
    sync.addForm(form);
    sync.sync();
    for (int row = 0; row < 2; ++row) {
        auto *label = qobject_cast<QLabel *>(form->itemAt(row, QFormLayout::LabelRole)->widget());
        QVERIFY(label);
        QCOMPARE(label->alignment() & Qt::AlignHorizontal_Mask,
                 form->labelAlignment() & Qt::AlignHorizontal_Mask);
    }
}

void TestAppForms::inlineSubFieldsAligned()
{
    // D7: the sub-fields of two inline fields in one group have one start line, and the
    // group has no extra margins.
    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *first = new QRadioButton(QStringLiteral("At index"));
    auto *second = new QRadioButton(QStringLiteral("Before the last"));
    auto *field1 = new QLineEdit;
    auto *field2 = new QLineEdit;
    auto *plain = new QRadioButton(QStringLiteral("At end"));
    QLayout *group = makeFieldGroup(
            {makeInlineField(first, {field1}), makeInlineField(second, {field2}), plain});
    layout->addLayout(group);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(ctrm::test::visualLeft(field1, &window), ctrm::test::visualLeft(field2, &window));
    QCOMPARE(ctrm::test::visualLeft(first, &window), ctrm::test::visualLeft(plain, &window));
    // The window is the parent: no group widget keeps a control inside it.
    QCOMPARE(first->parentWidget(), &window);
    QCOMPARE(field2->parentWidget(), &window);
    QCOMPARE(plain->parentWidget(), &window);
}

void TestAppForms::tertiaryButtonFrame()
{
    // D4: no frame until press or keyboard focus. With the macOS style, the corner of the
    // button has the window colour.
    QWidget window;
    window.setAutoFillBackground(true);
    if (QStyle *mac = QStyleFactory::create(QStringLiteral("macOS")))
        window.setStyle(mac);
    auto *layout = new QHBoxLayout(&window);
    auto *button = new TertiaryButton;
    button->setText(QStringLiteral("Chain options"));
    button->setArrowType(Qt::RightArrow);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setFocusPolicy(Qt::TabFocus);
    auto *other = new QLineEdit;
    layout->addWidget(other);
    layout->addWidget(button);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    other->setFocus();
    QTest::mouseMove(other);
    QVERIFY(button->autoRaise());
    QVERIFY(!button->framed());
    const QImage image = button->grab().toImage();
    const QColor background = window.palette().color(QPalette::Window);
    const QPoint corner(image.width() / image.devicePixelRatio() > 4 ? 2 : 0, 2);
    const QColor pixel = image.pixelColor(corner * image.devicePixelRatio());
    QVERIFY2(qAbs(pixel.lightness() - background.lightness()) <= 2,
             qPrintable(QStringLiteral("%1 vs %2").arg(pixel.name(), background.name())));
    button->setDown(true);
    QVERIFY(button->framed());
    button->setDown(false);
    button->setFocus(Qt::TabFocusReason);
    QVERIFY(button->framed());
}

namespace {

// Records where the item view check indicators are drawn.
class CheckRecorder : public QProxyStyle
{
public:
    struct Draw
    {
        QRect rect;
        QPointF origin;
        bool on = false;
    };
    QList<Draw> checks;
    int itemsWithCheck = 0;

    void drawPrimitive(PrimitiveElement pe, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget) const override
    {
        if (pe == PE_IndicatorItemViewItemCheck)
            const_cast<CheckRecorder *>(this)->checks.append({option->rect,
                                                              painter->transform().map(QPointF()),
                                                              bool(option->state & State_On)});
        QProxyStyle::drawPrimitive(pe, option, painter, widget);
    }

    void drawControl(ControlElement ce, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override
    {
        if (ce == CE_ItemViewItem) {
            const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option);
            if (item && item->features & QStyleOptionViewItem::HasCheckIndicator)
                ++const_cast<CheckRecorder *>(this)->itemsWithCheck;
        }
        QProxyStyle::drawControl(ce, option, painter, widget);
    }
};

} // namespace

void TestAppForms::itemCheckAtOrigin()
{
    // D9: the macOS style draws the check indicator of an item view only in the first row. The
    // indicator is drawn at the painter origin, and the style never gets an item with one.
    auto *recorder = new CheckRecorder;
    QWidget widget;
    widget.setStyle(recorder);
    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    QStyleOptionViewItem option;
    option.initFrom(&widget);
    option.widget = &widget;
    option.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
    option.text = QStringLiteral("x2.txt");
    option.rect = QRect(0, 60, 200, 24);
    option.checkState = Qt::Checked;
    const QRect check =
            recorder->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, &widget);
    paintItemViewItem(&painter, option);
    option.checkState = Qt::Unchecked;
    paintItemViewItem(&painter, option);
    painter.end();

    QCOMPARE(recorder->itemsWithCheck, 0);
    QCOMPARE(recorder->checks.size(), 2);
    for (const CheckRecorder::Draw &d : std::as_const(recorder->checks)) {
        QCOMPARE(d.rect.topLeft(), QPoint(0, 0));
        QCOMPARE(d.rect.size(), check.size());
        QCOMPARE(d.origin, QPointF(check.topLeft()));
    }
    QVERIFY(recorder->checks.at(0).on);
    QVERIFY(!recorder->checks.at(1).on);
}

// The first painted column of w (logical px, relative to window): the first column from the left
// in which a pixel of the middle rows is not the window background.
static int paintedLeft(const QImage &image, const QWidget *w, const QWidget *window)
{
    const QRect r = ctrm::test::visualRect(w, window);
    const qreal dpr = image.devicePixelRatio();
    const QColor bg = window->palette().color(QPalette::Window);
    for (int x = int((r.left() - 8) * dpr); x < int((r.left() + 12) * dpr); ++x) {
        for (int y = int((r.center().y() - 4) * dpr); y <= int((r.center().y() + 4) * dpr); ++y) {
            const QColor c = image.pixelColor(x, y);
            if (qAbs(c.red() - bg.red()) + qAbs(c.green() - bg.green()) +
                        qAbs(c.blue() - bg.blue()) >
                12)
                return int(x / dpr);
        }
    }
    return -1;
}

// The vertical ink centre (logical px, relative to `to`) of the painted pixels in the columns
// [x0, x1) and the rows [y0, y1) of image (logical px). NaN when there is no ink.
static double inkCentre(const QImage &image, const QWidget *to, int x0, int x1, int y0, int y1)
{
    const qreal dpr = image.devicePixelRatio();
    const QColor bg = to->palette().color(QPalette::Window);
    int top = INT_MAX;
    int bottom = -1;
    for (int y = int(y0 * dpr); y < int(y1 * dpr) && y < image.height(); ++y) {
        for (int x = int(x0 * dpr); x < int(x1 * dpr) && x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (qAbs(c.red() - bg.red()) + qAbs(c.green() - bg.green()) +
                        qAbs(c.blue() - bg.blue()) >
                30) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    return bottom < 0 ? qQNaN() : (top + bottom + 1) / 2.0 / dpr;
}

// C04, R5-02: Cocoa draws the box of a check box or a radio button at the left edge of the widget
// and centres the box and the text in the widget height. The form gives these widgets the layout
// rect with no style margins and their own 20 px height, so the widget is the visible rect.
void TestAppForms::toggleBoxOnFieldEdge()
{
    CaseForm f;
    applyFormRhythm(f.form);
    QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
    const int edge = ctrm::test::visualLeft(f.text, f.window.get());
    for (QWidget *w : QList<QWidget *>{f.radios.first(), f.radios.last(), f.check}) {
        const QRect visible = ctrm::test::visualRect(w, f.window.get());
        QCOMPARE(visible.left(), edge);
        QCOMPARE(QRect(w->mapTo(f.window.get(), QPoint()), w->size()), visible);
        QCOMPARE(w->height(), tokens::controlH - tokens::space1);
    }
    // A spanning check box row stays spanning.
    auto *spanning = new QCheckBox(QStringLiteral("Whole row"));
    f.form->addRow(spanning);
    applyFormRhythm(f.form);
    int spanRow = -1;
    QFormLayout::ItemRole spanRole = QFormLayout::FieldRole;
    f.form->getWidgetPosition(spanning, &spanRow, &spanRole);
    QCOMPARE(spanRole, QFormLayout::SpanningRole);
}

// C04 on the real Cocoa painter with CtrmStyle (the app style): the drawn left edges of a radio
// button, a text field, a combo box, a check box and a spin box are on one line. The offscreen
// platform does not draw native controls: run this test without QT_QPA_PLATFORM=offscreen.
void TestAppForms::cocoaPaintedEdges()
{
    if (QGuiApplication::platformName() != QLatin1String("cocoa"))
        QSKIP("needs the native Cocoa painter");
    const QString baseName = QApplication::style()->name();
    QApplication::setStyle(new CtrmStyle(baseName));
    const auto restore = qScopeGuard([&] { QApplication::setStyle(baseName); });
    CaseForm f;
    auto *spin = new QSpinBox;
    f.form->addRow(QStringLiteral("Start:"), spin);
    applyFormRhythm(f.form);
    QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
    f.window->setFocus();
    QTest::qWait(200);
    const QImage image = f.window->grab().toImage();
    QList<int> painted;
    for (QWidget *w : QList<QWidget *>{f.radios.first(), f.text, f.applyTo, f.check, spin})
        painted.append(paintedLeft(image, w, f.window.get()));
    QVERIFY2(allEqual(painted), qPrintable(join(painted)));
    if (qEnvironmentVariableIsSet("CTRM_GRAB"))
        image.save(qEnvironmentVariable("CTRM_GRAB") + QStringLiteral("-form.png"));
    // R5-02, R5-03: the text sits on the box as Qt draws it natively. The reference is the
    // offset between the text ink and the box ink of a plain button with the same text in a
    // plain layout.
    QWidget plain;
    auto *plainLayout = new QVBoxLayout(&plain);
    const auto addPlain = [&](QAbstractButton *b) {
        plainLayout->addWidget(b);
        return b;
    };
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *chain = new ChainOptionsWidget(&host);
    layout->addWidget(chain);
    layout->addStretch();
    chain->setOptions(ctrm::ChainOptions{});
    chain->setOpen(true);
    host.resize(320, 260);
    auto *portable = chain->findChild<QCheckBox *>(QStringLiteral("portableNames"));
    const QList<std::pair<QAbstractButton *, QWidget *>> pairs{
            {f.radios.first(), addPlain(new QRadioButton(f.radios.first()->text()))},
            {f.check, addPlain(new QCheckBox(f.check->text()))},
            {portable, addPlain(new QCheckBox(portable->text()))}};
    plain.resize(800, 120);
    plain.show();
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&plain));
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QTest::qWait(200);
    const QImage plainImage = plain.grab().toImage();
    const QImage chainImage = host.grab().toImage();
    QVERIFY(portable->height() > 30); // wraps at 296 px
    QCOMPARE(paintedLeft(chainImage, portable, &host), 12);
    // The text ink centre less the box ink centre, in the first 20 px row of the button.
    const auto offset = [&](const QImage &img, QWidget *to, QWidget *w) {
        const int left = paintedLeft(img, w, to);
        const int top = w->mapTo(to, QPoint(0, 0)).y();
        auto *b = static_cast<QAbstractButton *>(w);
        const int textX = left + 22 + b->fontMetrics().horizontalAdvance(b->text().left(1));
        return inkCentre(img, to, textX, textX + 30, top, top + 20) -
               inkCentre(img, to, left, left + 14, top, top + 20);
    };
    for (const auto &[w, ref] : pairs) {
        QWidget *to = w == portable ? &host : f.window.get();
        const QImage &img = w == portable ? chainImage : image;
        const double actual = offset(img, to, w);
        const double native = offset(plainImage, &plain, ref);
        QVERIFY2(qAbs(actual - native) <= 0.5, qPrintable(QStringLiteral("%1: offset %2, native %3")
                                                                  .arg(w->text())
                                                                  .arg(actual)
                                                                  .arg(native)));
    }
    if (qEnvironmentVariableIsSet("CTRM_GRAB"))
        chainImage.save(qEnvironmentVariable("CTRM_GRAB") + QStringLiteral("-chain.png"));
}

QTEST_MAIN(TestAppForms)
#include "tst_app_forms.moc"
