// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/plan.h"
#include "fs/filesystem.h"

#include <QList>
#include <QString>

#include <optional>

namespace ctrm {

// QUuid, without braces.
QString newBatchId();
// ".ctrm-tmp-" + first 8 hex digits of the batch ID + "-" + n.
QString tempName(const QString &batchId, int n);

struct PlanResult
{
    std::optional<Plan> plan;
    QString error; // set when plan is empty
};

// Orders the renames so that no step needs to replace an existing name:
// deepest folders first, chains in dependency order, cycles and case-only renames
// through a temporary name in the same folder.
PlanResult makePlan(const QList<RenameRequest> &requests, FileSystem &fs, const QString &batchId);

} // namespace ctrm
