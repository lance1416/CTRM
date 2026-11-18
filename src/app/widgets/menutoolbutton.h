// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QToolButton>

class QStyleOptionToolButton;

namespace ctrm::app {

// A tool button with a menu whose chevron is inside the button, after the text, on the text line
// (ui-design 5.1, D5, M17). Styles such as macOS draw the menu arrow of a tool button below and
// to the right of the text. The style draws the bevel (the toolbar hover and pressed fills); the
// button draws its label with the toolbar text button layout of design-tokens 4.5: space-2,
// icon-s icon, space-1, text, space-1, the icon-xs chevron-xs-down, space-2.
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
    bool iconShown(const QStyleOptionToolButton &option) const;
    int contentWidth(const QStyleOptionToolButton &option) const; // icon, text and chevron
    int contentLeft(const QStyleOptionToolButton &option) const;
};

// The body font: a toolbar can give its buttons a smaller font (macOS).
void useBodyFont(QWidget *widget);

} // namespace ctrm::app
