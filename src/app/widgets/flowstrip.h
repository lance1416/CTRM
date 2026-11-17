// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/windowstate.h"

#include <QWidget>

#include <array>

class QToolButton;

namespace ctrm::app {

// The four flow steps under the toolbar (ui-design 5.3). It only shows where the user is.
class FlowStrip : public QWidget
{
    Q_OBJECT
public:
    explicit FlowStrip(QWidget *parent);
    void setState(const WindowStateInfo &info);

signals:
    void stepClicked(int step); // 0..3

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateSteps();

    std::array<QToolButton *, 4> m_buttons{};
    std::array<bool, 4> m_done{};
    int m_current = 0;
    bool m_updating = false;
};

} // namespace ctrm::app
