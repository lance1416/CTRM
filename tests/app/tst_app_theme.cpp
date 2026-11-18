// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/surface.h"
#include "theme/tokens.h"
#include "theme/typography.h"
#include "widgets/tertiarybutton.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QListView>
#include <QPalette>
#include <QRegularExpression>
#include <QStyle>
#include <QStyleFactory>
#include <QTableView>
#include <QtTest>

#include <cmath>
#include <memory>

using namespace ctrm::app;

namespace {

bool near(const QColor &a, const QColor &b, int tolerance)
{
    return std::abs(a.red() - b.red()) <= tolerance &&
           std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

QString describe(const QColor &a, const QColor &b)
{
    return a.name() + QStringLiteral(" != ") + b.name();
}

QPalette paletteWith(QPalette p, const QColor &base, const QColor &text)
{
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::WindowText, text);
    return p;
}

// Reference palettes of design-tokens 2.1 and 2.2.
QPalette referencePalette(bool dark)
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(dark ? 0x2B2B2D : 0xECECEC));
    p.setColor(QPalette::Base, QColor(dark ? 0x1E1E1E : 0xFFFFFF));
    p.setColor(QPalette::Text, QColor(dark ? 0xE3E3E3 : 0x262626));
    p.setColor(QPalette::WindowText, QColor(dark ? 0xE3E3E3 : 0x262626));
    p.setColor(QPalette::PlaceholderText, QColor(dark ? 0x9A9A9A : 0x666666));
    p.setColor(QPalette::Accent, QColor(dark ? 0x4C9AFF : 0x0064D2));
    p.setColor(QPalette::HighlightedText, QColor(0xFFFFFF));
    return p;
}

bool hasPixel(const QImage &image, const QColor &color)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() == 255 && near(pixel, color, 2))
                return true;
        }
    }
    return false;
}

} // namespace

class TestAppTheme : public QObject
{
    Q_OBJECT
private slots:
    void mixFormula();
    void lightValues();
    void darkValues();
    void contrastPairs_data();
    void contrastPairs();
    void contrastEachStyle();
    void tintsHighContrast();
    void translucentPlaceholder();
    void typeScale();
    void captionMinimum();
    void iconsLoad();
    void stateIconUsesColor();
    void toolIconUsesWindowText();
    void tokenValues();
    void fillValues();
    void sharedIconsLoad();
    void tertiaryIconButtonProperty();
    void iconStrokeWidth();
    void surfaceWindow();
    void surfaceEdges();
};

void TestAppTheme::mixFormula()
{
    QColor c = mix(QColor(0xE0A000), QColor(0xFFFFFF), 0.12);
    QVERIFY2(near(c, QColor(0xFBF4E0), 1), qPrintable(describe(c, QColor(0xFBF4E0))));
    c = mix(QColor(0xE0A000), QColor(0x1E1E1E), 0.12);
    QVERIFY2(near(c, QColor(0x352E1A), 1), qPrintable(describe(c, QColor(0x352E1A))));
    c = mix(QColor(0xD03030), QColor(0x1E1E1E), 0.12);
    QVERIFY2(near(c, QColor(0x332020), 1), qPrintable(describe(c, QColor(0x332020))));
}

void TestAppTheme::lightValues()
{
    const QPalette p = referencePalette(false);
    const CtrmColors c = CtrmColors::fromPalette(p);
    QCOMPARE(c.stateError, QColor(0xC42B1C));
    QCOMPARE(c.stateConflict, QColor(0xA15C00));
    QCOMPARE(c.stateWarning, QColor(0xA15C00));
    QCOMPARE(c.stateDone, QColor(0x1E7F34));
    QCOMPARE(c.stateChange, p.color(QPalette::Accent));
    QCOMPARE(c.stateNeutral, p.color(QPalette::PlaceholderText));
    QVERIFY(near(c.tintConflict, QColor(0xFBF4E0), 1));
    QVERIFY(near(c.tintError, QColor(0xF9E6E6), 1));
}

void TestAppTheme::darkValues()
{
    const QPalette p = referencePalette(true);
    const CtrmColors c = CtrmColors::fromPalette(p);
    QCOMPARE(c.stateError, QColor(0xFF6B60));
    QCOMPARE(c.stateConflict, QColor(0xF0B429));
    QCOMPARE(c.stateWarning, QColor(0xF0B429));
    QCOMPARE(c.stateDone, QColor(0x4CC76A));
    QCOMPARE(c.stateChange, p.color(QPalette::Accent));
    QCOMPARE(c.stateNeutral, p.color(QPalette::PlaceholderText));
    QVERIFY(near(c.tintConflict, QColor(0x352E1A), 1));
    QVERIFY(near(c.tintError, QColor(0x332020), 1));
}

void TestAppTheme::contrastPairs_data()
{
    QTest::addColumn<QColor>("fg");
    QTest::addColumn<QColor>("bg");
    QTest::addColumn<double>("reference");

    // design-tokens 2.6, with the reference values of 2.1 to 2.4.
    for (const bool dark : {false, true}) {
        const QPalette p = referencePalette(dark);
        const CtrmColors c = CtrmColors::fromPalette(p);
        const QColor text = p.color(QPalette::Text);
        const QColor base = p.color(QPalette::Base);
        const QColor dim = p.color(QPalette::PlaceholderText);
        const QColor window = p.color(QPalette::Window);
        const QColor accent = p.color(QPalette::Accent);
        const QColor fill(dark ? 0x2D6CDF : 0x0064D2);
        const QColor onAccent = p.color(QPalette::HighlightedText);
        const char *mode = dark ? "dark" : "light";
        auto row = [mode](const char *name, const QColor &fg, const QColor &bg, double ref) {
            QTest::addRow("%s %s", name, mode) << fg << bg << ref;
        };
        row("text on base", text, base, dark ? 13.0 : 15.1);
        row("text on tint-error", text, c.tintError, dark ? 12.0 : 12.6);
        row("text-dim on window", dim, window, dark ? 5.0 : 4.9);
        row("text-dim on tint-error", dim, c.tintError, dark ? 5.5 : 4.8);
        row("accent on base", accent, base, dark ? 5.9 : 5.6);
        row("accent on tint-conflict", accent, c.tintConflict, dark ? 4.7 : 5.1);
        row("on-accent on accent-fill", onAccent, fill, dark ? 4.9 : 5.6);
        row("state-conflict on base", c.stateConflict, base, dark ? 8.9 : 5.2);
        row("state-conflict on tint-conflict", c.stateConflict, c.tintConflict, dark ? 7.2 : 4.7);
        row("state-error on base", c.stateError, base, dark ? 6.0 : 5.7);
        row("state-error on tint-error", c.stateError, c.tintError, dark ? 5.5 : 4.7);
        row("state-done on base", c.stateDone, base, dark ? 7.7 : 5.1);
    }
}

void TestAppTheme::contrastPairs()
{
    QFETCH(QColor, fg);
    QFETCH(QColor, bg);
    QFETCH(double, reference);
    const double ratio = contrastRatio(fg, bg);
    QVERIFY2(ratio >= 4.5, qPrintable(QString::number(ratio)));
    QVERIFY2(std::abs(ratio - reference) <= 0.1, qPrintable(QString::number(ratio)));
    QCOMPARE(contrastRatio(bg, fg), ratio);
}

void TestAppTheme::contrastEachStyle()
{
    const QStringList keys = QStyleFactory::keys();
    QVERIFY(!keys.isEmpty());
    for (const QString &key : keys) {
        const std::unique_ptr<QStyle> style(QStyleFactory::create(key));
        QVERIFY(style);
        const QPalette standard = style->standardPalette();
        const QPalette light = paletteWith(standard, standard.color(QPalette::Base),
                                           standard.color(QPalette::Text));
        const QPalette dark = paletteWith(standard, QColor(0x1E1E1E), QColor(0xE3E3E3));
        for (const QPalette &p : {light, dark}) {
            const CtrmColors c = CtrmColors::fromPalette(p);
            const QColor base = p.color(QPalette::Base);
            const double textOnTint = contrastRatio(p.color(QPalette::Text), c.tintError);
            const double errorOnBase = contrastRatio(c.stateError, base);
            const QString info = key + QStringLiteral(" base ") + base.name();
            QVERIFY2(textOnTint >= 4.5, qPrintable(info + QString::number(textOnTint)));
            QVERIFY2(errorOnBase >= 4.5, qPrintable(info + QString::number(errorOnBase)));
        }
    }
}

void TestAppTheme::tintsHighContrast()
{
    // Pure black and pure white and no known colour scheme: a high-contrast theme.
    QVERIFY(detail::isHighContrast(Qt::ColorScheme::Unknown, Qt::black, Qt::white));
    QVERIFY(detail::isHighContrast(Qt::ColorScheme::Unknown, Qt::white, Qt::black));
    QVERIFY(!detail::isHighContrast(Qt::ColorScheme::Light, Qt::black, Qt::white));
    QVERIFY(!detail::isHighContrast(Qt::ColorScheme::Dark, Qt::white, Qt::black));
    QVERIFY(!detail::isHighContrast(Qt::ColorScheme::Unknown, QColor(0x262626), Qt::white));
    QVERIFY(!detail::isHighContrast(Qt::ColorScheme::Unknown, QColor(0xE3E3E3), QColor(0x1E1E1E)));
}

void TestAppTheme::translucentPlaceholder()
{
    QPalette p = referencePalette(false);
    p.setColor(QPalette::PlaceholderText, QColor(0, 0, 0, 128));
    const CtrmColors c = CtrmColors::fromPalette(p);
    QCOMPARE(c.stateNeutral.alpha(), 255);
    QVERIFY2(near(c.stateNeutral, QColor(0x7F7F7F), 1), qPrintable(c.stateNeutral.name()));

    // A translucent colour on an icon: the whole icon has one alpha, also where strokes overlap.
    CtrmColors translucent = c;
    translucent.stateNeutral = QColor(0, 0, 0, 128);
    const QImage image =
            stateIcon(StateIcon::RolledBack, translucent).pixmap(QSize(16, 16), 2.0).toImage();
    int maxAlpha = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            maxAlpha = std::max(maxAlpha, image.pixelColor(x, y).alpha());
    }
    QVERIFY2(maxAlpha <= 129, qPrintable(QString::number(maxAlpha)));
    QVERIFY(maxAlpha >= 120);
}

void TestAppTheme::typeScale()
{
    QFont base;
    base.setPointSizeF(13.0);
    QCOMPARE(typeStyle(TypeToken::Display, base).pointSizeF(), 20.02);
    QCOMPARE(typeStyle(TypeToken::Display, base).weight(), QFont::Normal);
    QCOMPARE(typeStyle(TypeToken::Heading, base).pointSizeF(), 14.95);
    QCOMPARE(typeStyle(TypeToken::Heading, base).weight(), QFont::DemiBold);
    QCOMPARE(typeStyle(TypeToken::LabelStrong, base).pointSizeF(), 13.0);
    QCOMPARE(typeStyle(TypeToken::LabelStrong, base).weight(), QFont::DemiBold);
    QCOMPARE(typeStyle(TypeToken::Body, base).pointSizeF(), 13.0);
    QCOMPARE(typeStyle(TypeToken::Body, base).weight(), QFont::Normal);
    QCOMPARE(typeStyle(TypeToken::Caption, base).pointSizeF(), 11.05);
    QCOMPARE(typeStyle(TypeToken::Caption, base).weight(), QFont::Normal);

    // A bold base gives the token weight, not bold.
    base.setBold(true);
    QCOMPARE(typeStyle(TypeToken::Body, base).weight(), QFont::Normal);

    const QFont figures = tabularFigures(QFont());
    QCOMPARE(figures.featureValue(QFont::Tag("tnum")), 1u);
}

void TestAppTheme::captionMinimum()
{
    QFont base;
    base.setPointSizeF(9.0);
    QCOMPARE(detail::typeStyle(TypeToken::Caption, base, true).pointSizeF(), 10.0);
    QCOMPARE(detail::typeStyle(TypeToken::Caption, base, false).pointSizeF(), 7.65);
    // The minimum is for captions only.
    QCOMPARE(detail::typeStyle(TypeToken::Body, base, true).pointSizeF(), 9.0);
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    QCOMPARE(typeStyle(TypeToken::Caption, base).pointSizeF(), 10.0);
#else
    QCOMPARE(typeStyle(TypeToken::Caption, base).pointSizeF(), 7.65);
#endif
}

void TestAppTheme::iconsLoad()
{
    const CtrmColors c = CtrmColors::fromPalette(referencePalette(false));
    for (const StateIcon s :
         {StateIcon::Change, StateIcon::Conflict, StateIcon::Error, StateIcon::Warning,
          StateIcon::Done, StateIcon::RolledBack, StateIcon::Excluded}) {
        const QIcon icon = stateIcon(s, c);
        QVERIFY(!icon.isNull());
        const QPixmap pixmap = icon.pixmap(QSize(16, 16), 1.0);
        QVERIFY(!pixmap.isNull());
        QCOMPARE(pixmap.size(), QSize(16, 16));
        QCOMPARE(icon.pixmap(QSize(16, 16), 2.0).size(), QSize(32, 32));
    }
    for (const ToolIcon t :
         {ToolIcon::AddFiles, ToolIcon::AddFolder, ToolIcon::Add, ToolIcon::Remove, ToolIcon::Clear,
          ToolIcon::Close, ToolIcon::Delete, ToolIcon::MoveUp, ToolIcon::MoveDown,
          ToolIcon::Duplicate, ToolIcon::History, ToolIcon::Info, ToolIcon::Presets,
          ToolIcon::Refresh, ToolIcon::Undo}) {
        const QIcon icon = toolIcon(t, referencePalette(false));
        QVERIFY(!icon.isNull());
        const QPixmap pixmap = icon.pixmap(QSize(16, 16), 1.0);
        QVERIFY(!pixmap.isNull());
        QCOMPARE(pixmap.size(), QSize(16, 16));
    }
    QVERIFY(!appIcon().isNull());
    QVERIFY(!appIcon().pixmap(QSize(96, 96)).isNull());
}

void TestAppTheme::stateIconUsesColor()
{
    for (const bool dark : {false, true}) {
        const CtrmColors c = CtrmColors::fromPalette(referencePalette(dark));
        const QImage image = stateIcon(StateIcon::Error, c).pixmap(QSize(16, 16), 1.0).toImage();
        QVERIFY(hasPixel(image, c.stateError));
        const QImage done = stateIcon(StateIcon::Done, c).pixmap(QSize(16, 16), 2.0).toImage();
        QVERIFY(hasPixel(done, c.stateDone));
    }
}

void TestAppTheme::toolIconUsesWindowText()
{
#ifdef Q_OS_LINUX
    QSKIP("Linux uses the theme icon first");
#endif
    QPalette p = referencePalette(false);
    p.setColor(QPalette::WindowText, QColor(0x123456));
    const QImage image = toolIcon(ToolIcon::Delete, p).pixmap(QSize(16, 16), 2.0).toImage();
    QVERIFY(hasPixel(image, QColor(0x123456)));
}

void TestAppTheme::tokenValues()
{
    // design-tokens 4.1 to 4.4 (audit v0.2 section 4).
    namespace t = ctrm::app::tokens;
    QCOMPARE((QList<int>{t::space1, t::space2, t::space3, t::space4, t::space5, t::space6}),
             (QList<int>{4, 8, 12, 16, 20, 24}));
    QCOMPARE((QList<int>{t::controlH, t::controlHLarge, t::rowH, t::rowH2, t::headerH, t::hitMin}),
             (QList<int>{24, 32, 24, 40, 32, 24}));
    QCOMPARE((QList<int>{t::fieldWMax, t::iconXs, t::iconS}), (QList<int>{280, 12, 16}));
    QCOMPARE((QList<int>{t::radiusControl, t::radiusHover, t::radiusPanel}), (QList<int>{6, 5, 8}));
    QCOMPARE((QList<int>{t::defaultWidth, t::defaultHeight}), (QList<int>{1280, 860}));
}

void TestAppTheme::fillValues()
{
    // design-tokens 4.4: text over window at 10/18/26% (dark) and 6/12/18% (light).
    const CtrmColors dark = CtrmColors::fromPalette(referencePalette(true));
    QVERIFY2(near(dark.fillControl, QColor(0x3D3D3F), 1),
             qPrintable(describe(dark.fillControl, QColor(0x3D3D3F))));
    QVERIFY2(near(dark.fillHover, QColor(0x4C4C4E), 1),
             qPrintable(describe(dark.fillHover, QColor(0x4C4C4E))));
    QVERIFY2(near(dark.fillPressed, QColor(0x5B5B5C), 1),
             qPrintable(describe(dark.fillPressed, QColor(0x5B5B5C))));
    const CtrmColors light = CtrmColors::fromPalette(referencePalette(false));
    QVERIFY2(near(light.fillControl, QColor(0xE0E0E0), 1),
             qPrintable(describe(light.fillControl, QColor(0xE0E0E0))));
    QVERIFY2(near(light.fillHover, QColor(0xD4D4D4), 1),
             qPrintable(describe(light.fillHover, QColor(0xD4D4D4))));
    QVERIFY2(near(light.fillPressed, QColor(0xC8C8C8), 1),
             qPrintable(describe(light.fillPressed, QColor(0xC8C8C8))));
}

void TestAppTheme::sharedIconsLoad()
{
    // The icon names of audit v0.2 section 4.
    const QPalette p = referencePalette(false);
    for (const char *name : {"tool-left", "tool-right", "tool-remove"}) {
        const QIcon icon = toolIcon(QString::fromLatin1(name), p);
        QVERIFY2(!icon.isNull(), name);
        QCOMPARE(icon.pixmap(QSize(16, 16), 2.0).size(), QSize(32, 32));
    }
    // The chevrons are drawn on a 12 px canvas (icon-xs): sharp at 1x and 2x.
    for (const char *name : {"chevron-xs-right", "chevron-xs-down"}) {
        const QIcon icon = toolIcon(QString::fromLatin1(name), p);
        QVERIFY2(!icon.isNull(), name);
        QCOMPARE(icon.availableSizes().value(0), QSize(12, 12));
        QCOMPARE(icon.pixmap(QSize(12, 12), 2.0).size(), QSize(24, 24));
    }
    const CtrmColors c = CtrmColors::fromPalette(p);
    const QIcon done = stateIcon(StateIcon::FlowDone, c);
    QVERIFY(!done.isNull());
    const QImage image = done.pixmap(QSize(16, 16), 2.0).toImage();
    QVERIFY(hasPixel(image, c.stateDone));
    QVERIFY(hasPixel(image, QColor(0xFFFFFF)));
}

void TestAppTheme::tertiaryIconButtonProperty()
{
    TertiaryButton button;
    QCOMPARE(button.property("iconButton").toBool(), false);
    QVERIFY(button.setProperty("iconButton", true));
    QVERIFY(button.isIconButton());
    button.setIconButton(false);
    QCOMPARE(button.property("iconButton").toBool(), false);
}

void TestAppTheme::surfaceWindow()
{
    // R5-09 (design-tokens 2.1, surface rule): `window` is the palette Window. When the system
    // gives Window equal or almost equal to Base (macOS light: both white), `window` is `text`
    // mixed 8% over `base`, so the panel and the bars differ from the table.
    QPalette white = referencePalette(false);
    white.setColor(QPalette::Window, QColor(0xFFFFFF));
    white.setColor(QPalette::Text, QColor(0x1D1D1F));
    CtrmColors c = CtrmColors::fromPalette(white);
    QCOMPARE(c.window, mix(QColor(0x1D1D1F), QColor(0xFFFFFF), 0.08));
    QVERIFY2(near(c.window, QColor(0xEEEEEE), 2), qPrintable(c.window.name()));
    // Almost equal (less than 0.03 in relative luminance) counts as equal.
    white.setColor(QPalette::Window, QColor(0xFDFDFD));
    QCOMPARE(CtrmColors::fromPalette(white).window, c.window);
    // The control fills are on `window`, also when the palette Window is white.
    QCOMPARE(c.fillControl, mix(QColor(0x1D1D1F), c.window, 0.06));

    // A palette with a clear difference keeps its Window (light reference). Dark mode always
    // keeps its Window (the dark reference differs from base by less than 0.03 too).
    for (const bool dark : {false, true}) {
        const QPalette p = referencePalette(dark);
        QCOMPARE(CtrmColors::fromPalette(p).window, p.color(QPalette::Window));
    }

    // High contrast: only the palette roles.
    QPalette high;
    high.setColor(QPalette::Window, Qt::white);
    high.setColor(QPalette::Base, Qt::white);
    high.setColor(QPalette::Text, Qt::black);
    high.setColor(QPalette::WindowText, Qt::black);
    c = CtrmColors::fromPalette(high);
    if (c.tintsEnabled)
        QSKIP("The colour scheme of this run is known: not high contrast.");
    QCOMPARE(c.window, QColor(Qt::white));
}

void TestAppTheme::surfaceEdges()
{
    // R5-09: the rule list and the table have no frame; the surface colour shows the edges. In
    // high contrast a 1 px separator (Mid) line shows the given edges.
    QWidget window;
    window.resize(300, 200);
    auto *list = new QListView(&window);
    list->setGeometry(10, 10, 200, 150);
    setSurfaceEdges(list, Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge);
    QCOMPARE(list->frameShape(), QFrame::NoFrame);
    auto *edge = list->findChild<QWidget *>(QStringLiteral("surfaceEdge"));
    QVERIFY(edge);
    QPalette normal = referencePalette(false);
    list->setPalette(normal);
    QVERIFY(edge->isHidden());

    QPalette high;
    high.setColor(QPalette::Window, Qt::white);
    high.setColor(QPalette::Base, Qt::white);
    high.setColor(QPalette::Text, Qt::black);
    high.setColor(QPalette::WindowText, Qt::black);
    high.setColor(QPalette::Mid, QColor(0x808080));
    if (CtrmColors::fromPalette(high).tintsEnabled)
        QSKIP("The colour scheme of this run is known: not high contrast.");
    list->setPalette(high);
    QVERIFY(!edge->isHidden());
    QCOMPARE(edge->geometry(), list->rect());
    QImage image(list->size(), QImage::Format_ARGB32);
    image.fill(Qt::white);
    edge->render(&image, QPoint(), QRegion(), QWidget::DrawWindowBackground);
    QCOMPARE(image.pixelColor(0, 50), QColor(0x808080));
    QCOMPARE(image.pixelColor(199, 50), QColor(0x808080));
    QCOMPARE(image.pixelColor(100, 0), QColor(0x808080));
    QCOMPARE(image.pixelColor(100, 149), QColor(0x808080));
    QCOMPARE(image.pixelColor(100, 50), QColor(Qt::white)); // transparent inside

    // The table: the left edge only.
    auto *table = new QTableView(&window);
    table->setGeometry(0, 0, 200, 150);
    setSurfaceEdges(table, Qt::LeftEdge);
    table->setPalette(high);
    auto *tableEdge = table->findChild<QWidget *>(QStringLiteral("surfaceEdge"));
    QVERIFY(tableEdge && !tableEdge->isHidden());
    image.fill(Qt::white);
    tableEdge->render(&image, QPoint(), QRegion(), QWidget::DrawWindowBackground);
    QCOMPARE(image.pixelColor(0, 50), QColor(0x808080));
    QCOMPARE(image.pixelColor(199, 50), QColor(Qt::white));
    QCOMPARE(image.pixelColor(100, 0), QColor(Qt::white));
}

QTEST_MAIN(TestAppTheme)
void TestAppTheme::iconStrokeWidth()
{
    // C01: one icon set, each stroke 1.5 px (design-tokens 5).
    // The first icon loads the icon resources (a static library).
    QVERIFY(!toolIcon(QStringLiteral("tool-add"), referencePalette(false)).isNull());
    const QDir dir(QStringLiteral(":/icons"));
    const QStringList files =
            dir.entryList({QStringLiteral("tool-*.svg"), QStringLiteral("chevron-*.svg")});
    QCOMPARE(files.size(), 19); // 17 tool icons and 2 chevrons
    static const QRegularExpression width(QStringLiteral("stroke-width=\"([^\"]*)\""));
    for (const QString &name : files) {
        QFile file(dir.filePath(name));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(name));
        const QString svg = QString::fromUtf8(file.readAll());
        // A stroke with no width is 1 px: each stroked icon names its width.
        if (svg.contains(QStringLiteral("stroke=\"")))
            QVERIFY2(svg.contains(QStringLiteral("stroke-width=")), qPrintable(name));
        for (auto it = width.globalMatch(svg); it.hasNext();) {
            const QString value = it.next().captured(1);
            QVERIFY2(value == QStringLiteral("1.5"), qPrintable(name + QLatin1Char(' ') + value));
        }
    }
}

#include "tst_app_theme.moc"
