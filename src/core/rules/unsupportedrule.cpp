// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/unsupportedrule.h"

#include <utility>

namespace ctrm {

UnsupportedRule::UnsupportedRule(QJsonObject original)
    : m_original(std::move(original))
{
    const QJsonValue e = m_original.value(QStringLiteral("enabled"));
    if (e.isBool())
        enabled = e.toBool();
}

QString UnsupportedRule::type() const
{
    return m_original.value(QStringLiteral("type")).toString();
}

std::optional<RuleProblem> UnsupportedRule::problem() const
{
    return RuleProblem{QStringLiteral("Unsupported rule (%1)").arg(type()), -1, true};
}

bool UnsupportedRule::hasFixedTarget() const
{
    return true;
}

std::unique_ptr<Rule> UnsupportedRule::clone() const
{
    return std::make_unique<UnsupportedRule>(*this);
}

QStringList UnsupportedRule::optionKeys() const
{
    return optionsToJson().keys();
}

QJsonObject UnsupportedRule::optionsToJson() const
{
    QJsonObject o = m_original;
    o.remove(QStringLiteral("type"));
    o.remove(QStringLiteral("enabled"));
    return o;
}

void UnsupportedRule::optionsFromJson(const QJsonObject &, QStringList *) {}

QString UnsupportedRule::transform(const QString &text, const RuleContext &) const
{
    return text;
}

} // namespace ctrm
