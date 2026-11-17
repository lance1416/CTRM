// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "preview/previewengine.h"

#include <QString>

#include <array>

namespace ctrm::app {

// The window states of ui-design 5.2.
enum class WindowState { NoItems, NoRules, NoChange, Ready, Blocked, Running, Result };

// The one primary button of the state (ui-design 5.1, 5.2).
enum class PrimaryTarget { AddFiles, AddRule, Rename, None };

struct WindowStateInput
{
    int itemCount = 0;
    int enabledRuleCount = 0;
    ctrm::PreviewSummary summary;
    bool allFiles = true;
    bool allFolders = false;
    bool running = false;
    bool resultShown = false;
    int renamedCount = 0; // the result state: items the batch renamed
};

struct WindowStateInfo
{
    WindowState state = WindowState::NoItems;
    PrimaryTarget primary = PrimaryTarget::AddFiles;
    QString renameText;
    bool renameEnabled = false;
    QString reason; // empty when renameEnabled; also empty in the Running state (button hidden)
    bool skipVisible = false;
    QString skipText;               // "Skip 4 and Rename..."
    int renamedCount = 0;           // the result state only
    std::array<bool, 3> stepDone{}; // Add files, Add rules, Rename (S01)
    int currentStep = 0;            // flow strip, 0-based, -1 when all steps are done
};

WindowStateInfo computeWindowState(const WindowStateInput &in);

// "12 will change · 2 conflicts · 1 error · 1 warning" (ui-design 5.5).
QString countSummaryText(const ctrm::PreviewSummary &s);

// The parts of countSummaryText, for the action bar.
enum class SummaryPart { WillChange, Conflicts, Errors, Warnings, Excluded, Unchanged, Renamed };
QString summaryPartText(SummaryPart part, int n);

} // namespace ctrm::app
