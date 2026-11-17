// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/filesync.h"

#include <io.h>
#include <qt_windows.h>

namespace ctrm {

bool fullSync(QFile &file)
{
    if (!file.flush())
        return false;
    const int fd = file.handle();
    if (fd < 0)
        return false;
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    return FlushFileBuffers(handle) != 0;
}

bool syncFolder(const QString &folder)
{
    Q_UNUSED(folder);
    return true;
}

} // namespace ctrm
