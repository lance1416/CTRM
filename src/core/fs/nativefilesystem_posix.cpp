// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/nativefilesystem_posix.h"

#include <QDir>
#include <QFile>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ctrm {

namespace {

QString parentOf(const QString &path)
{
    const QString p = QDir::cleanPath(path);
    const qsizetype slash = p.lastIndexOf(u'/');
    return slash <= 0 ? QStringLiteral("/") : p.left(slash);
}

qint64 mtimeNs(const struct stat &st)
{
#ifdef __APPLE__
    return qint64(st.st_mtimespec.tv_sec) * 1000000000 + st.st_mtimespec.tv_nsec;
#else
    return qint64(st.st_mtim.tv_sec) * 1000000000 + st.st_mtim.tv_nsec;
#endif
}

bool hiddenFlag(const struct stat &st)
{
#ifdef __APPLE__
    return (st.st_flags & UF_HIDDEN) != 0;
#else
    Q_UNUSED(st);
    return false;
#endif
}

class NativeFileSystem final : public FileSystem
{
public:
    VolumeInfo volumeInfo(const QString &folder) override
    {
        return platformVolumeInfo(QFile::encodeName(folder));
    }

    std::optional<QList<DirEntry>> listFolder(const QString &folder, QString *error) override
    {
        const QByteArray path = QFile::encodeName(folder);
        DIR *dir = opendir(path.constData());
        if (!dir) {
            if (error)
                *error = posixErrorText(errno);
            return std::nullopt;
        }
        const int fd = dirfd(dir);
        QList<DirEntry> result;
        int readError = 0;
        for (;;) {
            // readdir() returns NULL at the end and on error; only errno tells them apart.
            errno = 0;
            const dirent *d = readdir(dir);
            if (!d) {
                readError = errno;
                break;
            }
            const QByteArray raw(d->d_name);
            if (raw == "." || raw == "..")
                continue;
            struct stat st;
            if (fstatat(fd, d->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
                continue; // removed after readdir
            DirEntry e;
            e.name = QFile::decodeName(raw);
            e.isFolder = S_ISDIR(st.st_mode);
            e.isSymlink = S_ISLNK(st.st_mode);
            e.hiddenAttribute = hiddenFlag(st);
            e.nameSupported = QFile::encodeName(e.name) == raw;
            result.append(e);
        }
        closedir(dir);
        if (readError != 0) {
            if (error)
                *error = posixErrorText(readError);
            return std::nullopt;
        }
        return result;
    }

    bool exists(const QString &path) override
    {
        struct stat st;
        return lstat(QFile::encodeName(path).constData(), &st) == 0;
    }

    std::optional<FileIdentity> identity(const QString &path, IdentityType type) override
    {
        struct stat st;
        if (lstat(QFile::encodeName(path).constData(), &st) != 0)
            return std::nullopt;
        FileIdentity id;
        id.type = type;
        if (type == IdentityType::Strong) {
            id.volume = quint64(st.st_dev);
            id.idLow = quint64(st.st_ino);
        } else {
            // A folder: type only (its time changes when an item in it is renamed).
            const bool folder = S_ISDIR(st.st_mode);
            id.size = folder ? -1 : qint64(st.st_size);
            id.mtimeNs = folder ? 0 : mtimeNs(st);
        }
        return id;
    }

    RenameResult renameNoReplace(const QString &from, const QString &to) override
    {
        if (parentOf(from) != parentOf(to)) {
            RenameResult r;
            r.errorCode = EXDEV;
            r.errorText = QStringLiteral("Not the same folder");
            return r;
        }
        return platformRenameNoReplace(QFile::encodeName(from), QFile::encodeName(to));
    }
};

} // namespace

QString posixErrorText(int errorCode)
{
    return QString::fromLocal8Bit(std::strerror(errorCode));
}

namespace {

RenameResult renameFailure(int errorCode, const QString &fallback)
{
    RenameResult r;
    r.errorCode = errorCode;
    r.errorText = posixErrorText(errorCode);
    r.fallback = fallback;
    return r;
}

RenameResult renameSuccess(const QString &fallback)
{
    RenameResult r;
    r.ok = true;
    r.fallback = fallback;
    return r;
}

// Only when the target does not exist. Not atomic: the documented last fallback.
RenameResult lstatRename(const char *from, const char *to)
{
    const QString fallback = QStringLiteral("lstat-rename");
    struct stat st;
    if (lstat(to, &st) == 0)
        return renameFailure(EEXIST, fallback);
    if (errno != ENOENT)
        return renameFailure(errno, fallback);
    if (::rename(from, to) != 0)
        return renameFailure(errno, fallback);
    return renameSuccess(fallback);
}

} // namespace

RenameResult posixRenameFallback(const QByteArray &from, const QByteArray &to)
{
    const char *src = from.constData();
    const char *dst = to.constData();
    struct stat st;
    if (lstat(src, &st) != 0)
        return renameFailure(errno, {});
    if (!S_ISDIR(st.st_mode)) {
        const QString fallback = QStringLiteral("link-unlink");
        // Flag 0: a symbolic link is linked itself, not its target (macOS link() follows it).
        if (linkat(AT_FDCWD, src, AT_FDCWD, dst, 0) == 0) {
            if (unlink(src) == 0)
                return renameSuccess(fallback);
            // Both names exist now: remove the new name, keep the original.
            const int unlinkError = errno;
            unlink(dst);
            return renameFailure(unlinkError, fallback);
        }
        const int linkError = errno;
        // ENOTSUP and EOPNOTSUPP are the same value on Linux, not on macOS.
        if (linkError != EPERM && linkError != EXDEV && linkError != ENOTSUP &&
            linkError != EOPNOTSUPP)
            return renameFailure(linkError, fallback);
    }
    return lstatRename(src, dst);
}

std::unique_ptr<FileSystem> createNativeFileSystem()
{
    return std::make_unique<NativeFileSystem>();
}

} // namespace ctrm
