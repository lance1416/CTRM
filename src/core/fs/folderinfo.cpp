// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/folderinfo.h"

namespace ctrm {

FolderInfo readFolderInfo(FileSystem &fs, const QString &folder)
{
    FolderInfo info;
    info.path = folder;
    info.volume = fs.volumeInfo(folder);
    QString error;
    const auto entries = fs.listFolder(folder, &error);
    if (!entries)
        return info;
    info.readable = true;
    info.names.reserve(entries->size());
    for (const DirEntry &e : *entries)
        info.names.append(e.name);
    return info;
}

QString nameKey(const QString &name, const VolumeInfo &v)
{
    QString n = name;
    if (v.normalizationInsensitive)
        n = n.normalized(QString::NormalizationForm_D);
    if (v.caseSensitivity != CaseSensitivity::Sensitive) {
        n = n.toCaseFolded();
        if (v.normalizationInsensitive)
            n = n.normalized(QString::NormalizationForm_D);
    }
    return n;
}

} // namespace ctrm
