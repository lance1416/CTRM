// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/status.h"
#include "preview/previewengine.h"

#include <QString>
#include <QStringList>

namespace ctrm::app {

// States of an item after a batch (ui-design 7.2).
enum class PostRun { None, Renamed, Failed, Skipped, RolledBack };

struct PostRunState
{
    PostRun state = PostRun::None;
    QString text; // Failed: OS error text. Skipped: the blocking reason.
};

// The text of one diagnostic (ui-design 7.2). ruleNames: display name of each rule of the
// chain, used for RuleFailed; when the name is not known, the text has no "(name)" part.
QString diagnosticText(const Diagnostic &d, bool portableNames, const QStringList &ruleNames = {});

// The Message column text of a preview row (ui-design 7.2).
QString statusText(const ItemPreview &p, bool portableNames);

// "Renamed", "Failed: ...", "Skipped: ...", "Rolled back to the old name". Empty for None.
QString postRunText(const PostRunState &s);

// The reason of a conflict with the old name of a blocked item (PRD 7.1).
QString blockedOldNameText(const QString &oldName);

// "Show in Finder" (macOS), "Show in Explorer" (Windows), "Show in File Manager" (others).
QString showInFileManagerText();

} // namespace ctrm::app
