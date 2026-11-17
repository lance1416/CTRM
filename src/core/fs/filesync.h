// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFile>
#include <QString>

namespace ctrm {

// Flushes Qt's buffer and the OS cache of the file to the disk.
// POSIX: fsync (macOS: F_FULLFSYNC, else fsync). Windows: FlushFileBuffers.
bool fullSync(QFile &file);

// Makes a new or removed name in the folder durable. POSIX: fsync on the folder.
// Windows: returns true (NTFS has no folder sync).
bool syncFolder(const QString &folder);

} // namespace ctrm
