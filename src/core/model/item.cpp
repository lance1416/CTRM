// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/item.h"

#include <QJsonValue>

namespace ctrm {

namespace {

const QString kType = QStringLiteral("type");
const QString kStrong = QStringLiteral("strong");
const QString kWeak = QStringLiteral("weak");
const QString kVolume = QStringLiteral("volume");
const QString kIdHigh = QStringLiteral("idHigh");
const QString kIdLow = QStringLiteral("idLow");
const QString kSize = QStringLiteral("size");
const QString kMtimeNs = QStringLiteral("mtimeNs");

bool allDigits(QStringView s)
{
    if (s.isEmpty())
        return false;
    for (const QChar c : s) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
    }
    return true;
}

// Accepts only plain decimal text: no sign, space or other characters.
std::optional<quint64> readUnsigned(const QJsonObject &o, const QString &key)
{
    const QJsonValue v = o.value(key);
    if (!v.isString())
        return std::nullopt;
    const QString s = v.toString();
    if (!allDigits(s))
        return std::nullopt;
    bool ok = false;
    const quint64 n = s.toULongLong(&ok);
    return ok ? std::optional<quint64>(n) : std::nullopt;
}

// Accepts plain decimal text with an optional leading '-'.
std::optional<qint64> readSigned(const QJsonObject &o, const QString &key)
{
    const QJsonValue v = o.value(key);
    if (!v.isString())
        return std::nullopt;
    const QString s = v.toString();
    const QStringView digits = s.startsWith(QLatin1Char('-')) ? QStringView(s).mid(1) : s;
    if (!allDigits(digits))
        return std::nullopt;
    bool ok = false;
    const qint64 n = s.toLongLong(&ok);
    return ok ? std::optional<qint64>(n) : std::nullopt;
}

} // namespace

bool sameFile(const FileIdentity &expected, const FileIdentity &actual)
{
    if (expected.type != actual.type)
        return false;
    if (expected.type == IdentityType::Strong) {
        return expected.volume == actual.volume && expected.idHigh == actual.idHigh &&
               expected.idLow == actual.idLow;
    }
    // A weak folder matches on its type only: its name is checked by the path, and its
    // modification time changes when CTRM renames an item in it.
    if (expected.size < 0)
        return actual.size < 0;
    return expected.mtimeNs == actual.mtimeNs && expected.size == actual.size;
}

QJsonObject identityToJson(const FileIdentity &id)
{
    return QJsonObject{
            {kType, id.type == IdentityType::Strong ? kStrong : kWeak},
            {kVolume, QString::number(id.volume)},
            {kIdHigh, QString::number(id.idHigh)},
            {kIdLow, QString::number(id.idLow)},
            {kSize, QString::number(id.size)},
            {kMtimeNs, QString::number(id.mtimeNs)},
    };
}

std::optional<FileIdentity> identityFromJson(const QJsonObject &o)
{
    FileIdentity id;
    const QString type = o.value(kType).toString();
    if (type == kStrong)
        id.type = IdentityType::Strong;
    else if (type == kWeak)
        id.type = IdentityType::Weak;
    else
        return std::nullopt;

    const auto volume = readUnsigned(o, kVolume);
    const auto idHigh = readUnsigned(o, kIdHigh);
    const auto idLow = readUnsigned(o, kIdLow);
    const auto size = readSigned(o, kSize);
    const auto mtimeNs = readSigned(o, kMtimeNs);
    if (!volume || !idHigh || !idLow || !size || !mtimeNs)
        return std::nullopt;

    id.volume = *volume;
    id.idHigh = *idHigh;
    id.idLow = *idLow;
    id.size = *size;
    id.mtimeNs = *mtimeNs;
    return id;
}

QString Item::path() const
{
    return folder + QLatin1Char('/') + name;
}

} // namespace ctrm
