// SPDX-License-Identifier: GPL-3.0-or-later
#include "fakefilesystem.h"

#include "fs/folderinfo.h"

#include <QDir>

#include <cerrno>
#include <utility>

using namespace ctrm;

namespace {

QString join(const QString &folder, const QString &name)
{
    return folder == u'/' ? u'/' + name : folder + u'/' + name;
}

} // namespace

QString FakeFileSystem::clean(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString FakeFileSystem::parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash <= 0 ? QStringLiteral("/") : path.left(slash);
}

QString FakeFileSystem::nameOf(const QString &path)
{
    return path.mid(path.lastIndexOf(u'/') + 1);
}

QString FakeFileSystem::resolveName(const QString &folder, const QString &name) const
{
    const auto it = m_folders.constFind(folder);
    if (it == m_folders.constEnd() || name.isEmpty())
        return {};
    if (it->entries.contains(name))
        return name;
    const QString key = nameKey(name, it->volume);
    for (auto e = it->entries.constBegin(); e != it->entries.constEnd(); ++e) {
        if (nameKey(e.key(), it->volume) == key)
            return e.key();
    }
    return {};
}

QString FakeFileSystem::resolveFolder(const QString &path) const
{
    if (m_folders.contains(path))
        return path;
    if (path == u'/' || path.isEmpty())
        return {};
    const QString parent = resolveFolder(parentOf(path));
    if (parent.isEmpty())
        return {};
    const QString name = resolveName(parent, nameOf(path));
    if (name.isEmpty())
        return {};
    const QString exact = join(parent, name);
    return m_folders.contains(exact) ? exact : QString();
}

FakeFileSystem::Folder *FakeFileSystem::ensureFolder(const QString &path)
{
    const QString exact = resolveFolder(path);
    if (!exact.isEmpty())
        return &m_folders[exact];
    // Inherit the volume of the nearest existing ancestor.
    VolumeInfo volume{CaseSensitivity::Sensitive, false, IdentityType::Strong,
                      QStringLiteral("fakefs")};
    for (QString p = parentOf(path);; p = parentOf(p)) {
        const QString a = resolveFolder(p);
        if (!a.isEmpty()) {
            volume = m_folders[a].volume;
            break;
        }
        if (p == u'/')
            break;
    }
    addFolder(path, volume);
    return &m_folders[resolveFolder(path)];
}

FakeFileSystem::Node *FakeFileSystem::findNode(const QString &path, QString *folderOut,
                                               QString *nameOut)
{
    const QString p = clean(path);
    const QString folder = resolveFolder(parentOf(p));
    if (folder.isEmpty())
        return nullptr;
    const QString name = resolveName(folder, nameOf(p));
    if (name.isEmpty())
        return nullptr;
    if (folderOut)
        *folderOut = folder;
    if (nameOut)
        *nameOut = name;
    return &m_folders[folder].entries[name];
}

void FakeFileSystem::addFolder(const QString &path, VolumeInfo volume)
{
    const QString p = clean(path);
    const QString existing = resolveFolder(p);
    if (!existing.isEmpty()) {
        m_folders[existing].volume = std::move(volume);
        return;
    }
    if (p != u'/') {
        const QString parentPath = parentOf(p);
        if (resolveFolder(parentPath).isEmpty())
            addFolder(parentPath, VolumeInfo{CaseSensitivity::Sensitive, false,
                                             IdentityType::Strong, QStringLiteral("fakefs")});
        Folder &parent = m_folders[resolveFolder(parentPath)];
        Node node;
        node.kind = Kind::Folder;
        node.mtimeNs = 1;
        node.inode = m_nextInode++;
        parent.entries.insert(nameOf(p), node);
        m_folders.insert(join(resolveFolder(parentPath), nameOf(p)), Folder{std::move(volume), {}});
        return;
    }
    m_folders.insert(p, Folder{std::move(volume), {}});
}

void FakeFileSystem::addFile(const QString &path, const QByteArray &content, qint64 mtimeNs)
{
    const QString p = clean(path);
    Folder *folder = ensureFolder(parentOf(p));
    const QString existing = resolveName(resolveFolder(parentOf(p)), nameOf(p));
    if (!existing.isEmpty()) {
        Node &node = folder->entries[existing];
        node.kind = Kind::File;
        node.content = content;
        node.mtimeNs = mtimeNs;
        return;
    }
    Node node;
    node.kind = Kind::File;
    node.content = content;
    node.mtimeNs = mtimeNs;
    node.inode = m_nextInode++;
    folder->entries.insert(nameOf(p), node);
}

void FakeFileSystem::addSymlink(const QString &path)
{
    const QString p = clean(path);
    Folder *folder = ensureFolder(parentOf(p));
    const QString existing = resolveName(resolveFolder(parentOf(p)), nameOf(p));
    Node node;
    node.kind = Kind::Symlink;
    node.inode = m_nextInode++;
    folder->entries.insert(existing.isEmpty() ? nameOf(p) : existing, node);
}

bool FakeFileSystem::remove(const QString &path)
{
    QString folder;
    QString name;
    Node *node = findNode(path, &folder, &name);
    if (!node)
        return false;
    if (node->kind == Kind::Folder) {
        const QString exact = join(folder, name);
        const QString prefix = exact + u'/';
        for (auto it = m_folders.begin(); it != m_folders.end();) {
            if (it.key() == exact || it.key().startsWith(prefix))
                it = m_folders.erase(it);
            else
                ++it;
        }
    }
    m_folders[folder].entries.remove(name);
    return true;
}

void FakeFileSystem::setHiddenAttribute(const QString &path, bool hidden)
{
    if (Node *node = findNode(path))
        node->hidden = hidden;
}

void FakeFileSystem::setNameSupported(const QString &path, bool supported)
{
    if (Node *node = findNode(path))
        node->nameSupported = supported;
}

QByteArray FakeFileSystem::content(const QString &path) const
{
    const QString p = clean(path);
    const auto folder = m_folders.constFind(parentOf(p));
    if (folder == m_folders.constEnd())
        return {};
    return folder->entries.value(nameOf(p)).content;
}

QStringList FakeFileSystem::names(const QString &folder) const
{
    const auto it = m_folders.constFind(clean(folder));
    if (it == m_folders.constEnd())
        return {};
    return it->entries.keys(); // QMap keys are sorted
}

void FakeFileSystem::failRename(const QString &from, int errorCode, const QString &text)
{
    m_failures.insert(clean(from), Failure{errorCode, text});
}

void FakeFileSystem::onBeforeRename(std::function<void(const QString &, const QString &)> hook)
{
    m_hook = std::move(hook);
}

int FakeFileSystem::renameCount() const
{
    return m_renameCount;
}

VolumeInfo FakeFileSystem::volumeInfo(const QString &folder)
{
    const QString exact = resolveFolder(clean(folder));
    if (exact.isEmpty())
        return {};
    return m_folders.value(exact).volume;
}

std::optional<QList<DirEntry>> FakeFileSystem::listFolder(const QString &folder, QString *error)
{
    const QString exact = resolveFolder(clean(folder));
    if (exact.isEmpty()) {
        if (error)
            *error = QStringLiteral("No such folder: %1").arg(folder);
        return std::nullopt;
    }
    QList<DirEntry> result;
    const Folder &f = m_folders[exact];
    for (auto it = f.entries.constBegin(); it != f.entries.constEnd(); ++it) {
        DirEntry e;
        e.name = it.key();
        e.isFolder = it->kind == Kind::Folder;
        e.isSymlink = it->kind == Kind::Symlink;
        e.hiddenAttribute = it->hidden;
        e.nameSupported = it->nameSupported;
        result.append(e);
    }
    return result;
}

bool FakeFileSystem::exists(const QString &path)
{
    return findNode(path) != nullptr;
}

std::optional<FileIdentity> FakeFileSystem::identity(const QString &path, IdentityType type)
{
    const Node *node = findNode(path);
    if (!node)
        return std::nullopt;
    FileIdentity id;
    id.type = type;
    if (type == IdentityType::Strong) {
        id.volume = 1;
        id.idLow = node->inode;
    } else {
        id.size = node->kind == Kind::Folder ? -1 : node->content.size();
        id.mtimeNs = node->mtimeNs;
    }
    return id;
}

RenameResult FakeFileSystem::renameNoReplace(const QString &from, const QString &to)
{
    ++m_renameCount;
    if (m_hook)
        m_hook(from, to);

    RenameResult result;
    const QString src = clean(from);
    const QString dst = clean(to);

    const auto failure = m_failures.constFind(src);
    if (failure != m_failures.constEnd()) {
        result.errorCode = failure->errorCode;
        result.errorText = failure->text;
        m_failures.erase(failure);
        return result;
    }

    QString folder;
    QString srcName;
    if (!findNode(src, &folder, &srcName)) {
        result.errorCode = ErrorCode::SourceMissing;
        result.errorText = QStringLiteral("Source missing: %1").arg(src);
        return result;
    }
    if (resolveFolder(parentOf(dst)) != folder) {
        result.errorCode = EXDEV;
        result.errorText = QStringLiteral("Not the same folder: %1 -> %2").arg(src, dst);
        return result;
    }
    const QString dstName = nameOf(dst);
    if (dstName.isEmpty()) {
        result.errorCode = EINVAL;
        result.errorText = QStringLiteral("Empty target name");
        return result;
    }
    if (dstName == srcName) {
        result.ok = true; // same exact name: nothing to do
        return result;
    }
    if (!resolveName(folder, dstName).isEmpty()) {
        result.errorCode = ErrorCode::TargetExists;
        result.errorText = QStringLiteral("Target exists: %1").arg(dst);
        return result;
    }

    Folder &f = m_folders[folder];
    const Node node = f.entries.take(srcName);
    f.entries.insert(dstName, node);
    if (node.kind == Kind::Folder) {
        // Move the folder record and all records below it.
        const QString oldPath = join(folder, srcName);
        const QString newPath = join(folder, dstName);
        const QString prefix = oldPath + u'/';
        QList<std::pair<QString, Folder>> moved;
        for (auto it = m_folders.begin(); it != m_folders.end();) {
            if (it.key() == oldPath || it.key().startsWith(prefix)) {
                moved.append({newPath + it.key().mid(oldPath.size()), it.value()});
                it = m_folders.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto &[path, record] : std::as_const(moved))
            m_folders.insert(path, record);
    }
    result.ok = true;
    return result;
}
