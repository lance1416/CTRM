// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/nativefilesystem_posix.h"

#include <QFile>

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/vfs.h>

namespace ctrm {

namespace {

constexpr unsigned long kExt = 0xEF53;
constexpr unsigned long kBtrfs = 0x9123683E;
constexpr unsigned long kXfs = 0x58465342;

QString fsNameOf(unsigned long type)
{
    switch (type) {
    case kExt:
        return QStringLiteral("ext4");
    case kBtrfs:
        return QStringLiteral("btrfs");
    case kXfs:
        return QStringLiteral("xfs");
    case 0x01021994:
        return QStringLiteral("tmpfs");
    case 0x4d44:
        return QStringLiteral("vfat");
    case 0x2011BAB0:
        return QStringLiteral("exfat");
    case 0x5346544e:
        return QStringLiteral("ntfs");
    case 0x7366746e:
        return QStringLiteral("ntfs3");
    case 0x6969:
        return QStringLiteral("nfs");
    case 0xFF534D42:
        return QStringLiteral("cifs");
    case 0x65735546:
        return QStringLiteral("fuse");
    case 0x794c7630:
        return QStringLiteral("overlay");
    case 0x2fc12fc1:
        return QStringLiteral("zfs");
    case 0xF2F52010:
        return QStringLiteral("f2fs");
    default:
        return QStringLiteral("0x") + QString::number(type, 16);
    }
}

// The name with the case of each letter swapped. Equal to name when no letter changes.
QString swappedCase(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        if (c.isLower())
            out += c.toUpper();
        else if (c.isUpper())
            out += c.toLower();
        else
            out += c;
    }
    return out;
}

// Looks up a listed name with its case swapped. Same item -> Insensitive.
CaseSensitivity probeCase(const QByteArray &folder)
{
    DIR *dir = opendir(folder.constData());
    if (!dir)
        return CaseSensitivity::Unknown;
    const int fd = dirfd(dir);
    CaseSensitivity result = CaseSensitivity::Unknown;
    while (const dirent *d = readdir(dir)) {
        const QByteArray raw(d->d_name);
        if (raw == "." || raw == "..")
            continue;
        const QString name = QFile::decodeName(raw);
        if (QFile::encodeName(name) != raw)
            continue;
        const QString swapped = swappedCase(name);
        if (swapped == name || swapped.toCaseFolded() != name.toCaseFolded())
            continue;
        struct stat original;
        if (fstatat(fd, d->d_name, &original, AT_SYMLINK_NOFOLLOW) != 0)
            continue;
        struct stat other;
        const QByteArray swappedRaw = QFile::encodeName(swapped);
        if (fstatat(fd, swappedRaw.constData(), &other, AT_SYMLINK_NOFOLLOW) == 0) {
            result = (other.st_dev == original.st_dev && other.st_ino == original.st_ino)
                             ? CaseSensitivity::Insensitive
                             : CaseSensitivity::Sensitive;
        } else {
            result = errno == ENOENT ? CaseSensitivity::Sensitive : CaseSensitivity::Unknown;
        }
        break;
    }
    closedir(dir);
    return result;
}

} // namespace

VolumeInfo platformVolumeInfo(const QByteArray &folder)
{
    VolumeInfo v;
    struct statfs sfs;
    if (statfs(folder.constData(), &sfs) == 0) {
        const auto type = static_cast<unsigned long>(sfs.f_type) & 0xFFFFFFFFUL;
        v.fsName = fsNameOf(type);
        if (type == kExt || type == kBtrfs || type == kXfs)
            v.identityType = IdentityType::Strong;
    }
    v.caseSensitivity = probeCase(folder);
    return v;
}

} // namespace ctrm
