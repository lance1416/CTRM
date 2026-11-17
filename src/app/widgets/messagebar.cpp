// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/messagebar.h"

#include "theme/colors.h"

#include <QAccessible>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>

namespace ctrm::app {

namespace {

QColor stateColor(StateIcon icon, const CtrmColors &c)
{
    switch (icon) {
    case StateIcon::Change:
        return c.stateChange;
    case StateIcon::Conflict:
    case StateIcon::Warning:
        return c.stateConflict;
    case StateIcon::Error:
        return c.stateError;
    case StateIcon::Done:
        return c.stateDone;
    case StateIcon::RolledBack:
    case StateIcon::Excluded:
        return c.stateNeutral;
    }
    return c.stateNeutral;
}

} // namespace

MessageBar::MessageBar(QWidget *parent)
    : QFrame(parent)
    , m_icon(new QLabel(this))
    , m_text(new QLabel(this))
    , m_buttonLayout(new QHBoxLayout)
    , m_close(new QToolButton(this))
{
    setObjectName(QStringLiteral("messageBar"));
    setFrameShape(QFrame::StyledPanel);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setAccessibleName(tr("Message"));

    m_icon->setObjectName(QStringLiteral("messageBarIcon"));
    m_text->setObjectName(QStringLiteral("messageBarText"));
    m_text->setTextFormat(Qt::PlainText);
    m_text->setWordWrap(true);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_close->setObjectName(QStringLiteral("messageBarClose"));
    m_close->setAutoRaise(true);
    m_close->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_close->setToolTip(tr("Close"));
    m_close->setAccessibleName(tr("Close message"));
    connect(m_close, &QToolButton::clicked, this, [this] {
        clear();
        emit closed();
    });

    m_buttonLayout->setContentsMargins(0, 0, 0, 0);
    auto *layout = new QHBoxLayout(this);
    layout->addWidget(m_icon, 0, Qt::AlignTop);
    layout->addWidget(m_text, 1);
    layout->addLayout(m_buttonLayout);
    layout->addWidget(m_close, 0, Qt::AlignTop);

    hide();
}

void MessageBar::showMessage(StateIcon icon, const QString &text,
                             const QList<QPushButton *> &buttons)
{
    // A button that is also in the new message stays.
    for (QPushButton *button : buttons)
        m_buttons.removeAll(button);
    removeButtons();
    m_stateIcon = icon;
    m_text->setText(text);
    for (QPushButton *button : buttons) {
        // Secondary buttons (ui-design 5.1): never default.
        button->setAutoDefault(false);
        button->setDefault(false);
        button->show();
        m_buttonLayout->addWidget(button);
        m_buttons.append(button);
    }
    updateColors();
    show();
    QAccessibleEvent event(this, QAccessible::Alert);
    QAccessible::updateAccessibility(&event);
}

void MessageBar::clear()
{
    hide();
    removeButtons();
    m_text->clear();
}

void MessageBar::removeButtons()
{
    for (QPushButton *button : std::as_const(m_buttons)) {
        if (button) {
            button->hide();
            button->deleteLater();
        }
    }
    m_buttons.clear();
}

void MessageBar::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        updateColors();
}

void MessageBar::updateColors()
{
    // Setting the palette sends a PaletteChange; do not start again.
    if (m_updating)
        return;
    m_updating = true;
    // Only Window is set, so the parent palette still gives the other roles.
    setPalette(QPalette());
    const QPalette base = palette();
    const CtrmColors colors = CtrmColors::fromPalette(base);
    QPalette tint;
    tint.setColor(QPalette::Window,
                  mix(stateColor(m_stateIcon, colors), base.color(QPalette::Base), 0.12));
    setPalette(tint);
    setAutoFillBackground(true);

    m_close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton, nullptr, this));
    const int size = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    m_icon->setPixmap(
            stateIcon(m_stateIcon, colors).pixmap(QSize(size, size), devicePixelRatioF()));
    m_updating = false;
}

} // namespace ctrm::app
