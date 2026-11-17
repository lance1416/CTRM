// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/filesync.h"

#include <fcntl.h>
#include <unistd.h>

namespace ctrm {

bool fullSync(QFile &file)
{
    if (!file.flush())
        return false;
    const int fd = file.handle();
    if (fd < 0)
        return false;
#ifdef F_FULLFSYNC
    if (fcntl(fd, F_FULLFSYNC) == 0)
        return true;
    // Some file systems (for example network volumes) do not support F_FULLFSYNC.
#endif
    return fsync(fd) == 0;
}

bool syncFolder(const QString &folder)
{
    const int fd = open(QFile::encodeName(folder).constData(), O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return false;
    const bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

} // namespace ctrm
