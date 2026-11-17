// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/itemsource.h"

#include "fs/folderinfo.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace ctrm {

namespace {

bool isDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

// Compares a and b by natural order with case folding. Returns <0, 0 or >0.
int naturalCompareFolded(const QString &a, const QString &b)
{
    const QString fa = a.toCaseFolded();
    const QString fb = b.toCaseFolded();
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < fa.size() && j < fb.size()) {
        if (isDigit(fa[i]) && isDigit(fb[j])) {
            qsizetype ei = i;
            while (ei < fa.size() && isDigit(fa[ei]))
                ++ei;
            qsizetype ej = j;
            while (ej < fb.size() && isDigit(fb[ej]))
                ++ej;
            // Skip leading zeros, then the longer run is the bigger number.
            qsizetype si = i;
            while (si + 1 < ei && fa[si] == u'0')
                ++si;
            qsizetype sj = j;
            while (sj + 1 < ej && fb[sj] == u'0')
                ++sj;
            const qsizetype li = ei - si;
            const qsizetype lj = ej - sj;
            if (li != lj)
                return li < lj ? -1 : 1;
            const int c = QStringView(fa).sliced(si, li).compare(QStringView(fb).sliced(sj, lj));
            if (c != 0)
                return c;
            // Same value: fewer leading zeros first.
            if (ei - i != ej - j)
                return (ei - i) < (ej - j) ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        if (fa[i] != fb[j])
            return fa[i] < fb[j] ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < fa.size())
        return 1;
    if (j < fb.size())
        return -1;
    return 0;
}

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    if (slash <= 0)
        return QStringLiteral("/");
    return path.left(slash);
}

QString childPath(const QString &folder, const QString &name)
{
    return folder.endsWith(u'/') ? folder + name : folder + u'/' + name;
}

bool isHidden(const DirEntry &e)
{
    return e.name.startsWith(u'.') || e.hiddenAttribute;
}

#ifdef Q_OS_MACOS
// "._X" next to "X": the AppleDouble file in which macOS keeps the extended attributes of "X" on
// volumes without them (exFAT, FAT, SMB). macOS moves it with "X", so it is never an item.
QSet<QString> appleDoubleSidecars(const QList<DirEntry> &entries)
{
    QSet<QString> names;
    for (const DirEntry &e : entries)
        names.insert(e.name);
    QSet<QString> sidecars;
    for (const DirEntry &e : entries) {
        if (e.name.size() > 2 && e.name.startsWith(QStringLiteral("._")) &&
            names.contains(e.name.mid(2)))
            sidecars.insert(e.name);
    }
    return sidecars;
}
#endif

} // namespace

bool naturalLess(const QString &a, const QString &b)
{
    const int c = naturalCompareFolded(a, b);
    if (c != 0)
        return c < 0;
    // Same name after folding: exact order, so the order is total and stable.
    return a < b;
}

QString canonicalItemPath(const QString &absolutePath)
{
    const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(absolutePath));
    const QFileInfo info(cleaned);
    const QString name = info.fileName();
    const QString parent = QFileInfo(info.path()).canonicalFilePath();
    if (parent.isEmpty() || name.isEmpty())
        return cleaned;
    return childPath(QDir::fromNativeSeparators(parent), name);
}

std::optional<Item> makeItem(FileSystem &fs, const QString &absolutePath, QString *error)
{
    const auto fail = [error](const QString &text) -> std::optional<Item> {
        if (error)
            *error = text;
        return std::nullopt;
    };

    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(absolutePath));
    const QString folder = parentOf(path);
    const QString given = path.mid(path.lastIndexOf(u'/') + 1);
    if (given.isEmpty())
        return fail(QStringLiteral("Not a file or folder: %1").arg(path));

    QString listError;
    const auto entries = fs.listFolder(folder, &listError);
    if (!entries)
        return fail(listError.isEmpty() ? QStringLiteral("Cannot read folder: %1").arg(folder)
                                        : listError);

    // Exact name first. Else the entry the volume treats as the same name (on-disk spelling).
    const VolumeInfo volume = fs.volumeInfo(folder);
    const DirEntry *entry = nullptr;
    for (const DirEntry &e : *entries) {
        if (e.name == given) {
            entry = &e;
            break;
        }
    }
    if (!entry) {
        const QString key = nameKey(given, volume);
        for (const DirEntry &e : *entries) {
            if (nameKey(e.name, volume) == key) {
                entry = &e;
                break;
            }
        }
    }
    if (!entry)
        return fail(QStringLiteral("Not found: %1").arg(path));
    if (!entry->nameSupported)
        return fail(QStringLiteral("Name not supported: %1").arg(path));

    Item item;
    item.folder = folder;
    item.name = entry->name;
    item.isFolder = entry->isFolder && !entry->isSymlink;
    const auto id = fs.identity(item.path(), volume.identityType);
    if (!id)
        return fail(QStringLiteral("Cannot read identity: %1").arg(item.path()));
    item.identity = *id;
    return item;
}

FolderChildren folderChildren(FileSystem &fs, const QString &folder, bool includeHidden)
{
    FolderChildren result;
    QString path = QDir::cleanPath(QDir::fromNativeSeparators(folder));
    QString error;
    const auto entries = fs.listFolder(path, &error);
    if (!entries)
        return result;

    const IdentityType type = fs.volumeInfo(path).identityType;
#ifdef Q_OS_MACOS
    const QSet<QString> sidecars = appleDoubleSidecars(*entries);
#endif
    for (const DirEntry &e : *entries) {
        if (!e.nameSupported) {
            result.unsupportedNames.append(e.name);
            continue;
        }
        if (!includeHidden && isHidden(e))
            continue;
#ifdef Q_OS_MACOS
        if (sidecars.contains(e.name))
            continue;
#endif
        Item item;
        item.folder = path;
        item.name = e.name;
        item.isFolder = e.isFolder && !e.isSymlink;
        const auto id = fs.identity(item.path(), type);
        if (!id)
            continue; // removed after the list was read
        item.identity = *id;
        result.items.append(item);
    }
    std::stable_sort(result.items.begin(), result.items.end(),
                     [](const Item &a, const Item &b) { return naturalLess(a.name, b.name); });
    return result;
}

} // namespace ctrm
