// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Qt>

class QAbstractScrollArea;

namespace ctrm::app {

// A `base` surface in a `window` surface (design-tokens 2.1, surface rule, audit R5-09): no
// frame in either mode, because the surface colours show the edge. In high contrast the
// surfaces can have one colour, so a 1 px `separator` (Mid) line shows the given edges.
void setSurfaceEdges(QAbstractScrollArea *area, Qt::Edges edges);

} // namespace ctrm::app
