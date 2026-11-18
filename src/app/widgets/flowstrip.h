// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/windowstate.h"

#include <QWidget>

#include <array>

class QLabel;

namespace ctrm::app {

// The three flow steps under the toolbar (ui-design 5.3, 14; audit O03b). A passive progress
// indicator: labels only, no click, no hover, no focus stop. For assistive technology it is one
// element: the name is "Steps" and the description is the current step.
class FlowStrip : public QWidget
{
    Q_OBJECT
public:
    explicit FlowStrip(QWidget *parent);
    void setState(const WindowStateInfo &info);

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateSteps();
    QString stepDescription() const;

    static constexpr int kSteps = 3;
    std::array<QLabel *, kSteps> m_icons{};
    std::array<QLabel *, kSteps> m_texts{};
    std::array<QLabel *, kSteps - 1> m_separators{};
    std::array<bool, kSteps> m_done{};
    int m_current = 0;
    bool m_updating = false;
};

} // namespace ctrm::app
