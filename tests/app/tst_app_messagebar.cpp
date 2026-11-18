// SPDX-License-Identifier: GPL-3.0-or-later
//
// The message bar actions (design-tokens 4.5 "Message bar action", audit v0.2 C15): text in
// `text`, `label-strong`, with 4.5:1 on each tint in light and dark mode.

#include "theme/colors.h"
#include "theme/icons.h"
#include "widgets/messagebar.h"

#include <QPushButton>
#include <QtTest>

using namespace ctrm::app;

namespace {

QPalette lightPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(0xECECEC));
    p.setColor(QPalette::Base, QColor(0xFFFFFF));
    p.setColor(QPalette::Button, QColor(0xFFFFFF));
    p.setColor(QPalette::Text, QColor(0x1D1D1F));
    // WindowText and ButtonText differ from Text, so the test sees which role is used.
    p.setColor(QPalette::WindowText, QColor(0x3C3C3E));
    p.setColor(QPalette::ButtonText, QColor(0x2C2C2E));
    p.setColor(QPalette::PlaceholderText, QColor(0x8A8A8E));
    p.setColor(QPalette::Accent, QColor(0x007AFF));
    p.setColor(QPalette::Highlight, QColor(0x007AFF));
    p.setColor(QPalette::HighlightedText, QColor(0xFFFFFF));
    return p;
}

QPalette darkPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x2B2B2D));
    p.setColor(QPalette::Base, QColor(0x1E1E1E));
    p.setColor(QPalette::Button, QColor(0x2B2B2D));
    p.setColor(QPalette::Text, QColor(0xE3E3E3));
    p.setColor(QPalette::WindowText, QColor(0xD0D0D0));
    p.setColor(QPalette::ButtonText, QColor(0xC8C8C8));
    p.setColor(QPalette::PlaceholderText, QColor(0x9A9A9A));
    p.setColor(QPalette::Accent, QColor(0x4C9AFF));
    p.setColor(QPalette::Highlight, QColor(0x4C9AFF));
    p.setColor(QPalette::HighlightedText, QColor(0xFFFFFF));
    return p;
}

} // namespace

class TestAppMessageBar : public QObject
{
    Q_OBJECT
private slots:
    void actionText_data();
    void actionText();
    void highContrastNoTint();
};

void TestAppMessageBar::actionText_data()
{
    QTest::addColumn<QPalette>("palette");
    QTest::addColumn<StateIcon>("icon");
    const auto add = [](const char *name, StateIcon icon) {
        QTest::addRow("light %s", name) << lightPalette() << icon;
        QTest::addRow("dark %s", name) << darkPalette() << icon;
    };
    add("done", StateIcon::Done);
    add("error", StateIcon::Error);
    add("conflict", StateIcon::Conflict);
    add("warning", StateIcon::Warning);
    add("change", StateIcon::Change);
    add("rolled back", StateIcon::RolledBack);
}

void TestAppMessageBar::actionText()
{
    QFETCH(QPalette, palette);
    QFETCH(StateIcon, icon);
    QWidget window;
    window.setPalette(palette);
    auto *bar = new MessageBar(&window);
    auto *button = new QPushButton(QStringLiteral("Undo"));
    bar->showMessage(icon, QStringLiteral("Done."), {button});

    const CtrmColors colors = CtrmColors::fromPalette(palette);
    const QColor tint = bar->palette().color(QPalette::Window);
    const QColor text = button->palette().color(QPalette::Active, QPalette::ButtonText);
    // C15: `text` (the Text role), never `accent`; the same in an inactive window.
    QCOMPARE(text, palette.color(QPalette::Text));
    QCOMPARE(button->palette().color(QPalette::Inactive, QPalette::ButtonText), text);
    QVERIFY2(contrastRatio(text, tint) >= 4.5,
             qPrintable(text.name() + QStringLiteral(" on ") + tint.name()));
    // Disabled: text-dim.
    QCOMPARE(button->palette().color(QPalette::Disabled, QPalette::ButtonText),
             colors.stateNeutral);
    // label-strong: semibold.
    QCOMPARE(button->font().weight(), QFont::DemiBold);
    // A new message with the same button keeps the look.
    bar->showMessage(icon, QStringLiteral("Again."), {button});
    QCOMPARE(button->palette().color(QPalette::Active, QPalette::ButtonText), text);
    QCOMPARE(button->font().weight(), QFont::DemiBold);
}

void TestAppMessageBar::highContrastNoTint()
{
    // design-tokens 2.6: a high-contrast theme has no tint; the action is still `text`.
    QPalette palette = lightPalette();
    palette.setColor(QPalette::Text, Qt::black);
    palette.setColor(QPalette::Base, Qt::white);
    QWidget window;
    window.setPalette(palette);
    auto *bar = new MessageBar(&window);
    auto *button = new QPushButton(QStringLiteral("Undo"));
    bar->showMessage(StateIcon::Done, QStringLiteral("Done."), {button});
    if (CtrmColors::fromPalette(palette).tintsEnabled)
        QSKIP("The colour scheme of this run is known: not high contrast.");
    QVERIFY(!bar->autoFillBackground());
    QCOMPARE(button->palette().color(QPalette::Active, QPalette::ButtonText), QColor(Qt::black));
}

QTEST_MAIN(TestAppMessageBar)
#include "tst_app_messagebar.moc"
