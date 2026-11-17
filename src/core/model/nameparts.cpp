// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/nameparts.h"

namespace ctrm {

NameParts splitName(const QString &name, bool isFolder, bool folderExtensions)
{
    if (isFolder && !folderExtensions)
        return {name, QString(), false};

    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
        return {name, QString(), false};

    return {name.left(dot), name.mid(dot + 1), true};
}

QString joinName(const NameParts &parts)
{
    if (!parts.extension.isEmpty())
        return parts.stem + QLatin1Char('.') + parts.extension;
    return parts.hasDot ? parts.stem + QLatin1Char('.') : parts.stem;
}

} // namespace ctrm
