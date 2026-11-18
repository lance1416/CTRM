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
    p.setColor(QPalette::WindowText, QColor(0x1D1D1F));
    p.setColor(QPalette::ButtonText, QColor(0x1D1D1F));
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
    p.setColor(QPalette::WindowText, QColor(0xE3E3E3));
    p.setColor(QPalette::ButtonText, QColor(0xE3E3E3));
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
    // C15: `text`, never `accent`.
    QCOMPARE(text, palette.color(QPalette::WindowText));
    QVERIFY2(contrastRatio(text, tint) >= 4.5,
             qPrintable(text.name() + QStringLiteral(" on ") + tint.name()));
    // Disabled: text-dim.
    QCOMPARE(button->palette().color(QPalette::Disabled, QPalette::ButtonText),
             colors.stateNeutral);
    // label-strong: semibold.
    QCOMPARE(button->font().weight(), QFont::DemiBold);
}

QTEST_MAIN(TestAppMessageBar)
#include "tst_app_messagebar.moc"
