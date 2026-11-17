// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/ruleoptions.h"

#include <QJsonArray>

#include <cmath>

namespace ctrm::ruleoptions {

void warnBadValue(QStringList *warnings, const QString &key, const QString &ruleType)
{
    if (warnings)
        warnings->append(QStringLiteral("Bad value for '%1' in rule %2").arg(key, ruleType));
}

std::optional<bool> readBool(const QJsonObject &o, const QString &key, const QString &ruleType,
                             QStringList *warnings)
{
    if (!o.contains(key))
        return std::nullopt;
    const QJsonValue v = o.value(key);
    if (!v.isBool()) {
        warnBadValue(warnings, key, ruleType);
        return std::nullopt;
    }
    return v.toBool();
}

std::optional<QString> readString(const QJsonObject &o, const QString &key, const QString &ruleType,
                                  QStringList *warnings)
{
    if (!o.contains(key))
        return std::nullopt;
    const QJsonValue v = o.value(key);
    if (!v.isString()) {
        warnBadValue(warnings, key, ruleType);
        return std::nullopt;
    }
    return v.toString();
}

std::optional<int> readInt(const QJsonObject &o, const QString &key, int min, int max,
                           const QString &ruleType, QStringList *warnings)
{
    if (!o.contains(key))
        return std::nullopt;
    const QJsonValue v = o.value(key);
    const double d = v.toDouble();
    if (!v.isDouble() || !std::isfinite(d) || d != std::floor(d) || d < min || d > max) {
        warnBadValue(warnings, key, ruleType);
        return std::nullopt;
    }
    return int(d);
}

std::optional<QStringList> readStringList(const QJsonObject &o, const QString &key,
                                          const QString &ruleType, QStringList *warnings)
{
    if (!o.contains(key))
        return std::nullopt;
    const QJsonValue v = o.value(key);
    if (!v.isArray()) {
        warnBadValue(warnings, key, ruleType);
        return std::nullopt;
    }
    QStringList out;
    for (const QJsonValue item : v.toArray()) {
        if (!item.isString()) {
            warnBadValue(warnings, key, ruleType);
            return std::nullopt;
        }
        out.append(item.toString());
    }
    return out;
}

} // namespace ctrm::ruleoptions
