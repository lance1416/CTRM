// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/messagebar.h"

#include "theme/colors.h"
#include "theme/tokens.h"
#include "widgets/tertiarybutton.h"

#include <QAccessible>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

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
    case StateIcon::FlowDone:
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
    , m_close(new TertiaryButton(this))
{
    setObjectName(QStringLiteral("messageBar"));
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setAccessibleName(tr("Message"));

    m_icon->setObjectName(QStringLiteral("messageBarIcon"));
    m_text->setObjectName(QStringLiteral("messageBarText"));
    m_text->setTextFormat(Qt::PlainText);
    m_text->setWordWrap(true);
    m_text->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_close->setObjectName(QStringLiteral("messageBarClose"));
    m_close->setAutoRaise(true);
    m_close->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // C07: an icon button of hit-min with an icon-s icon.
    m_close->setIconButton(true);
    m_close->setFixedSize(tokens::hitMin, tokens::hitMin);
    m_close->setIconSize(QSize(tokens::iconS, tokens::iconS));
    m_close->setToolTip(tr("Close"));
    m_close->setAccessibleName(tr("Close message"));
    connect(m_close, &QToolButton::clicked, this, [this] {
        clear();
        emit closed();
    });

    // M11, O07: one centre line, inset space-3 x space-2, gap space-2, a separator under it.
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);
    m_buttonLayout->setSpacing(tokens::space2);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(tokens::space3, tokens::space2, tokens::space3, tokens::space2);
    row->setSpacing(tokens::space2);
    row->addWidget(m_icon, 0, Qt::AlignVCenter);
    row->addWidget(m_text, 1);
    row->addLayout(m_buttonLayout);
    row->addWidget(m_close, 0, Qt::AlignVCenter);
    auto *separator = new QFrame(this);
    separator->setObjectName(QStringLiteral("messageBarSeparator"));
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setForegroundRole(QPalette::Mid);
    separator->setFixedHeight(1);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(row);
    layout->addWidget(separator);

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
        // C15: a light action, as the macOS banner actions: a borderless button with
        // label-strong text, never default.
        button->setAutoDefault(false);
        button->setDefault(false);
        button->setFlat(true);
        QFont strong = button->font();
        strong.setWeight(QFont::DemiBold);
        button->setFont(strong);
        // Into the bar first: a button with no parent shows as its own window and takes the
        // activation from the main window (M01).
        m_buttonLayout->addWidget(button);
        button->show();
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
    // A high-contrast theme has no tints (design-tokens 2.6): the palette only.
    if (colors.tintsEnabled) {
        QPalette tint;
        tint.setColor(QPalette::Window,
                      mix(stateColor(m_stateIcon, colors), base.color(QPalette::Base), 0.12));
        setPalette(tint);
    }
    setAutoFillBackground(colors.tintsEnabled);

    // design-tokens 5: the icon of the set, not the style pixmap. R5-07: a plain x (tool-close);
    // tool-clear (x in a circle) is only for clearing a field.
    m_close->setIcon(toolIcon(ToolIcon::Close, base));
    m_icon->setPixmap(stateIcon(m_stateIcon, colors)
                              .pixmap(QSize(tokens::iconS, tokens::iconS), devicePixelRatioF()));
    for (QPushButton *button : std::as_const(m_buttons)) {
        if (!button)
            continue;
        // design-tokens 4.5 "Message bar action": `text`, never `accent` on a tint; disabled
        // `text-dim`.
        QPalette action;
        action.setColor(QPalette::ButtonText, base.color(QPalette::Text));
        action.setColor(QPalette::Disabled, QPalette::ButtonText, colors.stateNeutral);
        button->setPalette(action);
    }
    m_updating = false;
}

} // namespace ctrm::app
