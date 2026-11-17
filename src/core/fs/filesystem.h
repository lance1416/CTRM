// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/item.h"

#include <QList>
#include <QString>

#include <memory>
#include <optional>

namespace ctrm {

// Unknown: treat as Insensitive.
enum class CaseSensitivity { Sensitive, Insensitive, Unknown };

struct VolumeInfo
{
    CaseSensitivity caseSensitivity = CaseSensitivity::Unknown;
    bool normalizationInsensitive = false; // macOS APFS / HFS+
    IdentityType identityType = IdentityType::Weak;
    QString fsName; // "apfs", "ext4", "exfat", "NTFS", ...
};

struct DirEntry
{
    QString name;
    bool isFolder = false;
    bool isSymlink = false;
    bool hiddenAttribute = false; // Windows hidden, macOS UF_HIDDEN
    bool nameSupported = true;    // false: name does not round-trip to UTF-8 (Linux)
};

struct RenameResult
{
    bool ok = false;
    int errorCode = 0; // errno / GetLastError / ctrm::ErrorCode
    QString errorText;
    QString fallback; // "", "link-unlink", "lstat-rename"
};

namespace ErrorCode {
constexpr int SourceMissing = -1001, IdentityChanged = -1002, JournalWrite = -1003,
              TargetExists = -1004;
}

// All platform calls go through this interface. Paths are absolute with '/' separators.
class FileSystem
{
public:
    virtual ~FileSystem() = default;
    virtual VolumeInfo volumeInfo(const QString &folder) = 0;
    // Direct children only. Symbolic links are not followed.
    virtual std::optional<QList<DirEntry>> listFolder(const QString &folder, QString *error) = 0;
    // lstat semantics: a symbolic link exists even when its target does not.
    virtual bool exists(const QString &path) = 0;
    // lstat semantics.
    virtual std::optional<FileIdentity> identity(const QString &path, IdentityType type) = 0;
    // Same folder only. Never replaces an existing item.
    virtual RenameResult renameNoReplace(const QString &from, const QString &to) = 0;
};

// Defined per platform (Tasks 11, 12).
std::unique_ptr<FileSystem> createNativeFileSystem();

} // namespace ctrm
