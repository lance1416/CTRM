// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>
#include <QPalette>

namespace ctrm::app {

struct CtrmColors;

// State icons always use the designer files (the shapes are part of the design).
enum class StateIcon { Change, Conflict, Error, Warning, Done, RolledBack, Excluded };
QIcon stateIcon(StateIcon s, const CtrmColors &c);

// Tool icons have the WindowText colour. On Linux the theme icon comes first.
enum class ToolIcon {
    AddFiles,
    AddFolder,
    Add,
    Remove,
    Clear,
    Delete,
    MoveUp,
    MoveDown,
    Duplicate,
    History,
    Info,
    Presets,
    Refresh,
    Undo
};
QIcon toolIcon(ToolIcon t, const QPalette &p);

QIcon appIcon();

} // namespace ctrm::app
