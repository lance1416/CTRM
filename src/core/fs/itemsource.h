// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fs/filesystem.h"
#include "model/item.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace ctrm {

// Natural order, case-insensitive: "file2" < "file10", "a" < "B".
bool naturalLess(const QString &a, const QString &b);

// Canonical parent folder + '/' + name. The item itself is not resolved, so a link stays a link.
// When the parent cannot be resolved, returns the cleaned absolute path.
QString canonicalItemPath(const QString &absolutePath);

// Makes an item (id = 0) for an existing path. nullopt and *error set when the item does not
// exist, its name is not supported, or its identity cannot be read.
std::optional<Item> makeItem(FileSystem &fs, const QString &absolutePath, QString *error);

struct FolderChildren
{
    QList<Item> items;
    QStringList unsupportedNames;
};

// Direct children in natural order. Hidden items (leading '.' or hidden attribute) only when
// includeHidden. Names that are not supported go to unsupportedNames, not to items.
// On macOS, an AppleDouble file "._X" next to "X" is never an item (macOS moves it with "X").
FolderChildren folderChildren(FileSystem &fs, const QString &folder, bool includeHidden);

} // namespace ctrm
