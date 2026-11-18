// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QToolButton>

namespace ctrm::app {

// A tertiary button (ui-design 5.1, design-tokens 4.5): no fill at rest, fill-hover on hover,
// fill-pressed when pressed, a focus ring on keyboard focus. It never draws the frame of the
// platform style (audit O03, C02).
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

    bool framed() const; // the fill or the focus ring shows now

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_iconButton = false;
};

} // namespace ctrm::app
