// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/flowstrip.h"

#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/typography.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QToolButton>

namespace ctrm::app {

namespace {

const char *const kStepNames[4] = {
        QT_TRANSLATE_NOOP("ctrm::app::FlowStrip", "Add files"),
        QT_TRANSLATE_NOOP("ctrm::app::FlowStrip", "Add rules"),
        QT_TRANSLATE_NOOP("ctrm::app::FlowStrip", "Check preview"),
        QT_TRANSLATE_NOOP("ctrm::app::FlowStrip", "Rename"),
};

// The step number in a circle. Current: filled with the accent colour. Later: a dimmed number.
QIcon numberIcon(int number, bool current, const QPalette &p, const QFont &font, int size)
{
    QIcon icon;
    for (const qreal dpr : {1.0, 1.5, 2.0}) {
        QPixmap pixmap(QSize(size, size) * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF circle = QRectF(0, 0, size, size).adjusted(0.5, 0.5, -0.5, -0.5);
        if (current) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(p.color(QPalette::Accent));
            painter.drawEllipse(circle);
            painter.setPen(p.color(QPalette::HighlightedText));
        } else {
            painter.setPen(QPen(p.color(QPalette::PlaceholderText), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(circle);
        }
        QFont f = font;
        f.setPixelSize(qMax(6, size * 2 / 3));
        f.setWeight(QFont::DemiBold);
        painter.setFont(f);
        painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, QString::number(number));
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace

FlowStrip::FlowStrip(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("flowStrip"));
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *layout = new QHBoxLayout(this);
    layout->setSpacing(0);
    for (int i = 0; i < 4; ++i) {
        if (i > 0) {
            auto *separator = new QLabel(QStringLiteral("›"), this);
            separator->setForegroundRole(QPalette::PlaceholderText);
            separator->setAccessibleName(QString());
            layout->addWidget(separator);
        }
        auto *button = new QToolButton(this);
        button->setObjectName(QStringLiteral("flowStep%1").arg(i + 1));
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFocusPolicy(Qt::TabFocus);
        connect(button, &QToolButton::clicked, this, [this, i] { emit stepClicked(i); });
        layout->addWidget(button);
        m_buttons[static_cast<size_t>(i)] = button;
    }
    layout->addStretch(1);
    setFocusProxy(m_buttons[0]);
    updateSteps();
}

void FlowStrip::setState(const WindowStateInfo &info)
{
    m_done = info.stepDone;
    m_current = info.currentStep;
    updateSteps();
}

void FlowStrip::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::FontChange:
    case QEvent::StyleChange:
    case QEvent::ApplicationPaletteChange:
        updateSteps();
        break;
    default:
        break;
    }
}

void FlowStrip::updateSteps()
{
    // Setting the font and palette of the buttons must not start this again.
    if (m_updating)
        return;
    m_updating = true;
    const QPalette p = palette();
    const CtrmColors colors = CtrmColors::fromPalette(p);
    const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    for (int i = 0; i < 4; ++i) {
        QToolButton *button = m_buttons[static_cast<size_t>(i)];
        const bool done = m_done[static_cast<size_t>(i)];
        const bool current = !done && i == m_current;
        const QString name = tr(kStepNames[i]);
        button->setText(name);
        button->setIconSize(QSize(iconSize, iconSize));

        // Only the roles set here; the rest come from the strip.
        QPalette buttonPalette;
        QFont font = typeStyle(current ? TypeToken::LabelStrong : TypeToken::Body, this->font());
        QString look;
        if (done) {
            button->setIcon(stateIcon(StateIcon::Done, colors));
            look = tr("done");
        } else if (current) {
            button->setIcon(numberIcon(i + 1, true, p, font, iconSize));
            look = tr("current");
        } else {
            button->setIcon(numberIcon(i + 1, false, p, font, iconSize));
            const QColor dim = p.color(QPalette::PlaceholderText);
            buttonPalette.setColor(QPalette::ButtonText, dim);
            buttonPalette.setColor(QPalette::WindowText, dim);
            look = tr("later");
        }
        button->setPalette(buttonPalette);
        button->setFont(font);
        button->setAccessibleName(
                tr("Step %1 of 4, %2, %3").arg(QString::number(i + 1), name, look));
    }
    m_updating = false;
}

} // namespace ctrm::app
