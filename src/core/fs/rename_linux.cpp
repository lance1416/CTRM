// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/nativefilesystem_posix.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

namespace ctrm {

namespace {

RenameResult failure(int errorCode, const QString &fallback = {})
{
    RenameResult r;
    r.errorCode = errorCode;
    r.errorText = posixErrorText(errorCode);
    r.fallback = fallback;
    return r;
}

RenameResult success(const QString &fallback = {})
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
        return failure(EEXIST, fallback);
    if (errno != ENOENT)
        return failure(errno, fallback);
    if (::rename(from, to) != 0)
        return failure(errno, fallback);
    return success(fallback);
}

} // namespace

RenameResult platformRenameNoReplace(const QByteArray &from, const QByteArray &to)
{
    const char *src = from.constData();
    const char *dst = to.constData();
#ifdef SYS_renameat2
    if (syscall(SYS_renameat2, AT_FDCWD, src, AT_FDCWD, dst, RENAME_NOREPLACE) == 0)
        return success();
    const int e = errno;
    if (e != EINVAL && e != ENOSYS)
        return failure(e);
#endif

    // The file system does not support RENAME_NOREPLACE.
    struct stat st;
    if (lstat(src, &st) != 0)
        return failure(errno);
    if (!S_ISDIR(st.st_mode)) {
        const QString fallback = QStringLiteral("link-unlink");
        if (link(src, dst) == 0) {
            if (unlink(src) == 0)
                return success(fallback);
            // Both names exist now: remove the new name, keep the original.
            const int unlinkError = errno;
            unlink(dst);
            return failure(unlinkError, fallback);
        }
        const int linkError = errno;
        // ENOTSUP and EOPNOTSUPP have the same value on Linux.
        if (linkError != EPERM && linkError != EXDEV && linkError != ENOTSUP)
            return failure(linkError, fallback);
    }
    return lstatRename(src, dst);
}

} // namespace ctrm
