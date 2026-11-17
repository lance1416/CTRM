// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/focusshortcuts.h"

#include <QAction>
#include <QFocusEvent>
#include <QWidget>

namespace ctrm::app {

FocusShortcuts::FocusShortcuts(QWidget *widget, QList<Binding> bindings)
    : QObject(widget)
    , m_bindings(std::move(bindings))
{
    widget->installEventFilter(this);
    apply(widget->hasFocus());
}

bool FocusShortcuts::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
        apply(true);
    else if (event->type() == QEvent::FocusOut &&
             static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason)
        apply(false);
    return QObject::eventFilter(watched, event);
}

void FocusShortcuts::apply(bool on)
{
    for (const Binding &binding : std::as_const(m_bindings))
        binding.first->setShortcuts(on ? binding.second : QList<QKeySequence>{});
}

} // namespace ctrm::app
