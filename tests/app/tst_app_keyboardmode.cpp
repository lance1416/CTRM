// SPDX-License-Identifier: GPL-3.0-or-later
//
// The keyboard mode and the focus rings (ui-design 14 "Focus ring", audit O11 area A):
// 1. Tab, Shift+Tab or a focus key turns the keyboard mode on, a mouse press turns it off, and a
//    focus change by the program does not change it.
// 2. Text entry controls show the ring whenever they have the focus, also after a click.
// 3. All other controls show the ring only in keyboard mode, and take no click focus.
// 4. Lists and tables show no ring. In keyboard mode the current row that is not selected has a
//    1 px accent outline inside the row.

#include "theme/ctrmstyle.h"
#include "theme/keyboardmode.h"
#include "theme/tokens.h"
#include "widgets/tertiarybutton.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFocusFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProxyStyle>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyleOption>
#include <QTableView>
#include <QToolButton>
#include <QtTest>

#include <cmath>
#include <memory>

using namespace ctrm::app;
namespace tokens = ctrm::app::tokens;

namespace {

const QColor kAccent(0xE0, 0x10, 0x90); // a colour that no style draws by itself

bool near(const QColor &a, const QColor &b, int tolerance = 24)
{
    return std::abs(a.red() - b.red()) <= tolerance &&
           std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

QPalette testPalette()
{
    QPalette p = QApplication::palette();
    p.setColor(QPalette::Accent, kAccent);
    return p;
}

bool isMac(const CtrmStyle &style)
{
    return style.baseStyle()->name().compare(QStringLiteral("macos"), Qt::CaseInsensitive) == 0;
}

int accentPixels(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            count += near(image.pixelColor(x, y), kAccent) ? 1 : 0;
    return count;
}

QImage blank(QSize size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    return image;
}

// A base style that keeps what the app style gives it.
class Spy : public QProxyStyle
{
public:
    Spy()
        : QProxyStyle(QStringLiteral("Fusion"))
    {}
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget) const override
    {
        if (element == PE_FrameFocusRect)
            ++focusRects;
        if (element == PE_PanelItemViewItem)
            states[-1] = option->state;
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override
    {
        states[element] = option->state;
        QProxyStyle::drawControl(element, option, painter, widget);
    }
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget) const override
    {
        states[1000 + control] = option->state;
        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }
    mutable int focusRects = 0;
    mutable QHash<int, QStyle::State> states;
};

// The app style over a spy base style.
struct Spied
{
    std::unique_ptr<CtrmStyle> style{new CtrmStyle(QStringLiteral("Fusion"))};
    Spy *spy = new Spy;
    Spied() { style->setBaseStyle(spy); }
};

// A shown, active window with the app style and the test palette.
struct Window
{
    std::unique_ptr<CtrmStyle> style{new CtrmStyle(QApplication::style()->name())};
    QWidget window;
    QHBoxLayout *layout = new QHBoxLayout(&window);
    template <class W>
    W *add(W *w)
    {
        layout->addWidget(w);
        return w;
    }
    void show()
    {
        for (QWidget *w : window.findChildren<QWidget *>())
            w->setStyle(style.get());
        window.setStyle(style.get());
        window.setPalette(testPalette());
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
    }
};

} // namespace

class TestKeyboardMode : public QObject
{
    Q_OBJECT
private slots:
    void init() { KeyboardMode::instance()->setOn(false); }

    void tabTurnsOnMouseTurnsOff();
    void programFocusKeepsMode();
    void dialogShowKeepsMode();
    void tabTypedInTextEditKeepsMode();
    void ringRules();
    void textEntryRingAfterClick();
    void buttonRingOnlyInKeyboardMode();
    void tertiaryRingOnlyInKeyboardMode();
    void toggleAndComboFocusOnlyInKeyboardMode();
    void otherControlsTakeTabFocus();
    void itemViewsNoRing();
    void currentRowOutline();
    void macFocusFrameRules();
};

void TestKeyboardMode::tabTurnsOnMouseTurnsOff()
{
    Window w;
    auto *first = w.add(new QLineEdit);
    auto *second = w.add(new QLineEdit);
    w.show();
    first->setFocus(Qt::OtherFocusReason);
    QSignalSpy changed(KeyboardMode::instance(), &KeyboardMode::changed);

    QTest::keyClick(first, Qt::Key_Tab);
    QVERIFY(KeyboardMode::isOn());
    QCOMPARE(QApplication::focusWidget(), second);
    QVERIFY(w.window.testAttribute(Qt::WA_KeyboardFocusChange));
    QStyleOption option;
    option.initFrom(second);
    QVERIFY(option.state & QStyle::State_KeyboardFocusChange);

    QTest::mouseClick(first, Qt::LeftButton);
    QVERIFY(!KeyboardMode::isOn());
    QVERIFY(!w.window.testAttribute(Qt::WA_KeyboardFocusChange));

    QTest::keyClick(first, Qt::Key_Backtab, Qt::ShiftModifier);
    QVERIFY(KeyboardMode::isOn());
    QTest::mouseClick(second, Qt::LeftButton);
    QVERIFY(!KeyboardMode::isOn());
    QCOMPARE(changed.count(), 4);

    // A focus key (a focus shortcut, or an arrow key in a button group) turns it on.
    first->setFocus(Qt::ShortcutFocusReason);
    QVERIFY(KeyboardMode::isOn());
}

void TestKeyboardMode::programFocusKeepsMode()
{
    Window w;
    auto *first = w.add(new QLineEdit);
    auto *second = w.add(new QLineEdit);
    w.show();
    first->setFocus(Qt::OtherFocusReason);
    second->setFocus(Qt::OtherFocusReason);
    QVERIFY(!KeyboardMode::isOn());
    KeyboardMode::instance()->setOn(true);
    first->setFocus(Qt::OtherFocusReason);
    QVERIFY(KeyboardMode::isOn());
    QVERIFY(w.window.testAttribute(Qt::WA_KeyboardFocusChange));
}

void TestKeyboardMode::dialogShowKeepsMode()
{
    // QDialog sends a synthetic Tab FocusIn to its focus widget when it shows: a dialog opened
    // with the mouse stays out of keyboard mode.
    CtrmStyle style(QApplication::style()->name());
    QDialog dialog;
    auto *layout = new QHBoxLayout(&dialog);
    layout->addWidget(new QPushButton(QStringLiteral("OK")));
    layout->addWidget(new QLineEdit);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QVERIFY(!KeyboardMode::isOn());
}

void TestKeyboardMode::tabTypedInTextEditKeepsMode()
{
    Window w;
    auto *text = w.add(new QPlainTextEdit);
    w.add(new QLineEdit);
    w.show();
    text->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(text, Qt::Key_Tab);
    QVERIFY(!KeyboardMode::isOn());
    QCOMPARE(QApplication::focusWidget(), text);
    QCOMPARE(text->toPlainText(), QStringLiteral("\t"));
}

void TestKeyboardMode::ringRules()
{
    QWidget parent;
    auto *edit = new QLineEdit(&parent);
    auto *spin = new QSpinBox(&parent);
    auto *editable = new QComboBox(&parent);
    editable->setEditable(true);
    const QList<QWidget *> text{edit, spin, spin->findChild<QLineEdit *>(), editable};
    auto *popup = new QComboBox(&parent);
    auto *link = new QLabel(QStringLiteral("<a href=\"x\">x</a>"), &parent);
    const QList<QWidget *> other{new QPushButton(&parent),
                                 new QToolButton(&parent),
                                 new QCheckBox(&parent),
                                 new QRadioButton(&parent),
                                 popup,
                                 link};
    auto *table = new QTableView(&parent);
    auto *list = new QListView(&parent);
    const QList<QWidget *> views{table, table->viewport(), list, list->viewport()};

    for (const bool on : {false, true}) {
        KeyboardMode::instance()->setOn(on);
        for (QWidget *w : text)
            QVERIFY2(KeyboardMode::showsRing(w), w->metaObject()->className());
        for (QWidget *w : other)
            QCOMPARE(KeyboardMode::showsRing(w), on);
        for (QWidget *w : views)
            QVERIFY2(!KeyboardMode::showsRing(w), w->metaObject()->className());
    }
}

void TestKeyboardMode::textEntryRingAfterClick()
{
    // Rule 2: the ring with the focus and no keyboard mode (a click). On macOS the focus frame
    // draws it (ringRules covers its choice).
    CtrmStyle style(QApplication::style()->name());
    if (isMac(style))
        QSKIP("The macOS focus frame draws the text field ring.");
    QWidget parent;
    auto *edit = new QLineEdit(&parent);
    auto *spin = new QSpinBox(&parent);
    const QSize size(80, tokens::controlH);

    QStyleOptionFrame frame;
    frame.initFrom(edit);
    frame.palette = testPalette();
    frame.rect = QRect(QPoint(0, 0), size);
    frame.lineWidth = 1;
    frame.state = QStyle::State_Enabled | QStyle::State_HasFocus;
    QImage image = blank(size);
    {
        QPainter painter(&image);
        style.drawPrimitive(QStyle::PE_PanelLineEdit, &frame, &painter, edit);
    }
    QVERIFY(near(image.pixelColor(0, size.height() / 2), kAccent));

    QStyleOptionSpinBox box;
    box.initFrom(spin);
    box.palette = testPalette();
    box.rect = QRect(QPoint(0, 0), size);
    box.frame = true;
    box.state = QStyle::State_Enabled | QStyle::State_HasFocus;
    image = blank(size);
    {
        QPainter painter(&image);
        style.drawComplexControl(QStyle::CC_SpinBox, &box, &painter, spin);
    }
    QVERIFY(near(image.pixelColor(0, size.height() / 2), kAccent));
}

void TestKeyboardMode::buttonRingOnlyInKeyboardMode()
{
    CtrmStyle style(QApplication::style()->name());
    if (isMac(style))
        QSKIP("The macOS focus frame draws the push button ring (ringRules).");
    QWidget parent;
    auto *button = new QPushButton(QStringLiteral("Rename"), &parent);
    const QSize size(80, tokens::controlH);
    QStyleOptionButton option;
    option.initFrom(button);
    option.palette = testPalette();
    option.rect = QRect(QPoint(0, 0), size);
    option.state = QStyle::State_Enabled | QStyle::State_HasFocus | QStyle::State_Raised;
    for (const bool on : {false, true}) {
        KeyboardMode::instance()->setOn(on);
        QImage image = blank(size);
        {
            QPainter painter(&image);
            style.drawControl(QStyle::CE_PushButton, &option, &painter, button);
        }
        QCOMPARE(near(image.pixelColor(0, size.height() / 2), kAccent), on);
    }
}

void TestKeyboardMode::tertiaryRingOnlyInKeyboardMode()
{
    // The Chain options header and the other tertiary buttons draw their own ring.
    Window w;
    auto *button = w.add(new TertiaryButton);
    button->setText(QStringLiteral("Chain options"));
    w.add(new QLineEdit);
    w.show();
    button->setFocus(Qt::OtherFocusReason);
    QVERIFY(button->hasFocus());
    for (const bool on : {false, true}) {
        KeyboardMode::instance()->setOn(on);
        const QImage image = button->grab().toImage();
        const qreal dpr = image.devicePixelRatio();
        QCOMPARE(near(image.pixelColor(0, int(button->height() / 2 * dpr)), kAccent), on);
    }
}

void TestKeyboardMode::toggleAndComboFocusOnlyInKeyboardMode()
{
    Spied s;
    QWidget parent;
    auto *check = new QCheckBox(QStringLiteral("Recursive"), &parent);
    auto *radio = new QRadioButton(QStringLiteral("Name"), &parent);
    auto *popup = new QComboBox(&parent);
    auto *editable = new QComboBox(&parent);
    editable->setEditable(true);
    const QSize size(120, tokens::controlH);
    QImage image = blank(size);
    QPainter painter(&image);

    for (const bool on : {false, true}) {
        KeyboardMode::instance()->setOn(on);
        for (QWidget *toggle : {static_cast<QWidget *>(check), static_cast<QWidget *>(radio)}) {
            const auto element = qobject_cast<QCheckBox *>(toggle) ? QStyle::CE_CheckBox
                                                                   : QStyle::CE_RadioButton;
            QStyleOptionButton option;
            option.initFrom(toggle);
            option.rect = QRect(QPoint(0, 0), size);
            option.text = QStringLiteral("Text");
            option.state = QStyle::State_Enabled | QStyle::State_HasFocus | QStyle::State_Off;
            s.spy->focusRects = 0;
            s.style->drawControl(element, &option, &painter, toggle);
            QCOMPARE(bool(s.spy->states.value(element) & QStyle::State_HasFocus), on);
            QCOMPARE(s.spy->focusRects > 0, on);
        }
        // A focus rect of a control (a wrapping check box draws one itself).
        QStyleOptionFocusRect focus;
        focus.initFrom(check);
        focus.rect = QRect(QPoint(0, 0), size);
        s.spy->focusRects = 0;
        s.style->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, check);
        QCOMPARE(s.spy->focusRects > 0, on);

        QStyleOptionComboBox combo;
        combo.initFrom(popup);
        combo.rect = QRect(QPoint(0, 0), size);
        combo.state = QStyle::State_Enabled | QStyle::State_HasFocus;
        s.style->drawComplexControl(QStyle::CC_ComboBox, &combo, &painter, popup);
        QCOMPARE(bool(s.spy->states.value(1000 + QStyle::CC_ComboBox) & QStyle::State_HasFocus),
                 on);
        // An editable combo box is a text entry: the keyboard look after a click too.
        combo.initFrom(editable);
        combo.rect = QRect(QPoint(0, 0), size);
        combo.editable = true;
        combo.state = QStyle::State_Enabled | QStyle::State_HasFocus;
        s.style->drawComplexControl(QStyle::CC_ComboBox, &combo, &painter, editable);
        const QStyle::State state = s.spy->states.value(1000 + QStyle::CC_ComboBox);
        QVERIFY(state & QStyle::State_HasFocus);
        QVERIFY(state & QStyle::State_KeyboardFocusChange);
    }
}

void TestKeyboardMode::otherControlsTakeTabFocus()
{
    // Rule 3: a click does not focus these controls, so a click leaves no ring.
    CtrmStyle style(QApplication::style()->name());
    QWidget parent;
    auto *link = new QLabel(QStringLiteral("<a href=\"x\">x</a>"), &parent);
    link->setTextInteractionFlags(Qt::TextBrowserInteraction);
    auto *editable = new QComboBox(&parent);
    editable->setEditable(true);
    auto *edit = new QLineEdit(&parent);
    const QList<QWidget *> tabOnly{new QCheckBox(&parent), new QRadioButton(&parent),
                                   new QComboBox(&parent), link};
    for (QWidget *w : tabOnly) {
        w->setStyle(&style);
        w->ensurePolished();
        QVERIFY2(w->focusPolicy() == Qt::TabFocus, w->metaObject()->className());
    }
    for (QWidget *w : {static_cast<QWidget *>(editable), static_cast<QWidget *>(edit)}) {
        w->setStyle(&style);
        w->ensurePolished();
        QVERIFY2(w->focusPolicy() & Qt::ClickFocus, w->metaObject()->className());
    }
}

void TestKeyboardMode::itemViewsNoRing()
{
    Spied s;
    QStandardItemModel model(2, 2);
    QTableView table;
    table.setModel(&model);
    QImage image = blank(QSize(100, 30));
    QPainter painter(&image);
    for (const bool on : {false, true}) {
        KeyboardMode::instance()->setOn(on);
        QStyleOptionViewItem item;
        item.initFrom(&table);
        item.widget = &table;
        item.rect = QRect(0, 0, 100, 30);
        item.state = QStyle::State_Enabled | QStyle::State_HasFocus;
        s.spy->focusRects = 0;
        s.style->drawControl(QStyle::CE_ItemViewItem, &item, &painter, &table);
        QVERIFY(!(s.spy->states.value(QStyle::CE_ItemViewItem) & QStyle::State_HasFocus));
        s.style->drawPrimitive(QStyle::PE_PanelItemViewItem, &item, &painter, &table);
        QVERIFY(!(s.spy->states.value(-1) & QStyle::State_HasFocus));
        QStyleOptionFocusRect focus;
        focus.initFrom(&table);
        focus.rect = item.rect;
        s.style->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, table.viewport());
        s.style->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, &table);
        QCOMPARE(s.spy->focusRects, 0);
    }
}

void TestKeyboardMode::currentRowOutline()
{
    Window w;
    QStandardItemModel model(3, 3);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            model.setItem(row, column, new QStandardItem(QString()));
    auto *table = w.add(new QTableView);
    table->setModel(&model);
    table->setShowGrid(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); // all in view
    table->horizontalHeader()->hide();
    table->verticalHeader()->hide();
    w.add(new QLineEdit);
    w.show();
    table->setFocus(Qt::OtherFocusReason);
    table->selectionModel()->setCurrentIndex(model.index(1, 1), QItemSelectionModel::NoUpdate);

    const auto grab = [&] {
        QImage image = table->viewport()->grab().toImage();
        image.setDevicePixelRatio(1.0);
        return image.scaled(table->viewport()->size());
    };
    const int top = table->rowViewportPosition(1);
    const int bottom = top + table->rowHeight(1) - 1;
    const int left = table->columnViewportPosition(0);
    const int right = table->columnViewportPosition(2) + table->columnWidth(2) - 1;
    const int midX = table->columnViewportPosition(1) + table->columnWidth(1) / 2;
    const int midY = (top + bottom) / 2;

    QCOMPARE(accentPixels(grab()), 0); // no keyboard mode: no outline
    KeyboardMode::instance()->setOn(true);
    QImage image = grab();
    QVERIFY(near(image.pixelColor(midX, top), kAccent));
    QVERIFY(near(image.pixelColor(midX, bottom), kAccent));
    QVERIFY(near(image.pixelColor(left, midY), kAccent));
    QVERIFY(near(image.pixelColor(right, midY), kAccent));
    QVERIFY(!near(image.pixelColor(midX, midY), kAccent));
    QVERIFY(!near(image.pixelColor(midX, top - 1), kAccent)); // inside the row
    QVERIFY(!near(image.pixelColor(midX, bottom + 1), kAccent));

    // A row with a selected cell has no outline (a table that selects cells), and a selected
    // current row has none.
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->selectionModel()->select(model.index(1, 2), QItemSelectionModel::Select);
    QCOMPARE(accentPixels(grab()), 0);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->selectionModel()->select(model.index(1, 0),
                                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCOMPARE(accentPixels(grab()), 0);
}

void TestKeyboardMode::macFocusFrameRules()
{
    // macOS: the focus frame draws the rings. A text entry has its ring after a click; a push
    // button only in keyboard mode, with the text field ring.
    CtrmStyle style(QApplication::style()->name());
    if (!isMac(style))
        QSKIP("The other styles draw the rings in the control (the tests above).");
    QWidget parent;
    parent.setStyle(&style);
    auto *edit = new QLineEdit(&parent);
    auto *button = new QPushButton(QStringLiteral("Rename"), &parent);
    const QSize size(80, tokens::controlH);
    const auto ring = [&](QWidget *target) {
        target->setStyle(&style);
        target->resize(size);
        QFocusFrame frame(&parent);
        frame.setStyle(&style);
        frame.setWidget(target);
        frame.resize(size + QSize(8, 8));
        QImage image(frame.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        frame.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        return image;
    };
    const QImage fieldRing = ring(edit);
    QImage empty(fieldRing.size(), fieldRing.format());
    empty.fill(Qt::transparent);
    if (fieldRing == empty)
        QSKIP("QMacStyle draws rings only with Cocoa: run with -platform cocoa -style macOS.");
    QCOMPARE(ring(button), empty);
    KeyboardMode::instance()->setOn(true);
    QCOMPARE(ring(edit), fieldRing);
    QCOMPARE(ring(button), fieldRing);
}

QTEST_MAIN(TestKeyboardMode)
#include "tst_app_keyboardmode.moc"
