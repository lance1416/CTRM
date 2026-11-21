// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/status.h"
#include "preview/previewengine.h"

#include <QString>
#include <QStringList>

#include <utility>

namespace ctrm::app {

// States of an item after a batch (ui-design 7.2).
enum class PostRun { None, Renamed, Failed, Skipped, RolledBack };

struct PostRunState
{
    PostRunState() = default;
    PostRunState(PostRun state, QString text, QString fullText = {})
        : state(state)
        , text(std::move(text))
        , fullText(std::move(fullText))
    {}

    PostRun state = PostRun::None;
    QString text;     // Failed: OS error text. Skipped: the short blocking reason.
    QString fullText; // Skipped: the full blocking reason; when empty, text
};

// The text of one diagnostic (ui-design 7.2). ruleNames: display name of each rule of the
// chain, used for RuleFailed; when the name is not known, the text has no "(name)" part.
QString diagnosticText(const Diagnostic &d, bool portableNames, const QStringList &ruleNames = {});

// The Message column text of a preview row (ui-design 7.2).
QString statusText(const ItemPreview &p, bool portableNames);

// The full text of an item that keeps its name: "No change. The name stays 'a.txt'."
QString unchangedText(const QString &name);

// "Renamed", "Failed: ...", "Skipped: ...", "Rolled back to the old name". Empty for None.
QString postRunText(const PostRunState &s);

// The reason of a conflict with the old name of a blocked item (PRD 7.1).
QString blockedOldNameText(const QString &oldName);

// The short texts of the Message cell (ui-design 7.2, audit S02 and S03): 28 characters or
// fewer, key word first. The cell elides them at the end, and the tooltip has the full sentence.
QString shortDiagnosticText(const Diagnostic &d);
// Empty for a row that needs no text: a change without a warning, or no change.
QString shortStatusText(const ItemPreview &p);
// "Renamed", "Failed: ...", "Skipped: ...", "Rolled back". Empty for None.
QString shortPostRunText(const PostRunState &s);
QString shortBlockedOldNameText();

// "Show in Finder" (macOS), "Show in Explorer" (Windows), "Show in File Manager" (others).
QString showInFileManagerText();

} // namespace ctrm::app
