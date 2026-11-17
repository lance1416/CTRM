// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/nativefilesystem_posix.h"

#include <cerrno>
#include <cstdio>

namespace ctrm {

RenameResult platformRenameNoReplace(const QByteArray &from, const QByteArray &to)
{
    RenameResult r;
    if (renamex_np(from.constData(), to.constData(), RENAME_EXCL) == 0) {
        r.ok = true;
        return r;
    }
    r.errorCode = errno;
    r.errorText = posixErrorText(r.errorCode);
    return r;
}

} // namespace ctrm
