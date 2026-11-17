// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QPainter;
class QStyleOptionViewItem;

namespace ctrm::app {

// Draws CE_ItemViewItem. The check indicator is drawn at the origin of a translated painter:
// the macOS style draws it only in the first row when its rect is not at the origin.
void paintItemViewItem(QPainter *painter, const QStyleOptionViewItem &option);

} // namespace ctrm::app
