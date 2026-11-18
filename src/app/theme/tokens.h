// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The design language of CTRM (design-tokens 4 and 5). All values are logical pixels. Use these
// names for each space, size and radius: do not use other pixel values or the style metrics.
namespace ctrm::app::tokens {

// Spacing scale (design-tokens 4.1).
constexpr int space1 = 4;  // a control and its own label or icon; rows in one check box group
constexpr int space2 = 8;  // form rows; buttons in a row
constexpr int space3 = 12; // panel inset in the main window; a header row and its content
constexpr int space4 = 16; // groups in a form or a panel
constexpr int space5 = 20; // dialog inset
constexpr int space6 = 24; // around empty-state content; before the button row of a dialog

// Sizes (design-tokens 4.3). Heights are visible heights, not widget heights.
constexpr int controlH = 24;      // text field, combo box, spin box, push button
constexpr int controlHLarge = 32; // only the primary button of an empty state
constexpr int rowH = 24;          // table rows and list rows with one line
constexpr int rowH2 = 40;         // rule list rows (two lines)
constexpr int headerH = 32;       // the header row of each column
constexpr int hitMin = 24;        // each icon button (square)
constexpr int fieldWMax = 280;    // maximum width of a text field or combo box in a form

// Icon sizes (design-tokens 5.2).
constexpr int iconXs = 12; // disclosure and separator chevrons
constexpr int iconS = 16;  // all other UI icons

// Shape (design-tokens 4.4).
constexpr int radiusControl = 6; // text field, combo box, push button, spin box
constexpr int radiusHover = 5;   // hover and pressed fill of borderless and icon buttons
constexpr int radiusPanel = 8;   // the rule list frame

// Window (design-tokens 4.3).
constexpr int defaultWidth = 1280;
constexpr int defaultHeight = 860;

} // namespace ctrm::app::tokens
