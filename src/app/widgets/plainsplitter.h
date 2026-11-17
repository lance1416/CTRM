// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSplitter>

namespace ctrm::app {

// A splitter whose handle draws nothing (ui-design 4.1: clean panel edges). Some styles (macOS)
// draw a grip in the empty space between the panels. The handle keeps its size and its resize
// cursor, so the user can still drag it.
class PlainSplitter : public QSplitter
{
public:
    explicit PlainSplitter(Qt::Orientation orientation, QWidget *parent = nullptr);

protected:
    QSplitterHandle *createHandle() override;
};

} // namespace ctrm::app
