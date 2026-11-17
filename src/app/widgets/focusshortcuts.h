// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QKeySequence>
#include <QList>
#include <QObject>

#include <utility>

class QAction;
class QWidget;

namespace ctrm::app {

// Gives actions their shortcuts only while one widget has the focus (M28, R1-02). On macOS a menu
// shortcut is a native key equivalent that works for any focus widget, so a plain key such as
// Delete or Space must not stay on a menu action. An open popup menu does not clear them.
class FocusShortcuts : public QObject
{
    Q_OBJECT
public:
    using Binding = std::pair<QAction *, QList<QKeySequence>>;
    FocusShortcuts(QWidget *widget, QList<Binding> bindings);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void apply(bool on);

    QList<Binding> m_bindings;
};

} // namespace ctrm::app
