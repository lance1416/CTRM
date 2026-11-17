// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/nativefilesystem_posix.h"

#include <cerrno>
#include <cstdio>

namespace ctrm {

RenameResult platformRenameNoReplace(const QByteArray &from, const QByteArray &to)
{
    if (renamex_np(from.constData(), to.constData(), RENAME_EXCL) == 0) {
        RenameResult r;
        r.ok = true;
        return r;
    }
    const int e = errno;
    // The file system does not support RENAME_EXCL (exFAT, FAT, some network volumes).
    if (e == ENOTSUP || e == EINVAL)
        return posixRenameFallback(from, to);
    RenameResult r;
    r.errorCode = e;
    r.errorText = posixErrorText(e);
    return r;
}

} // namespace ctrm
