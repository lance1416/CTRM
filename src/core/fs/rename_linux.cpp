// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/nativefilesystem_posix.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

namespace ctrm {

#ifdef SYS_renameat2
namespace {

RenameResult failure(int errorCode)
{
    RenameResult r;
    r.errorCode = errorCode;
    r.errorText = posixErrorText(errorCode);
    return r;
}

RenameResult success()
{
    RenameResult r;
    r.ok = true;
    return r;
}

} // namespace
#endif

RenameResult platformRenameNoReplace(const QByteArray &from, const QByteArray &to)
{
#ifdef SYS_renameat2
    if (syscall(SYS_renameat2, AT_FDCWD, from.constData(), AT_FDCWD, to.constData(),
                RENAME_NOREPLACE) == 0)
        return success();
    const int e = errno;
    if (e != EINVAL && e != ENOSYS)
        return failure(e);
#endif

    // The file system does not support RENAME_NOREPLACE.
    return posixRenameFallback(from, to);
}

} // namespace ctrm
