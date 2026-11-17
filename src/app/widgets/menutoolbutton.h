// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QToolButton>

class QStyleOptionToolButton;

namespace ctrm::app {

// A tool button with a menu whose arrow is inside the button, after the text, on the text line
// (ui-design 5.1, D5). Styles such as macOS draw the menu arrow of a tool button below and to
// the right of the text.
class MenuToolButton : public QToolButton
{
    Q_OBJECT
public:
    explicit MenuToolButton(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    // The arrow, in widget coordinates.
    QRect arrowRect() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void initOption(QStyleOptionToolButton *option) const; // without the menu features
    QRect labelRect(const QStyleOptionToolButton &option) const;
    int arrowSize() const;
    int arrowGap() const;
};

// The body font: a toolbar can give its buttons a smaller font (macOS).
void useBodyFont(QWidget *widget);

} // namespace ctrm::app
