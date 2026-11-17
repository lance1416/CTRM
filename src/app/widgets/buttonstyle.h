// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QPushButton;

namespace ctrm::app {

// Makes button the primary button (default, accent) or a normal one (ui-design 5.1). When the
// style does not draw the default button with the accent colour (Fusion), the button palette
// gets Button = Accent and ButtonText = HighlightedText. Call it again on a style or palette
// change of the parent.
void setPrimary(QPushButton *button, bool primary);

} // namespace ctrm::app
