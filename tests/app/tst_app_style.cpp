// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app style (design-tokens 4.4 and 4.5, audit v0.2 O03, O05, O09, O10, C02, C05). Each test
// paints a control into an image and reads the pixels, with the app style over the base style
// of the run (Fusion, or the native style in the _native run).

#include "theme/colors.h"
#include "theme/ctrmstyle.h"
#include "theme/icons.h"
#include "theme/tokens.h"
#include "widgets/menutoolbutton.h"
#include "widgets/tertiarybutton.h"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStyleOption>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>

#include <cmath>
#include <memory>

using namespace ctrm::app;
namespace tokens = ctrm::app::tokens;

namespace {

bool near(const QColor &a, const QColor &b, int tolerance = 2)
{
    return std::abs(a.red() - b.red()) <= tolerance &&
           std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

QString describe(const QColor &a, const QColor &b)
{
    return a.name() + QStringLiteral(" != ") + b.name();
}

// Dark reference palette (design-tokens 2.2): the fills differ clearly from the window.
QPalette darkPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x2B2B2D));
    p.setColor(QPalette::Base, QColor(0x1E1E1E));
    p.setColor(QPalette::Button, QColor(0x2B2B2D));
    p.setColor(QPalette::Text, QColor(0xE3E3E3));
    p.setColor(QPalette::WindowText, QColor(0xE3E3E3));
    p.setColor(QPalette::ButtonText, QColor(0xE3E3E3));
    p.setColor(QPalette::PlaceholderText, QColor(0x9A9A9A));
    p.setColor(QPalette::Accent, QColor(0x4C9AFF));
    p.setColor(QPalette::Highlight, QColor(0x4C9AFF));
    p.setColor(QPalette::HighlightedText, QColor(0xFFFFFF));
    return p;
}

// Paints the widget on the window colour, at 1x.
QImage paint(QWidget *w)
{
    QImage image(w->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(w->palette().color(QPalette::Window));
    QPainter painter(&image);
    w->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();
    return image;
}

// The rows that have a pixel of colour c (in the centre column), as [top, bottom].
std::pair<int, int> rowsOf(const QImage &image, const QColor &c, int x)
{
    int top = -1;
    int bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        if (near(image.pixelColor(x, y), c)) {
            if (top < 0)
                top = y;
            bottom = y;
        }
    }
    return {top, bottom};
}

// A widget with the app style and the dark palette, in a parent so that render() works.
struct Styled
{
    std::unique_ptr<CtrmStyle> style{new CtrmStyle(QApplication::style()->name())};
    QWidget window;
    Styled()
    {
        window.setStyle(style.get());
        window.setPalette(darkPalette());
    }
    template <class W>
    W *add(W *w)
    {
        w->setParent(&window);
        // setStyle() does not go to the children (the edit field of a spin box); the app sets
        // the style for each widget with QApplication::setStyle().
        w->setStyle(style.get());
        for (QWidget *child : w->template findChildren<QWidget *>())
            child->setStyle(style.get());
        w->setPalette(darkPalette());
        w->ensurePolished();
        w->resize(w->sizeHint());
        return w;
    }
    CtrmColors colors() const { return CtrmColors::fromPalette(darkPalette()); }
};

} // namespace

class TestAppStyle : public QObject
{
    Q_OBJECT
private slots:
    void lineEditFilled();
    void spinBoxFilled();
    void fieldEdgesOnLayout();
    void pushButtonStates();
    void primaryButtonStates();
    void flatButtonStates();
    void tertiaryNeverNativeFrame();
    void iconButtonLook();
    void toolBarButtonHover();
    void toolBarMenuButtonIsTextButton();
};

void TestAppStyle::lineEditFilled()
{
    // O05, C05: one field family: fill-control, no border line, visible height control-h.
    Styled s;
    auto *edit = s.add(new QLineEdit);
    edit->resize(160, edit->sizeHint().height());
    QCOMPARE(edit->sizeHint().height(), tokens::controlH);
    const QImage image = paint(edit);
    const QColor fill = s.colors().fillControl;
    const auto [top, bottom] = rowsOf(image, fill, image.width() / 2);
    QCOMPARE(top, 0);
    QCOMPARE(bottom, tokens::controlH - 1);
    // No border line: the left and right edges at the centre row are the fill or anti-aliasing,
    // never a line of another grey.
    QVERIFY2(near(image.pixelColor(2, tokens::controlH / 2), fill),
             qPrintable(describe(image.pixelColor(2, tokens::controlH / 2), fill)));
    // Text inset space-2.
    QStyleOptionFrame option;
    option.initFrom(edit);
    option.rect = edit->rect();
    const QRect contents =
            edit->style()->subElementRect(QStyle::SE_LineEditContents, &option, edit);
    QCOMPARE(contents.left() + 2, tokens::space2); // QLineEdit adds a 2 px margin
}

void TestAppStyle::spinBoxFilled()
{
    // O05: the spin box is the same filled field, the stepper inside its right end.
    Styled s;
    auto *spin = s.add(new QSpinBox);
    spin->setRange(0, 99999);
    spin->resize(spin->sizeHint());
    QCOMPARE(spin->sizeHint().height(), tokens::controlH);
    const QImage image = paint(spin);
    const QColor fill = s.colors().fillControl;
    // Column 7: after the corner radius, before the text.
    const auto [top, bottom] = rowsOf(image, fill, tokens::radiusControl + 1);
    QCOMPARE(top, 0);
    QCOMPARE(bottom, tokens::controlH - 1);
    // The stepper is inside the field: the fill also shows at its right end.
    const auto [rTop, rBottom] = rowsOf(image, fill, image.width() - 3);
    QVERIFY2(rTop >= 0 && rTop <= 2 && rBottom >= tokens::controlH - 3,
             qPrintable(QStringLiteral("%1..%2").arg(rTop).arg(rBottom)));
    // The up and down parts are in the field.
    QStyleOptionSpinBox option;
    option.initFrom(spin);
    option.rect = spin->rect();
    option.subControls = QStyle::SC_All;
    option.frame = spin->hasFrame();
    const QRect up =
            spin->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, spin);
    const QRect field = spin->style()->subControlRect(QStyle::CC_SpinBox, &option,
                                                      QStyle::SC_SpinBoxEditField, spin);
    QVERIFY(spin->rect().contains(up));
    QVERIFY(field.right() < up.left());
}

void TestAppStyle::fieldEdgesOnLayout()
{
    // Area D: in a form the visible edges of the text field, the spin box and the combo box are
    // on the layout rectangle (one left edge). The field and the spin box fill their widget.
    Styled s;
    auto *form = new QFormLayout(&s.window);
    auto *edit = new QLineEdit;
    auto *spin = new QSpinBox;
    auto *combo = new QComboBox;
    combo->addItem(QStringLiteral("All"));
    form->addRow(QStringLiteral("Find:"), edit);
    form->addRow(QStringLiteral("Start:"), spin);
    form->addRow(QStringLiteral("Mode:"), combo);
    for (QWidget *w : s.window.findChildren<QWidget *>())
        w->setStyle(s.style.get());
    s.window.resize(400, 200);
    s.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&s.window));
    const int column = form->itemAt(0, QFormLayout::FieldRole)->geometry().left();
    QCOMPARE(edit->x(), column);
    QCOMPARE(spin->x(), column);
    QCOMPARE(form->itemAt(2, QFormLayout::FieldRole)->geometry().left(), column);
    // The combo box: its layout item rectangle is its visible frame.
    QStyleOptionComboBox option;
    option.initFrom(combo);
    option.rect = combo->rect();
    const QRect visible =
            combo->style()->subElementRect(QStyle::SE_ComboBoxLayoutItem, &option, combo);
    QCOMPARE(combo->x() + visible.left(), column);
}

void TestAppStyle::pushButtonStates()
{
    // O09: secondary push button: fill-control, hover fill-hover, pressed fill-pressed.
    Styled s;
    auto *button = s.add(new QPushButton);
    button->resize(80, button->sizeHint().height());
    const CtrmColors c = s.colors();
    const auto centre = [&] {
        const QImage image = paint(button);
        return image.pixelColor(image.width() / 2, image.height() / 2);
    };
    QVERIFY2(near(centre(), c.fillControl), qPrintable(describe(centre(), c.fillControl)));
    // Visible height control-h.
    const auto [top, bottom] = rowsOf(paint(button), c.fillControl, 40);
    QCOMPARE(bottom - top + 1, tokens::controlH);

    button->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY2(near(centre(), c.fillHover), qPrintable(describe(centre(), c.fillHover)));
    button->setDown(true);
    QVERIFY2(near(centre(), c.fillPressed), qPrintable(describe(centre(), c.fillPressed)));
}

void TestAppStyle::primaryButtonStates()
{
    // O09: primary: accent-fill, 8% lighter on hover, 8% darker when pressed.
    Styled s;
    auto *button = s.add(new QPushButton);
    button->setDefault(true);
    button->resize(80, button->sizeHint().height());
    const QColor fill = s.colors().accentFill;
    const auto centre = [&] {
        const QImage image = paint(button);
        return image.pixelColor(image.width() / 2, image.height() / 2);
    };
    QVERIFY2(near(centre(), fill), qPrintable(describe(centre(), fill)));
    button->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY2(near(centre(), fill.lighter(108)), qPrintable(describe(centre(), fill.lighter(108))));
    button->setDown(true);
    QVERIFY2(near(centre(), fill.darker(108)), qPrintable(describe(centre(), fill.darker(108))));
    // White text keeps 4.5:1 on the fill (design-tokens 2.6).
    QVERIFY(contrastRatio(fill, QColor(Qt::white)) >= 4.5);
}

void TestAppStyle::flatButtonStates()
{
    // C02, C15: a flat push button (message bar action) is a borderless button: no fill at
    // rest, fill-hover on hover, fill-pressed when pressed, space-1 padding, height control-h.
    Styled s;
    auto *button = s.add(new QPushButton(QStringLiteral("Undo")));
    button->setFlat(true);
    QPalette accent = darkPalette();
    accent.setColor(QPalette::ButtonText, accent.color(QPalette::Accent));
    button->setPalette(accent);
    button->resize(button->sizeHint());
    QCOMPARE(button->sizeHint().height(), tokens::controlH);
    QCOMPARE(button->sizeHint().width(),
             button->fontMetrics().size(Qt::TextShowMnemonic, button->text()).width() +
                     2 * tokens::space1);
    const CtrmColors c = s.colors();
    const QColor window = darkPalette().color(QPalette::Window);
    const int midY = tokens::controlH / 2;
    const auto at = [&](int x) { return paint(button).pixelColor(x, midY); };
    QVERIFY2(near(at(1), window), qPrintable(describe(at(1), window)));
    button->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY2(near(at(1), c.fillHover), qPrintable(describe(at(1), c.fillHover)));
    button->setDown(true);
    QVERIFY2(near(at(1), c.fillPressed), qPrintable(describe(at(1), c.fillPressed)));
    // The text has the ButtonText colour (accent in the message bar).
    const QImage pressed = paint(button);
    bool accentText = false;
    for (int x = 0; x < pressed.width() && !accentText; ++x)
        accentText = near(pressed.pixelColor(x, midY), accent.color(QPalette::Accent), 24);
    QVERIFY(accentText);
    // Disabled: no fill, also under the mouse.
    button->setDown(false);
    button->setEnabled(false);
    QVERIFY2(near(at(1), window), qPrintable(describe(at(1), window)));
}

void TestAppStyle::tertiaryNeverNativeFrame()
{
    // O03, C02: a borderless button has no fill at rest, fill-hover on hover, fill-pressed when
    // pressed, and never the native square frame with a grey line.
    Styled s;
    auto *button = s.add(new TertiaryButton);
    button->setText(QStringLiteral("Add files"));
    button->resize(button->sizeHint());
    const CtrmColors c = s.colors();
    const QColor window = darkPalette().color(QPalette::Window);
    const auto at = [&](int x, int y) { return paint(button).pixelColor(x, y); };
    const int midY = button->height() / 2;
    QVERIFY2(near(at(1, midY), window), qPrintable(describe(at(1, midY), window)));

    button->setAttribute(Qt::WA_UnderMouse, true);
    // The fill reaches the left edge (no line in another grey), with space-1 padding to the text.
    QVERIFY2(near(at(1, midY), c.fillHover), qPrintable(describe(at(1, midY), c.fillHover)));
    // No line of another grey at the left edge either.
    QVERIFY2(near(at(0, midY), c.fillHover), qPrintable(describe(at(0, midY), c.fillHover)));
    QVERIFY2(near(at(button->width() / 2, 1), c.fillHover) ||
                     near(at(button->width() / 2, 1), window),
             qPrintable(at(button->width() / 2, 1).name()));
    button->setDown(true);
    QVERIFY2(near(at(1, midY), c.fillPressed), qPrintable(describe(at(1, midY), c.fillPressed)));
}

void TestAppStyle::iconButtonLook()
{
    // O10, C02, C07: an icon button is a hit-min square, the icon text-dim at rest and text on
    // hover, on a fill-hover square.
    Styled s;
    auto *button = s.add(new TertiaryButton);
    button->setIconButton(true);
    // A white icon: the button recolours it.
    QPalette white = darkPalette();
    white.setColor(QPalette::WindowText, Qt::white);
    button->setIcon(toolIcon(QStringLiteral("tool-clear"), white));
    button->resize(button->sizeHint());
    QCOMPARE(button->sizeHint(), QSize(tokens::hitMin, tokens::hitMin));
    QCOMPARE(button->iconSize(), QSize(tokens::iconS, tokens::iconS));
    const CtrmColors c = s.colors();
    const auto hasColour = [](const QImage &image, const QColor &colour) {
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (near(image.pixelColor(x, y), colour, 1))
                    return true;
        return false;
    };
    const QColor dim = c.stateNeutral;
    const QColor text = darkPalette().color(QPalette::WindowText);
    QImage image = paint(button);
    QVERIFY(hasColour(image, dim));
    QVERIFY(!hasColour(image, QColor(Qt::white)));
    button->setAttribute(Qt::WA_UnderMouse, true);
    image = paint(button);
    QVERIFY(hasColour(image, text));
    QVERIFY2(near(image.pixelColor(1, tokens::hitMin / 2), c.fillHover),
             qPrintable(describe(image.pixelColor(1, tokens::hitMin / 2), c.fillHover)));
}

void TestAppStyle::toolBarButtonHover()
{
    // C02: the toolbar buttons get the same hover fill.
    Styled s;
    auto *bar = s.add(new QToolBar);
    QAction *action = bar->addAction(QStringLiteral("Undo"));
    bar->resize(bar->sizeHint());
    auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(action));
    QVERIFY(button);
    button->setStyle(s.style.get());
    button->setPalette(darkPalette());
    const CtrmColors c = s.colors();
    const auto centreLeft = [&] {
        const QImage image = paint(button);
        return image.pixelColor(2, image.height() / 2);
    };
    const QColor window = darkPalette().color(QPalette::Window);
    QVERIFY2(near(centreLeft(), window), qPrintable(describe(centreLeft(), window)));
    button->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY2(near(centreLeft(), c.fillHover), qPrintable(describe(centreLeft(), c.fillHover)));
}

void TestAppStyle::toolBarMenuButtonIsTextButton()
{
    // O03a: Presets (a MenuToolButton, which draws its own label) is a toolbar text button: the
    // fill is control-h high, also in a higher button.
    Styled s;
    auto *bar = s.add(new QToolBar);
    auto *button = new MenuToolButton;
    button->setText(QStringLiteral("Presets"));
    bar->addWidget(button);
    button->setStyle(s.style.get());
    button->setPalette(darkPalette());
    button->resize(button->sizeHint().width(), tokens::controlH + 8);
    button->setAttribute(Qt::WA_UnderMouse, true);
    const QImage image = paint(button);
    const auto [top, bottom] = rowsOf(image, s.colors().fillHover, tokens::radiusControl + 1);
    QCOMPARE(bottom - top + 1, tokens::controlH);
}

QTEST_MAIN(TestAppStyle)
#include "tst_app_style.moc"
