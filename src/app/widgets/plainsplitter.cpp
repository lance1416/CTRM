// SPDX-License-Identifier: GPL-3.0-or-later
#include "plainsplitter.h"

namespace ctrm::app {

namespace {

class PlainHandle : public QSplitterHandle
{
public:
    using QSplitterHandle::QSplitterHandle;

protected:
    void paintEvent(QPaintEvent * /*event*/) override {}
};

} // namespace

PlainSplitter::PlainSplitter(Qt::Orientation orientation, QWidget *parent)
    : QSplitter(orientation, parent)
{}

QSplitterHandle *PlainSplitter::createHandle()
{
    return new PlainHandle(orientation(), this);
}

} // namespace ctrm::app
