// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace ctrm {

using ItemId = quint64;

// Strong: volume + file ID from the OS. Weak: size + modification time only.
enum class IdentityType { Strong, Weak };

struct FileIdentity
{
    IdentityType type = IdentityType::Weak;
    quint64 volume = 0; // strong: st_dev / volume serial
    quint64 idHigh = 0; // strong: 0 / FILE_ID_128 high
    quint64 idLow = 0;  // strong: st_ino / FILE_ID_128 low
    qint64 size = -1;   // weak: file size, -1 for a folder
    qint64 mtimeNs = 0; // weak: modification time of a file (0 for a folder)
    bool operator==(const FileIdentity &) const = default;
};

// Strong: compares volume, idHigh and idLow. Weak file: compares size and mtimeNs. Weak
// folder (size -1): matches any folder (the path checks the name). Identities of different
// type are never the same file.
bool sameFile(const FileIdentity &expected, const FileIdentity &actual);

// Keys: type ("strong"|"weak"), volume, idHigh, idLow, size, mtimeNs.
// All numbers are decimal strings, so 64-bit values stay exact.
QJsonObject identityToJson(const FileIdentity &id);
// nullopt when the type is unknown or a key is missing or not a valid decimal string.
std::optional<FileIdentity> identityFromJson(const QJsonObject &o);

struct Item
{
    ItemId id = 0;
    QString folder; // absolute, '/' separators, no trailing '/'
    QString name;
    bool isFolder = false;
    bool included = true;
    FileIdentity identity;
    QString path() const; // folder + '/' + name
};

} // namespace ctrm
