// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/ruleoptions.h"

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

} // namespace ctrm::ruleoptions
