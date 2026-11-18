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
#include <QCheckBox>
#include <QComboBox>
#include <QFocusFrame>
#include <QFormLayout>
#include <QHeaderView>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QProxyStyle>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyleOption>
#include <QTableView>
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

// Light palette as macOS gives it: the highlighted text is near-black (audit R5-01).
QPalette lightPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(0xFFFFFF));
    p.setColor(QPalette::Base, QColor(0xFFFFFF));
    p.setColor(QPalette::Button, QColor(0xFFFFFF));
    p.setColor(QPalette::Text, QColor(0x1D1D1F));
    p.setColor(QPalette::WindowText, QColor(0x1D1D1F));
    p.setColor(QPalette::ButtonText, QColor(0x1D1D1F));
    p.setColor(QPalette::PlaceholderText, QColor(0x8A8A8E));
    p.setColor(QPalette::Accent, QColor(0x0A60FF));
    p.setColor(QPalette::Highlight, QColor(0xB3D7FF));
    p.setColor(QPalette::HighlightedText, QColor(0x020F27));
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

// A widget with the app style and a palette (dark by default), in a parent so that render()
// works.
struct Styled
{
    std::unique_ptr<CtrmStyle> style{new CtrmStyle(QApplication::style()->name())};
    QPalette palette;
    QWidget window;
    explicit Styled(const QPalette &p = darkPalette())
        : palette(p)
    {
        window.setStyle(style.get());
        window.setPalette(palette);
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
        w->setPalette(palette);
        w->ensurePolished();
        w->resize(w->sizeHint());
        return w;
    }
    CtrmColors colors() const { return CtrmColors::fromPalette(palette); }
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
    void primaryTextOnAccent_data();
    void primaryTextOnAccent();
    void mnemonicTextWhole();
    void flatButtonStates();
    void tertiaryNeverNativeFrame();
    void iconButtonLook();
    void toolBarButtonHover();
    void toolBarMenuButtonIsTextButton();
    void toolBarMenuButtonMatchesTextButton();
    void toolBarFirstIconAtInset();
    void oneFocusRing();
    void toggleCentredInTallRow();
    void headerNoSeparatorAfterLast();
    void paletteWindowSurface();
    void focusFrameOnFullSpinBox();
    void disabledFieldsHalfFill();
    void disabledTextDim();
    void disabledComboHalf();
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

void TestAppStyle::primaryTextOnAccent_data()
{
    QTest::addColumn<QPalette>("palette");
    QTest::addRow("light") << lightPalette();
    QTest::addRow("dark") << darkPalette();
}

void TestAppStyle::primaryTextOnAccent()
{
    // R5-01: the primary button text is on-accent (white) in both modes, never a palette role
    // that changes with the mode. 4.5:1 or more on the fill at rest, on hover and when pressed.
    QFETCH(QPalette, palette);
    Styled s(palette);
    const CtrmColors c = s.colors();
    QCOMPARE(c.onAccent, QColor(Qt::white));
    for (const QColor &fill : {c.accentFill, c.accentFill.lighter(108), c.accentFill.darker(108)})
        QVERIFY2(contrastRatio(c.onAccent, fill) >= 4.5,
                 qPrintable(QStringLiteral("white on ") + fill.name()));

    auto *button = s.add(new QPushButton(QStringLiteral("WWWW")));
    button->setDefault(true);
    button->resize(button->sizeHint());
    const QImage image = paint(button);
    // The text pixels are white; no pixel is the highlighted text colour of the palette.
    bool white = false;
    bool dark = false;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = tokens::radiusControl; x < image.width() - tokens::radiusControl; ++x) {
            const QColor px = image.pixelColor(x, y);
            white = white || near(px, c.onAccent, 24);
            dark = dark || near(px, palette.color(QPalette::HighlightedText), 24);
        }
    }
    QVERIFY(white);
    // In dark mode the palette highlighted text is white too; then there is nothing to tell apart.
    if (!near(palette.color(QPalette::HighlightedText), c.onAccent, 24))
        QVERIFY(!dark);
}

void TestAppStyle::mnemonicTextWhole()
{
    // A label with a mnemonic is not elided when it fits (area D check: "&Delete" showed "De...").
    Styled s;
    for (const QString &label :
         {QStringLiteral("&Delete"), QStringLiteral("&Rename..."), QStringLiteral("Show &File")}) {
        auto *button = s.add(new QPushButton(label));
        button->resize(button->sizeHint());
        const QImage image = paint(button);
        const QColor fill = s.colors().fillControl;
        int left = image.width();
        int right = -1;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = tokens::radiusControl; x < image.width() - tokens::radiusControl; ++x) {
                if (!near(image.pixelColor(x, y), fill, 24)) {
                    left = qMin(left, x);
                    right = qMax(right, x);
                }
            }
        }
        QString plain = label;
        plain.remove(QLatin1Char('&'));
        const int expected = button->fontMetrics().horizontalAdvance(plain);
        QVERIFY2(right - left + 1 >= expected - 3, qPrintable(QStringLiteral("%1: %2 < %3")
                                                                      .arg(label)
                                                                      .arg(right - left + 1)
                                                                      .arg(expected)));
    }
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
    // The text has the ButtonText colour (here the accent, a colour that is easy to find).
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

void TestAppStyle::toolBarMenuButtonMatchesTextButton()
{
    // Presets draws its own label with the toolbar text button layout (design-tokens 4.5):
    // space-2, icon-s icon, space-1, text, space-1, icon-xs chevron, space-2. It has the same
    // hover and pressed fills as a toolbar text button, and the chevron is on the text line (M17).
    Styled s;
    auto *bar = s.add(new QToolBar);
    bar->setIconSize(QSize(tokens::iconS, tokens::iconS));
    QPixmap square(tokens::iconS, tokens::iconS);
    square.fill(Qt::white);
    QAction *add = bar->addAction(QIcon(square), QStringLiteral("Add Files"));
    auto *presets = new MenuToolButton;
    presets->setText(QStringLiteral("Presets"));
    presets->setIcon(QIcon(square));
    bar->addWidget(presets);
    for (QWidget *child : bar->findChildren<QWidget *>()) {
        child->setStyle(s.style.get());
        child->setPalette(darkPalette());
    }
    auto *reference = qobject_cast<QToolButton *>(bar->widgetForAction(add));
    reference->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    for (QToolButton *b : {reference, static_cast<QToolButton *>(presets)})
        b->resize(b->sizeHint());

    const int text = presets->fontMetrics().size(Qt::TextShowMnemonic, presets->text()).width();
    QCOMPARE(presets->height(), tokens::controlH);
    QCOMPARE(presets->width(), tokens::space2 + tokens::iconS + tokens::space1 + text +
                                       tokens::space1 + tokens::iconXs + tokens::space2);

    // The icon starts at space-2 in both buttons.
    const auto firstWhite = [](QToolButton *b) {
        const QImage image = paint(b);
        for (int x = 0; x < image.width(); ++x) {
            if (near(image.pixelColor(x, image.height() / 2), Qt::white))
                return x;
        }
        return -1;
    };
    QCOMPARE(firstWhite(presets), tokens::space2);
    QCOMPARE(firstWhite(reference), tokens::space2);

    // M17: the chevron is at space-2 from the right edge, in the vertical centre (the text line).
    const QRect arrow = presets->arrowRect();
    QCOMPARE(arrow.size(), QSize(tokens::iconXs, tokens::iconXs));
    QCOMPARE(presets->width() - 1 - arrow.right(), tokens::space2);
    QVERIFY(qAbs(arrow.center().y() - presets->rect().center().y()) <= 1);

    // The same fills as the toolbar text button.
    const CtrmColors c = s.colors();
    const auto fillAt = [](QToolButton *b) {
        return paint(b).pixelColor(tokens::radiusControl + 1, b->height() / 2);
    };
    for (QToolButton *b : {reference, static_cast<QToolButton *>(presets)}) {
        b->setAttribute(Qt::WA_UnderMouse, true);
        QVERIFY2(near(fillAt(b), c.fillHover), qPrintable(describe(fillAt(b), c.fillHover)));
        b->setDown(true);
        QVERIFY2(near(fillAt(b), c.fillPressed), qPrintable(describe(fillAt(b), c.fillPressed)));
        b->setDown(false);
        b->setAttribute(Qt::WA_UnderMouse, false);
    }
}

void TestAppStyle::toolBarFirstIconAtInset()
{
    // O07, design-tokens 4.2: the first icon of the toolbar starts at x = space-3. Text buttons
    // are control-h high; icon buttons are hit-min squares.
    Styled s;
    auto *bar = s.add(new QToolBar);
    bar->setMovable(false);
    bar->setIconSize(QSize(tokens::iconS, tokens::iconS));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QPixmap square(tokens::iconS, tokens::iconS);
    square.fill(Qt::white);
    QAction *add = bar->addAction(QIcon(square), QStringLiteral("Add Files"));
    QAction *undo = bar->addAction(QIcon(square), QStringLiteral("Undo"));
    for (QWidget *child : bar->findChildren<QWidget *>())
        child->setStyle(s.style.get());
    auto *undoButton = qobject_cast<QToolButton *>(bar->widgetForAction(undo));
    undoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    bar->resize(400, bar->sizeHint().height());
    bar->layout()->activate();
    auto *addButton = qobject_cast<QToolButton *>(bar->widgetForAction(add));
    QCOMPARE(addButton->height(), tokens::controlH);
    QCOMPARE(undoButton->size(), QSize(tokens::hitMin, tokens::hitMin));
    const QImage image = paint(bar);
    int first = -1;
    for (int x = 0; x < image.width() && first < 0; ++x)
        if (near(image.pixelColor(x, addButton->geometry().center().y()), QColor(Qt::white), 8))
            first = x;
    QCOMPARE(first, tokens::space3);
}

void TestAppStyle::oneFocusRing()
{
    // Area D check: one focus ring. On macOS the focus frame draws the text field ring around a
    // spin box and a push button (not the square ring, not a second ring in the button).
    Styled s;
    auto *edit = s.add(new QLineEdit);
    auto *spin = s.add(new QSpinBox);
    auto *button = s.add(new QPushButton(QStringLiteral("Roll Back")));
    button->setDefault(true);
    const auto ring = [&](QWidget *target, QSize size) {
        target->resize(size);
        QFocusFrame frame(&s.window);
        frame.setStyle(s.style.get());
        frame.setWidget(target);
        frame.resize(size + QSize(8, 8));
        QImage image(frame.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        frame.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        return image;
    };
    const QSize size(80, tokens::controlH);
    // QMacStyle draws the ring only with Cocoa (offscreen it draws nothing): run this test
    // with the cocoa platform and -style macOS to check the rings.
    const QImage fieldRing = ring(edit, size);
    QCOMPARE(ring(spin, size), fieldRing);
    QCOMPARE(ring(button, size), fieldRing);

    // The button draws no ring of its own on macOS; on the other styles it draws one.
    QStyleOptionButton option;
    option.initFrom(button);
    option.rect = QRect(QPoint(0, 0), size);
    option.features = QStyleOptionButton::DefaultButton;
    option.state |= QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(darkPalette().color(QPalette::Window));
    QPainter painter(&image);
    s.style->drawControl(QStyle::CE_PushButton, &option, &painter, button);
    painter.end();
    const bool mac =
            s.style->baseStyle()->name().compare(QStringLiteral("macos"), Qt::CaseInsensitive) == 0;
    const QColor edge = image.pixelColor(1, size.height() / 2);
    QCOMPARE(near(edge, s.colors().accentFill), mac);
}

namespace {

// A base style that keeps the rect and the painter offset of the last check box or radio button.
class ToggleSpy : public QProxyStyle
{
public:
    ToggleSpy()
        : QProxyStyle(QStringLiteral("Fusion"))
    {}
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override
    {
        if (element == CE_CheckBox || element == CE_RadioButton) {
            rect = option->rect;
            dy = qRound(painter->transform().dy());
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
    mutable QRect rect;
    mutable int dy = 0;
};

} // namespace

void TestAppStyle::toggleCentredInTallRow()
{
    // R5-02: a form row is taller than a check box. QMacStyle draws the native indicator at the
    // top of the widget and the text in the centre. The app style draws the control at its
    // natural height in the centre of the row, so the indicator and the text share one centre.
    Styled s;
    auto *spy = new ToggleSpy;
    s.style->setBaseStyle(spy); // the app style takes it
    for (QAbstractButton *toggle :
         {static_cast<QAbstractButton *>(new QCheckBox(QStringLiteral("Hidden files"))),
          static_cast<QAbstractButton *>(new QRadioButton(QStringLiteral("Before")))}) {
        s.add(toggle);
        const int natural = toggle->sizeHint().height();
        toggle->setFixedHeight(natural + 7);
        paint(toggle);
        QCOMPARE(spy->rect.height(), natural);
        QCOMPARE(spy->dy, 7 / 2);
        // At its natural height nothing moves.
        toggle->setFixedHeight(natural);
        paint(toggle);
        QCOMPARE(spy->rect.height(), natural);
        QCOMPARE(spy->dy, 0);
    }
}

void TestAppStyle::headerNoSeparatorAfterLast()
{
    // R5-11: the table header draws no separator after the last section; the other sections
    // keep theirs.
    Styled s;
    auto *table = new QTableView;
    auto *model = new QStandardItemModel(1, 3, table);
    table->setModel(model);
    table->horizontalHeader()->setStretchLastSection(true);
    s.add(table);
    table->resize(400, 120);
    QHeaderView *header = table->horizontalHeader();
    for (QWidget *child : table->findChildren<QWidget *>())
        child->setStyle(s.style.get());
    header->resize(header->width(), header->sizeHint().height());
    const QImage image = header->grab().toImage();
    const qreal dpr = image.devicePixelRatio();
    const int y = qRound(header->height() / 2 * dpr);
    const auto rightEdgeIsInside = [&](int column) {
        const int left = header->sectionViewportPosition(column);
        const int right = left + header->sectionSize(column);
        const QColor inside = image.pixelColor(qRound((right - 6) * dpr), y);
        for (int x = qRound((right - 2) * dpr); x < qRound(right * dpr); ++x) {
            if (image.pixelColor(x, y) != inside)
                return false;
        }
        return true;
    };
    // The style draws separators at all: the first section has one. QMacStyle draws no header
    // with the offscreen platform; the Fusion run and a Cocoa run check this.
    if (rightEdgeIsInside(0))
        QSKIP("The style draws no header separator on this platform.");
    QVERIFY(rightEdgeIsInside(2));
}

void TestAppStyle::paletteWindowSurface()
{
    // R5-09: the app style puts the computed `window` into the Window role of the app palette,
    // so each `window` surface (panels, bars, dialogs) differs from `base` (table, list).
    CtrmStyle style(QApplication::style()->name());
    QPalette p = lightPalette(); // Window = Base = white, as macOS light mode
    const CtrmColors c = CtrmColors::fromPalette(p);
    style.polish(p);
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
        QCOMPARE(p.color(group, QPalette::Window), c.window);
    QCOMPARE(p.color(QPalette::Base), QColor(0xFFFFFF));
    QVERIFY(p.color(QPalette::Window) != p.color(QPalette::Base));
    // Polish again: no change; the fills stay the same.
    QPalette again = p;
    style.polish(again);
    QCOMPARE(again.color(QPalette::Window), c.window);
    QCOMPARE(CtrmColors::fromPalette(again).fillControl, c.fillControl);
    // Dark mode keeps its Window.
    QPalette dark = darkPalette();
    style.polish(dark);
    QCOMPARE(dark.color(QPalette::Window), darkPalette().color(QPalette::Window));
}

void TestAppStyle::focusFrameOnFullSpinBox()
{
    // R6-01: QMacStyle puts the focus frame on the line edit inside a spin box. The app style
    // moves it to the spin box, so that one ring goes around the full field, stepper included.
    Styled s;
    auto *spin = s.add(new QSpinBox);
    QFocusFrame frame(&s.window);
    frame.setStyle(s.style.get());
    frame.setWidget(spin->findChild<QLineEdit *>());
    QEvent show(QEvent::Show);
    QApplication::sendEvent(&frame, &show);
    QCOMPARE(frame.widget(), spin);
    // A plain line edit keeps its frame.
    auto *edit = s.add(new QLineEdit);
    frame.setWidget(edit);
    QApplication::sendEvent(&frame, &show);
    QCOMPARE(frame.widget(), edit);
}

void TestAppStyle::disabledFieldsHalfFill()
{
    // R6-03: a disabled text field and spin box have the fill at 50% (design-tokens 4.5).
    for (const QPalette &palette : {darkPalette(), lightPalette()}) {
        Styled s(palette);
        auto *edit = s.add(new QLineEdit);
        auto *spin = s.add(new QSpinBox);
        edit->resize(160, tokens::controlH);
        edit->setEnabled(false);
        spin->setEnabled(false);
        const QColor window = palette.color(QPalette::Window);
        const QColor half = mix(s.colors().fillControl, window, 0.5);
        const QColor editPixel = paint(edit).pixelColor(edit->width() - 4, tokens::controlH / 2);
        QVERIFY2(near(editPixel, half), qPrintable(describe(editPixel, half)));
        const QColor spinPixel = paint(spin).pixelColor(tokens::radiusControl + 1, 2);
        QVERIFY2(near(spinPixel, half), qPrintable(describe(spinPixel, half)));
    }
}

void TestAppStyle::disabledTextDim()
{
    // R6-03: the text and the placeholder of a disabled field are text-dim.
    CtrmStyle style(QApplication::style()->name());
    for (QPalette p : {darkPalette(), lightPalette()}) {
        const QColor dim = CtrmColors::fromPalette(p).stateNeutral;
        style.polish(p);
        QCOMPARE(p.color(QPalette::Disabled, QPalette::Text), dim);
        QCOMPARE(p.color(QPalette::Disabled, QPalette::PlaceholderText), dim);
        QVERIFY(p.color(QPalette::Active, QPalette::Text) != dim);
    }
}

void TestAppStyle::disabledComboHalf()
{
    // R6-03: a disabled combo box is the enabled combo box at 50% over its background, and its
    // label is text-dim.
    Styled s;
    auto *combo = s.add(new QComboBox);
    combo->addItem(QStringLiteral("MMMM"));
    combo->resize(combo->sizeHint());
    const QColor window = s.palette.color(QPalette::Window);
    const auto draw = [&](bool enabled) {
        combo->setEnabled(enabled);
        QStyleOptionComboBox option;
        option.initFrom(combo);
        option.rect = combo->rect();
        option.subControls = QStyle::SC_All;
        option.editable = false;
        option.frame = true;
        QImage image(combo->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(window);
        QPainter painter(&image);
        s.style->drawComplexControl(QStyle::CC_ComboBox, &option, &painter, combo);
        return image;
    };
    const QImage on = draw(true);
    const QImage off = draw(false);
    for (int y = 0; y < on.height(); ++y) {
        for (int x = 0; x < on.width(); ++x) {
            const QColor want = mix(on.pixelColor(x, y), window, 0.5);
            QVERIFY2(near(off.pixelColor(x, y), want, 3),
                     qPrintable(QStringLiteral("%1,%2 ").arg(x).arg(y) +
                                describe(off.pixelColor(x, y), want)));
        }
    }

    // The label: the strongest pixel is text-dim.
    combo->setEnabled(false);
    QStyleOptionComboBox option;
    option.initFrom(combo);
    option.rect = combo->rect();
    option.currentText = combo->currentText();
    QImage label(combo->size(), QImage::Format_ARGB32_Premultiplied);
    label.fill(window);
    QPainter painter(&label);
    s.style->drawControl(QStyle::CE_ComboBoxLabel, &option, &painter, combo);
    painter.end();
    QColor strongest = window;
    for (int y = 0; y < label.height(); ++y)
        for (int x = 0; x < label.width(); ++x)
            if (std::abs(label.pixelColor(x, y).lightness() - window.lightness()) >
                std::abs(strongest.lightness() - window.lightness()))
                strongest = label.pixelColor(x, y);
    if (strongest == window)
        QSKIP("This style draws no combo box label (QMacStyle without Cocoa).");
    const QColor dim = s.colors().stateNeutral;
    QVERIFY2(near(strongest, dim, 6), qPrintable(describe(strongest, dim)));
}

QTEST_MAIN(TestAppStyle)
#include "tst_app_style.moc"
