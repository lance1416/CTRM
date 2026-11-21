// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "theme/icons.h"

#include <QFrame>
#include <QList>

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace ctrm::app {

class TertiaryButton;

// A message above the table or the action bar (ui-design 9.3, 9.8): icon, text, buttons and a
// close button. It is hidden until showMessage.
class MessageBar : public QFrame
{
    Q_OBJECT
public:
    explicit MessageBar(QWidget *parent);
    // The bar takes the buttons. The text is plain text. A new message deletes the old buttons.
    void showMessage(StateIcon icon, const QString &text, const QList<QPushButton *> &buttons);
    // A neutral short message (ui-design 9.13): the tool-info icon in text-dim, no tint, no
    // buttons.
    void showNote(const QString &text);
    bool isNote() const { return m_note; }
    void clear(); // hides the bar; does not emit closed()

signals:
    void closed(); // the user clicked the close button

protected:
    void changeEvent(QEvent *event) override;

private:
    void removeButtons();
    void updateColors();

    QLabel *m_icon;
    QLabel *m_text;
    QHBoxLayout *m_buttonLayout;
    TertiaryButton *m_close;
    QList<QPushButton *> m_buttons;
    StateIcon m_stateIcon = StateIcon::Done;
    bool m_note = false;
    bool m_updating = false;
};

} // namespace ctrm::app
