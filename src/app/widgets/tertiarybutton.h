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
    // The icon-button look (design-tokens 4.5): a hit-min square, the icon in text-dim at rest.
    // false: the borderless-button look (flow step, link-style button).
    Q_PROPERTY(bool iconButton READ isIconButton WRITE setIconButton)
public:
    explicit TertiaryButton(QWidget *parent = nullptr);

    bool isIconButton() const;
    void setIconButton(bool on);

    bool framed() const; // the frame shows now

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_iconButton = false;
};

} // namespace ctrm::app
