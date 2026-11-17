// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fs/folderinfo.h"
#include "model/item.h"
#include "model/status.h"
#include "rules/rulechain.h"
#include "validate/namevalidator.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <atomic>
#include <optional>

namespace ctrm {

// Preview of one item (PRD 7.1).
struct ItemPreview
{
    QString newName;
    Status status = Status::Unchanged;
    QList<Diagnostic> problems; // reasons for the blocking status
    QList<Diagnostic> warnings;
    bool included = true;
    bool skipped = false; // Skip blocked items
};

struct PreviewSummary
{
    int willChange = 0; // Changed + CaseOnly
    int conflicts = 0;
    int errors = 0;   // Invalid + Missing + RuleError
    int warnings = 0; // items with warnings and no blocking status
    int excluded = 0;
    int unchanged = 0; // included, not skipped, Unchanged
    int skipped = 0;
    int blocked() const { return conflicts + errors; }
};

struct PreviewInput
{
    QList<Item> items; // table order
    RuleChain chain;
    QHash<QString, FolderInfo> folders; // key: Item::folder
    OsRules os = currentOsRules();
    QSet<ItemId> skip;            // treated as excluded, marked skipped
    QSet<ItemId> identityChanged; // from BatchService::prepare
};

struct PreviewResult
{
    QList<ItemPreview> items; // same order as input.items
    PreviewSummary summary;
    bool cancelled = false;
};

struct ProposedName
{
    QString newName;
    std::optional<Diagnostic> ruleError;
};

// Applies the rule chain to each item, then evaluateNames(). Pure: safe on a worker thread.
PreviewResult computePreview(const PreviewInput &in, const std::atomic<bool> *cancel = nullptr);

// Gives each proposed name a status, problems and warnings (validity, conflicts, auto-number).
// names has one entry per item of in.items. Also used for the undo preview.
PreviewResult evaluateNames(const PreviewInput &in, const QList<ProposedName> &names,
                            const NameValidator &validator,
                            const std::atomic<bool> *cancel = nullptr);

struct SkipResult
{
    PreviewResult preview;
    QSet<ItemId> skipped;
    QSet<ItemId> cascaded; // subset of skipped: blocked only in a later round
};

// "Skip blocked items": skips blocked items until no new item is blocked (PRD 7.1).
SkipResult previewSkippingBlocked(const PreviewInput &in);

} // namespace ctrm
