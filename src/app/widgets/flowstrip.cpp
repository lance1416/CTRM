// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/flowstrip.h"

#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/tokens.h"
#include "theme/typography.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>

namespace ctrm::app {

namespace {

const char *const kStepNames[] = {
        QT_TRANSLATE_NOOP("ctrm::app::FlowStrip", "Add files"),
        QT_TRANSLATE_NOOP("ctrm::app::FlowStrip", "Add rules"),
        QT_TRANSLATE_NOOP("ctrm::app::FlowStrip", "Rename"),
};

// The step number in a circle at the given device pixel ratio. Current: filled with the accent
// colour. Later: an outline and a dim number.
QPixmap numberPixmap(int number, bool current, const QPalette &p, const QFont &font, int size,
                     qreal dpr)
{
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
    return pixmap;
}

// One element without children: the step labels are not separate stops for a screen reader.
class FlowStripAccessible : public QAccessibleWidget
{
public:
    explicit FlowStripAccessible(QWidget *widget)
        : QAccessibleWidget(widget, QAccessible::Grouping)
    {}
    int childCount() const override { return 0; }
    QAccessibleInterface *child(int) const override { return nullptr; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
    QAccessibleInterface *childAt(int, int) const override { return nullptr; }
};

QAccessibleInterface *flowStripFactory(const QString &className, QObject *object)
{
    if (className == QLatin1StringView("ctrm::app::FlowStrip") && object && object->isWidgetType())
        return new FlowStripAccessible(static_cast<QWidget *>(object));
    return nullptr;
}

QLabel *passiveLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setFocusPolicy(Qt::NoFocus);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    return label;
}

} // namespace

FlowStrip::FlowStrip(QWidget *parent)
    : QWidget(parent)
{
    static const bool factoryInstalled = [] {
        QAccessible::installFactory(flowStripFactory);
        return true;
    }();
    Q_UNUSED(factoryInstalled);

    setObjectName(QStringLiteral("flowStrip"));
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setAccessibleName(tr("Steps"));

    auto *layout = new QHBoxLayout(this);
    // 5.3, design-tokens 4.2: inset space-3 x space-1; icon to text space-1; space-2 on each
    // side of a chevron.
    layout->setContentsMargins(tokens::space3, tokens::space1, tokens::space3, tokens::space1);
    layout->setSpacing(0);
    for (int i = 0; i < kSteps; ++i) {
        const auto index = static_cast<size_t>(i);
        if (i > 0) {
            QLabel *separator = passiveLabel(this);
            separator->setObjectName(QStringLiteral("flowSeparator"));
            separator->setFixedWidth(tokens::iconXs);
            separator->setAlignment(Qt::AlignCenter);
            layout->addSpacing(tokens::space2);
            layout->addWidget(separator);
            layout->addSpacing(tokens::space2);
            m_separators[index - 1] = separator;
        }
        QLabel *icon = passiveLabel(this);
        icon->setObjectName(QStringLiteral("flowStep%1Icon").arg(i + 1));
        icon->setFixedWidth(tokens::iconS);
        icon->setAlignment(Qt::AlignCenter);
        QLabel *text = passiveLabel(this);
        text->setObjectName(QStringLiteral("flowStep%1").arg(i + 1));
        layout->addWidget(icon);
        layout->addSpacing(tokens::space1);
        layout->addWidget(text);
        m_icons[index] = icon;
        m_texts[index] = text;
    }
    layout->addStretch(1);
    updateSteps();
}

void FlowStrip::setState(const WindowStateInfo &info)
{
    const QString before = stepDescription();
    m_done = info.stepDone;
    m_current = info.currentStep;
    updateSteps();
    if (stepDescription() != before) {
        QAccessibleEvent description(this, QAccessible::DescriptionChanged);
        QAccessible::updateAccessibility(&description);
        QAccessibleEvent name(this, QAccessible::NameChanged);
        QAccessible::updateAccessibility(&name);
    }
}

void FlowStrip::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::FontChange:
    case QEvent::StyleChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::DevicePixelRatioChange:
        updateSteps();
        break;
    default:
        break;
    }
}

QString FlowStrip::stepDescription() const
{
    // The current step; when all steps are done, the last step.
    const int step = m_current >= 0 && m_current < kSteps ? m_current : kSteps - 1;
    const bool done = m_done[static_cast<size_t>(step)];
    return tr("Step %1 of %2, %3, %4")
            .arg(QString::number(step + 1), QString::number(kSteps), tr(kStepNames[step]),
                 done ? tr("done") : tr("current"));
}

void FlowStrip::updateSteps()
{
    // Setting the font and palette of the labels must not start this again.
    if (m_updating)
        return;
    m_updating = true;
    const QPalette p = palette();
    const CtrmColors colors = CtrmColors::fromPalette(p);
    const qreal dpr = devicePixelRatioF();
    const QColor dim = p.color(QPalette::PlaceholderText);
    const QSize iconSize(tokens::iconS, tokens::iconS);

    QPalette dimPalette = p;
    dimPalette.setColor(QPalette::WindowText, dim);
    const QPixmap chevron = toolIcon(QStringLiteral("chevron-xs-right"), dimPalette)
                                    .pixmap(QSize(tokens::iconXs, tokens::iconXs), dpr);
    for (QLabel *separator : m_separators)
        separator->setPixmap(chevron);

    for (int i = 0; i < kSteps; ++i) {
        const auto index = static_cast<size_t>(i);
        QLabel *text = m_texts[index];
        const bool done = m_done[index];
        const bool current = !done && i == m_current;
        text->setText(tr(kStepNames[i]));

        // Only the roles set here; the rest come from the strip.
        QPalette textPalette;
        const QFont font =
                typeStyle(current ? TypeToken::LabelStrong : TypeToken::Body, this->font());
        QPixmap icon;
        if (done)
            icon = stateIcon(StateIcon::FlowDone, colors).pixmap(iconSize, dpr);
        else
            icon = numberPixmap(i + 1, current, p, font, tokens::iconS, dpr);
        if (!done && !current)
            textPalette.setColor(QPalette::WindowText, dim);
        m_icons[index]->setPixmap(icon);
        text->setPalette(textPalette);
        text->setFont(font);
    }
    setAccessibleDescription(stepDescription());
    m_updating = false;
}

} // namespace ctrm::app
