// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

namespace ctrm::ruleoptions {

// Helpers for Rule::optionsFromJson. A missing key gives nullopt and no warning. A value of the
// wrong type or an unknown enum string gives nullopt and the warning
// "Bad value for '<key>' in rule <type>".

void warnBadValue(QStringList *warnings, const QString &key, const QString &ruleType);

std::optional<bool> readBool(const QJsonObject &o, const QString &key, const QString &ruleType,
                             QStringList *warnings);
std::optional<QString> readString(const QJsonObject &o, const QString &key, const QString &ruleType,
                                  QStringList *warnings);

// Reads a string and maps it to an enum value with the (value, text) table.
template <typename E, typename Table>
std::optional<E> readEnum(const QJsonObject &o, const QString &key, const Table &table,
                          const QString &ruleType, QStringList *warnings)
{
    const std::optional<QString> s = readString(o, key, ruleType, warnings);
    if (!s)
        return std::nullopt;
    for (const auto &[value, text] : table) {
        if (*s == text)
            return value;
    }
    warnBadValue(warnings, key, ruleType);
    return std::nullopt;
}

// The text of an enum value in the (value, text) table.
template <typename E, typename Table>
QString enumText(E value, const Table &table)
{
    for (const auto &[v, text] : table) {
        if (v == value)
            return QString(text);
    }
    return QString();
}

} // namespace ctrm::ruleoptions
