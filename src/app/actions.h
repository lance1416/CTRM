// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

class QAction;
class QMenu;

namespace ctrm::app {

// Every QAction of ui-design sections 12 and 13, with text, tooltip and shortcut. The actions
// are not connected; the window and the panels connect them.
struct Actions
{
    QAction *addFiles, *addFolder, *removeFromList, *clearList, *refresh, *closeWindow, *quit;
    QAction *undoRename, *include, *exclude, *selectAll, *find;
    QMenu *addRuleMenu;
    QAction *duplicateRule, *deleteRule, *moveRuleUp, *moveRuleDown, *toggleRule, *clearRules;
    QAction *saveAsPreset, *updatePreset, *managePresets, *importPreset, *exportRules;
    QAction *showSteps, *highlightChanged, *showAllItems, *showChangedOnly, *showProblemsOnly,
            *showToolbar;
    QAction *rename, *skipAndRename, *nextProblem, *previousProblem, *history, *cancelBatch;
    QAction *focusRules, *focusTable, *focusEditor, *preferences;
    QAction *help, *regexHelp, *reportProblem, *about;
};

// Makes the actions as children of parent. When parent is a widget, addRuleMenu is its child;
// if not, addRuleMenu is deleted when parent is destroyed.
Actions createActions(QObject *parent);

} // namespace ctrm::app
