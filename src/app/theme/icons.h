// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>
#include <QPalette>

namespace ctrm::app {

struct CtrmColors;

// State icons always use the designer files (the shapes are part of the design).
// FlowDone: the done step of the flow strip (a filled circle with a white check).
enum class StateIcon { Change, Conflict, Error, Warning, Done, RolledBack, Excluded, FlowDone };
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
// A designer icon by its file name (for example "tool-left", "chevron-xs-right"), in the
// WindowText colour, at the size of its canvas (icon-s 16 or icon-xs 12). No theme icon.
QIcon toolIcon(const QString &name, const QPalette &p);

QIcon appIcon();

} // namespace ctrm::app
