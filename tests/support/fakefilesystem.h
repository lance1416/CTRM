// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fs/filesystem.h"

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>

// In-memory file system for tests. Paths are absolute with '/' separators.
// Name lookup in a folder uses nameKey() with that folder's VolumeInfo, like a real volume:
// on an Insensitive volume "/f/A.TXT" finds "/f/a.txt".
// Each item has a fixed inode number; a rename keeps it (strong identity) and keeps the
// modification time (weak identity).
class FakeFileSystem : public ctrm::FileSystem
{
public:
    // Adds a folder. Missing parent folders get the default volume. When the parent
    // exists, the folder is also an entry in the parent. Adding an existing folder sets its volume.
    void addFolder(const QString &path,
                   ctrm::VolumeInfo volume = {ctrm::CaseSensitivity::Sensitive, false,
                                              ctrm::IdentityType::Strong, "fakefs"});
    // Adds or replaces a file. Missing parent folders are added with the volume of the nearest
    // existing ancestor (or the default volume).
    void addFile(const QString &path, const QByteArray &content = {}, qint64 mtimeNs = 1);
    // Adds a symbolic link (not followed: no target).
    void addSymlink(const QString &path);
    // Removes an item; a folder is removed with all its contents. Returns false when not found.
    bool remove(const QString &path);
    // Sets the hidden attribute (Windows hidden, macOS UF_HIDDEN) of an item.
    void setHiddenAttribute(const QString &path, bool hidden);
    // Marks a name as not round-trip to UTF-8 (DirEntry::nameSupported = false).
    void setNameSupported(const QString &path, bool supported);

    QByteArray content(const QString &path) const;  // exact name
    QStringList names(const QString &folder) const; // exact names, sorted
    // The next rename from this exact path fails with this error. Used once.
    void failRename(const QString &from, int errorCode, const QString &text);
    // Called at the start of each renameNoReplace(), before any check.
    void onBeforeRename(std::function<void(const QString &from, const QString &to)> hook);
    // Number of renameNoReplace() calls, also failed ones.
    int renameCount() const;

    // FileSystem
    ctrm::VolumeInfo volumeInfo(const QString &folder) override;
    std::optional<QList<ctrm::DirEntry>> listFolder(const QString &folder, QString *error) override;
    bool exists(const QString &path) override;
    std::optional<ctrm::FileIdentity> identity(const QString &path,
                                               ctrm::IdentityType type) override;
    // Fails with ErrorCode::TargetExists when the target key exists and the target name is not
    // the exact source name. Thus a case-only rename on an Insensitive volume fails (as on some
    // volumes); the planner must use two steps. Fails with ErrorCode::SourceMissing when the
    // source does not exist, and with EXDEV when the folders are not the same.
    ctrm::RenameResult renameNoReplace(const QString &from, const QString &to) override;

private:
    enum class Kind { File, Folder, Symlink };
    struct Node
    {
        Kind kind = Kind::File;
        QByteArray content;
        qint64 mtimeNs = 1;
        quint64 inode = 0;
        bool hidden = false;
        bool nameSupported = true;
    };
    struct Folder
    {
        ctrm::VolumeInfo volume;
        QMap<QString, Node> entries; // exact name -> node
    };
    struct Failure
    {
        int errorCode = 0;
        QString text;
    };

    static QString clean(const QString &path);
    static QString parentOf(const QString &path);
    static QString nameOf(const QString &path);
    // Exact folder path for a path given in any case the volumes accept. Empty when not found.
    QString resolveFolder(const QString &path) const;
    // Exact name of the entry in an (exact) folder. Empty when not found.
    QString resolveName(const QString &folder, const QString &name) const;
    Folder *ensureFolder(const QString &path);
    Node *findNode(const QString &path, QString *folderOut = nullptr, QString *nameOut = nullptr);

    QMap<QString, Folder> m_folders; // exact folder path -> folder
    QHash<QString, Failure> m_failures;
    std::function<void(const QString &, const QString &)> m_hook;
    int m_renameCount = 0;
    quint64 m_nextInode = 100;
};
