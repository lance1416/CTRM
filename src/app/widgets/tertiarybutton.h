// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QToolButton>

namespace ctrm::app {

// A tertiary button (ui-design 5.1): no frame until hover, press or keyboard focus. The macOS
// style draws a bevel on each QToolButton that is not in a QToolBar, also with autoRaise, so
// this button draws only its label in the other states.
class TertiaryButton : public QToolButton
{
    Q_OBJECT
public:
    explicit TertiaryButton(QWidget *parent = nullptr);

    bool framed() const; // the frame shows now

protected:
    void paintEvent(QPaintEvent *event) override;
};

} // namespace ctrm::app
