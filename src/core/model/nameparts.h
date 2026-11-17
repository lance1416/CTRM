// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace ctrm {

// A name split into stem and extension (PRD 6.1).
struct NameParts
{
    QString stem;
    QString extension;   // without the dot
    bool hasDot = false; // a separator dot exists (also "file." with empty extension)
};

// Splits at the last dot. A dot at position 0 is not a separator. A folder has no extension
// unless folderExtensions is true.
NameParts splitName(const QString &name, bool isFolder, bool folderExtensions);

// Extension not empty: stem + "." + extension. Empty: stem + (hasDot ? "." : "").
QString joinName(const NameParts &parts);

} // namespace ctrm
