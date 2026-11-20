// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>

// File system states that tests make on purpose. Each one uses a method that works on the
// platform: Windows ignores the read-only attribute of a folder, QFile::setPermissions cannot
// remove read access there, and QFile::link makes a shell shortcut (.lnk), not a file system
// link.

// Makes the rename of one item fail until the object goes away.
// POSIX: the parent folder is read-only. Windows: the item is kept open without
// FILE_SHARE_DELETE, so MoveFileExW fails with ERROR_SHARING_VIOLATION.
class RenameBlock
{
public:
    explicit RenameBlock(const QString &itemPath);
    ~RenameBlock();
    Q_DISABLE_COPY_MOVE(RenameBlock)

    bool isActive() const { return m_active; }

private:
#if defined(Q_OS_WIN)
    void *m_handle = nullptr; // HANDLE
#else
    QString m_folder;
#endif
    bool m_active = false;
};

// Makes a folder that cannot be listed until the object goes away.
// POSIX: the folder has no permissions. Windows: an ACE denies "list folder" to Everyone.
class UnlistableFolder
{
public:
    explicit UnlistableFolder(QString folder);
    ~UnlistableFolder();
    Q_DISABLE_COPY_MOVE(UnlistableFolder)

    bool isActive() const { return m_active; }

private:
    QString m_folder;
    bool m_active = false;
};

// Makes a file system link to a folder: a symbolic link on POSIX, a junction on Windows (a
// junction needs no privilege).
bool makeFolderLink(const QString &target, const QString &link);

// Sets the modification time of a file that another handle keeps open. On Windows a QLockFile
// shares only reading, so QFile cannot open it for writing; FILE_WRITE_ATTRIBUTES access is not
// part of the sharing check.
bool setModificationTimeShared(const QString &path, const QDateTime &time);
