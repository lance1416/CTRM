// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/ruleregistry.h"

#include "rules/changecaserule.h"
#include "rules/cleanuprule.h"
#include "rules/extensionrule.h"
#include "rules/insertrule.h"
#include "rules/numberingrule.h"
#include "rules/removerule.h"
#include "rules/replacerule.h"
#include "rules/ruleoptions.h"
#include "rules/unsupportedrule.h"

#include <QSet>

namespace ctrm {

namespace {

const QString kType = QStringLiteral("type");
const QString kEnabled = QStringLiteral("enabled");
const QString kTarget = QStringLiteral("target");

void warn(QStringList *warnings, const QString &text)
{
    if (warnings)
        warnings->append(text);
}

} // namespace

QStringList ruleTypes()
{
    return {QStringLiteral("replace"),  QStringLiteral("insert"),     QStringLiteral("remove"),
            QStringLiteral("cleanup"),  QStringLiteral("changeCase"), QStringLiteral("extension"),
            QStringLiteral("numbering")};
}

std::unique_ptr<Rule> createRule(const QString &type)
{
    if (type == QLatin1String("replace"))
        return std::make_unique<ReplaceRule>();
    if (type == QLatin1String("insert"))
        return std::make_unique<InsertRule>();
    if (type == QLatin1String("remove"))
        return std::make_unique<RemoveRule>();
    if (type == QLatin1String("cleanup"))
        return std::make_unique<CleanUpRule>();
    if (type == QLatin1String("changeCase"))
        return std::make_unique<ChangeCaseRule>();
    if (type == QLatin1String("extension"))
        return std::make_unique<ExtensionRule>();
    if (type == QLatin1String("numbering"))
        return std::make_unique<NumberingRule>();
    return nullptr;
}

std::unique_ptr<Rule> ruleFromJson(const QJsonObject &o, QStringList *warnings)
{
    std::unique_ptr<Rule> rule = createRule(o.value(kType).toString());
    if (!rule) {
        rule = std::make_unique<UnsupportedRule>(o);
        rule->enabled = ruleoptions::readBool(o, kEnabled, rule->type(), warnings).value_or(true);
        return rule;
    }

    const QString t = rule->type();
    rule->enabled = ruleoptions::readBool(o, kEnabled, t, warnings).value_or(true);
    if (rule->hasFixedTarget()) {
        if (o.contains(kTarget))
            warn(warnings, QStringLiteral("Ignored key '%1' in rule %2").arg(kTarget, t));
    } else if (const auto text = ruleoptions::readString(o, kTarget, t, warnings)) {
        if (const auto target = targetFromString(*text))
            rule->target = *target;
        else
            ruleoptions::warnBadValue(warnings, kTarget, t);
    }
    rule->optionsFromJson(o, warnings);

    const QStringList options = rule->optionKeys();
    QSet<QString> known(options.cbegin(), options.cend());
    known << kType << kEnabled << kTarget;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (known.contains(it.key()))
            continue;
        rule->unknownKeys.insert(it.key(), it.value());
        warn(warnings, QStringLiteral("Unknown key '%1' in rule %2").arg(it.key(), t));
    }
    return rule;
}

QJsonObject ruleToJson(const Rule &r)
{
    QJsonObject o = r.optionsToJson();
    o.insert(kType, r.type());
    o.insert(kEnabled, r.enabled);
    if (!r.hasFixedTarget())
        o.insert(kTarget, targetToString(r.target));
    for (auto it = r.unknownKeys.constBegin(); it != r.unknownKeys.constEnd(); ++it) {
        if (!o.contains(it.key()))
            o.insert(it.key(), it.value());
    }
    return o;
}

QJsonArray chainRulesToJson(const RuleChain &chain)
{
    QJsonArray rules;
    for (int i = 0; i < chain.size(); ++i)
        rules.append(ruleToJson(*chain.at(i)));
    return rules;
}

QJsonObject chainOptionsToJson(const ChainOptions &o)
{
    return {
            {QStringLiteral("portableNames"), o.portableNames},
            {QStringLiteral("autoResolveDuplicates"), o.autoResolveDuplicates},
            {QStringLiteral("folderExtensions"), o.folderExtensions},
    };
}

ChainOptions chainOptionsFromJson(const QJsonObject &o, QStringList *warnings)
{
    ChainOptions options;
    const std::pair<QString, bool *> keys[] = {
            {QStringLiteral("portableNames"), &options.portableNames},
            {QStringLiteral("autoResolveDuplicates"), &options.autoResolveDuplicates},
            {QStringLiteral("folderExtensions"), &options.folderExtensions},
    };
    QSet<QString> known;
    for (const auto &[key, field] : keys) {
        known.insert(key);
        if (!o.contains(key))
            continue;
        const QJsonValue v = o.value(key);
        if (v.isBool())
            *field = v.toBool();
        else
            warn(warnings, QStringLiteral("Bad value for '%1' in options").arg(key));
    }
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (!known.contains(it.key()))
            warn(warnings, QStringLiteral("Unknown key '%1' in options").arg(it.key()));
    }
    return options;
}

} // namespace ctrm
