// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QLayout;
class QTableView;
class QVBoxLayout;
class QWidget;

namespace ctrm::app {

// The layout of a dialog or the History window (design-tokens 4.1, 4.2; craft audit O07): inset
// space-5 on each side, space-2 between the rows. The layout is the layout of dialog.
QVBoxLayout *makeDialogLayout(QWidget *dialog);

// Adds the button row as the last row, space-6 under the content.
void addButtonRow(QVBoxLayout *layout, QWidget *buttons);
void addButtonRow(QVBoxLayout *layout, QLayout *buttons);

// Rows of row-h, as all tables of the app (craft audit C11).
void useRowHeight(QTableView *table);

} // namespace ctrm::app
