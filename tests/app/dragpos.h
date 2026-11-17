// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPoint>
#include <QPointF>
#include <QtGlobal>

namespace ctrm::test {

// The position type of the QDragEnterEvent constructor. Qt 6.8 has only QPoint; Qt 6.12
// adds QPointF and deprecates QPoint.
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
using DragPos = QPointF;
#else
using DragPos = QPoint;
#endif

} // namespace ctrm::test
