// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>
#include <QPalette>
#include <QPixmap>

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
    Close,
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

// The icon at size, with each pixel in colour (the alpha of the icon stays). An icon button
// recolours its icon for each state (design-tokens 4.5).
QPixmap tintedPixmap(const QIcon &icon, const QSize &size, qreal devicePixelRatio,
                     const QColor &colour);

} // namespace ctrm::app
