// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fs/filesystem.h"
#include "fs/folderinfo.h"
#include "model/item.h"

#include <QHash>
#include <QList>
#include <QString>

namespace ctrm::app {

// FolderInfo of the folders of the items. Reads each folder one time until invalidate().
class FolderCache
{
public:
    explicit FolderCache(ctrm::FileSystem &fs);

    // The FolderInfo of each folder of the items. Reads only the folders that are not cached.
    QHash<QString, ctrm::FolderInfo> folders(const QList<ctrm::Item> &items);
    // Forgets all folders (Refresh, after a batch, on add of items, on window activation).
    void invalidate();

private:
    ctrm::FileSystem &m_fs;
    QHash<QString, ctrm::FolderInfo> m_cache;
};

} // namespace ctrm::app
