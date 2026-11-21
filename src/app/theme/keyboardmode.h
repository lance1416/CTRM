// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

class QPainter;
class QStyleOptionViewItem;
class QWidget;

namespace ctrm::app {

// The app keyboard mode (ui-design 14 "Focus ring", O11). Tab, Shift+Tab or a key that moves the
// focus turns it on: an arrow key in a button group, or a shortcut key whose action gives the
// focus with Qt::ShortcutFocusReason. A mouse press or a touch turns it off. A focus change by
// the program does not change it, also with a Tab or Shortcut reason: that reason counts only in
// the event cycle of a focus key (R8-01).
//
// While it is on, each window has Qt::WA_KeyboardFocusChange, so the style options have
// QStyle::State_KeyboardFocusChange. The text entry controls show the focus ring always; the other
// controls show it only in keyboard mode. CtrmStyle makes the object; a widget that paints its own
// focus ring reads isOn().
class KeyboardMode : public QObject
{
    Q_OBJECT
public:
    // The one object of the application. It needs a QApplication.
    static KeyboardMode *instance();
    static bool isOn();

    // Sets the mode and repaints the focus widget. The event filter calls it; tests can too.
    void setOn(bool on);

    // A line edit, a spin box or an editable combo box: it shows the ring after a click too.
    static bool isTextEntry(const QWidget *widget);
    // Whether widget shows its focus ring when it has the focus: a text entry always, an item
    // view (or its viewport) never, all other controls only in keyboard mode.
    static bool showsRing(const QWidget *widget);

signals:
    void changed(bool on);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit KeyboardMode(QObject *parent);
    // A focus key (Tab, an arrow, a shortcut) starts: until the event loop runs again, a focus
    // change with a Tab, Backtab or Shortcut reason turns the mode on.
    void startFocusKey();

    bool m_on = false;
    bool m_focusKey = false;
    bool m_focusKeyEndQueued = false;
};

// The current row of an item view in keyboard mode, when the view has the focus and the row is
// not selected: a 1 px accent outline inside the row (ui-design 14 rule 4). The cell of option
// gets its part of the outline. Does nothing for an option with no valid index.
void drawCurrentRowOutline(QPainter *painter, const QStyleOptionViewItem &option);

} // namespace ctrm::app
