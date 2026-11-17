// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fs/filesystem.h"

#include <QByteArray>

namespace ctrm {

// One definition per OS (rename_macos.cpp, rename_linux.cpp). Paths are encoded with
// QFile::encodeName. Never replaces an existing item.
RenameResult platformRenameNoReplace(const QByteArray &from, const QByteArray &to);

// One definition per OS (volume_macos.cpp, volume_linux.cpp).
VolumeInfo platformVolumeInfo(const QByteArray &folder);

// strerror(errorCode) as a QString.
QString posixErrorText(int errorCode);

} // namespace ctrm
