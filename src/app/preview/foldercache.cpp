// SPDX-License-Identifier: GPL-3.0-or-later
#include "preview/foldercache.h"

namespace ctrm::app {

FolderCache::FolderCache(FileSystem &fs)
    : m_fs(fs)
{}

QHash<QString, FolderInfo> FolderCache::folders(const QList<Item> &items)
{
    QHash<QString, FolderInfo> out;
    for (const Item &item : items) {
        if (out.contains(item.folder))
            continue;
        auto it = m_cache.constFind(item.folder);
        if (it == m_cache.cend())
            it = m_cache.insert(item.folder, readFolderInfo(m_fs, item.folder));
        out.insert(item.folder, *it);
    }
    return out;
}

void FolderCache::invalidate()
{
    m_cache.clear();
}

} // namespace ctrm::app
