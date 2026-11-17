// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/item.h"

#include <QList>
#include <QString>

namespace ctrm {

struct RenameRequest
{
    ItemId item = 0;
    QString folder; // absolute, '/' separators, no trailing '/'
    QString oldName;
    QString newName;
    bool isFolder = false;
    FileIdentity identity;
};

// Direct: old -> new. ToTemp: old -> temporary name. FromTemp: temporary name -> new.
enum class StepKind { Direct, ToTemp, FromTemp };

struct PlanStep
{
    int index = 0;
    ItemId item = 0;
    QString from; // absolute path
    QString to;   // absolute path
    StepKind kind = StepKind::Direct;
};

struct Plan
{
    QString batchId;
    QList<RenameRequest> requests;
    QList<PlanStep> steps; // run order
};

} // namespace ctrm
