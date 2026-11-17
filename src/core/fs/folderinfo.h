// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fs/filesystem.h"

#include <QString>
#include <QStringList>

namespace ctrm {

struct FolderInfo
{
    QString path;
    bool readable = false;
    VolumeInfo volume;
    QStringList names; // exact names of all direct children
};

FolderInfo readFolderInfo(FileSystem &fs, const QString &folder);

// The key two names share when the volume treats them as the same name.
// Folds case for Insensitive and Unknown; uses NFD when normalizationInsensitive.
QString nameKey(const QString &name, const VolumeInfo &v);

} // namespace ctrm
