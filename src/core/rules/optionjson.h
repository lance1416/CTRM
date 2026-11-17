// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Helpers for Rule::optionsFromJson. A missing key gives the fallback with no warning. A bad
// value gives the fallback and the warning "Bad value for '<key>' in rule <type>".

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace ctrm::optionjson {

template <typename Enum, size_t N>
using EnumTable = std::array<std::pair<Enum, const char *>, N>;

class Reader
{
public:
    Reader(const QJsonObject &o, QString ruleType, QStringList *warnings)
        : m_o(o)
        , m_type(std::move(ruleType))
        , m_warnings(warnings)
    {}

    QString string(const QString &key, const QString &fallback) const
    {
        const QJsonValue v = m_o.value(key);
        if (v.isUndefined())
            return fallback;
        if (!v.isString())
            return bad(key, fallback);
        return v.toString();
    }

    bool boolean(const QString &key, bool fallback) const
    {
        const QJsonValue v = m_o.value(key);
        if (v.isUndefined())
            return fallback;
        if (!v.isBool())
            return bad(key, fallback);
        return v.toBool();
    }

    // An integral JSON number in [min, max].
    int integer(const QString &key, int fallback, int min, int max) const
    {
        const QJsonValue v = m_o.value(key);
        if (v.isUndefined())
            return fallback;
        if (const auto n = toInt(v, min, max))
            return *n;
        return bad(key, fallback);
    }

    QStringList stringList(const QString &key, const QStringList &fallback) const
    {
        const QJsonValue v = m_o.value(key);
        if (v.isUndefined())
            return fallback;
        if (!v.isArray())
            return bad(key, fallback);
        QStringList out;
        for (const QJsonValue item : v.toArray()) {
            if (!item.isString())
                return bad(key, fallback);
            out.append(item.toString());
        }
        return out;
    }

    template <typename Enum, size_t N>
    Enum enumValue(const QString &key, const EnumTable<Enum, N> &table, Enum fallback) const
    {
        const QJsonValue v = m_o.value(key);
        if (v.isUndefined())
            return fallback;
        if (v.isString()) {
            for (const auto &[e, text] : table) {
                if (v.toString() == QLatin1String(text))
                    return e;
            }
        }
        return bad(key, fallback);
    }

    static std::optional<int> toInt(const QJsonValue &v, int min, int max)
    {
        if (!v.isDouble())
            return std::nullopt;
        const double d = v.toDouble();
        if (!std::isfinite(d) || d != std::floor(d) || d < min || d > max)
            return std::nullopt;
        return int(d);
    }

    template <typename T>
    T bad(const QString &key, T fallback) const
    {
        if (m_warnings)
            m_warnings->append(QStringLiteral("Bad value for '%1' in rule %2").arg(key, m_type));
        return fallback;
    }

private:
    const QJsonObject &m_o;
    QString m_type;
    QStringList *m_warnings;
};

template <typename Enum, size_t N>
QString enumToString(const EnumTable<Enum, N> &table, Enum value)
{
    for (const auto &[e, text] : table) {
        if (e == value)
            return QString::fromLatin1(text);
    }
    return QString();
}

} // namespace ctrm::optionjson
