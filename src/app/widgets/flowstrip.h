// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/windowstate.h"

#include <QWidget>

#include <array>

class QToolButton;

namespace ctrm::app {

// The three flow steps under the toolbar (ui-design 5.3, S01). It only shows where the user is.
class FlowStrip : public QWidget
{
    Q_OBJECT
public:
    explicit FlowStrip(QWidget *parent);
    void setState(const WindowStateInfo &info);

signals:
    void stepClicked(int step); // 0..2

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateSteps();

    static constexpr int kSteps = 3;
    std::array<QToolButton *, kSteps> m_buttons{};
    std::array<bool, kSteps> m_done{};
    int m_current = 0;
    bool m_updating = false;
};

} // namespace ctrm::app
