// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/extensionrule.h"

#include "rules/ruleoptions.h"

#include <array>
#include <utility>

namespace ctrm {

namespace {

using Action = ExtensionRule::Action;

const std::array<std::pair<Action, QLatin1StringView>, 5> kActions = {{
        {Action::Set, QLatin1StringView("set")},
        {Action::Remove, QLatin1StringView("remove")},
        {Action::Lower, QLatin1StringView("lower")},
        {Action::Upper, QLatin1StringView("upper")},
        {Action::Add, QLatin1StringView("add")},
}};

QString withoutLeadingDot(const QString &s)
{
    return s.startsWith(u'.') ? s.mid(1) : s;
}

} // namespace

ExtensionRule::ExtensionRule()
{
    target = Target::Extension;
}

QString ExtensionRule::type() const
{
    return QStringLiteral("extension");
}

std::unique_ptr<Rule> ExtensionRule::clone() const
{
    return std::make_unique<ExtensionRule>(*this);
}

bool ExtensionRule::hasFixedTarget() const
{
    return true;
}

bool ExtensionRule::matches(const QString &extension) const
{
    bool anyEntry = false;
    const QString folded = extension.toCaseFolded();
    for (const QString &raw : onlyIfMatches.split(u';')) {
        const QString entry = withoutLeadingDot(raw.trimmed());
        if (entry.isEmpty())
            continue;
        anyEntry = true;
        if (!extension.isEmpty() && entry.toCaseFolded() == folded)
            return true;
    }
    return !anyEntry;
}

void ExtensionRule::apply(NameParts &parts, const RuleContext &ctx) const
{
    if (ctx.isFolder && !ctx.folderExtensions)
        return;
    if (!matches(parts.extension))
        return;

    const QString newExt = withoutLeadingDot(value);
    switch (action) {
    case Action::Lower:
        parts.extension = parts.extension.toLower();
        break;
    case Action::Upper:
        parts.extension = parts.extension.toUpper();
        break;
    case Action::Remove:
        parts.extension.clear();
        parts.hasDot = false;
        break;
    case Action::Set:
        if (newExt.isEmpty())
            return;
        parts.extension = newExt;
        parts.hasDot = true;
        break;
    case Action::Add:
        if (newExt.isEmpty())
            return;
        if (!parts.extension.isEmpty())
            parts.stem += u'.' + parts.extension;
        parts.extension = newExt;
        parts.hasDot = true;
        break;
    }
}

QString ExtensionRule::transform(const QString &text, const RuleContext &) const
{
    // Not used: apply() does all the work, because the rule can add or remove the dot.
    return text;
}

QStringList ExtensionRule::optionKeys() const
{
    return {QStringLiteral("action"), QStringLiteral("value"), QStringLiteral("onlyIfMatches")};
}

QJsonObject ExtensionRule::optionsToJson() const
{
    return {
            {QStringLiteral("action"), ruleoptions::enumText(action, kActions)},
            {QStringLiteral("value"), value},
            {QStringLiteral("onlyIfMatches"), onlyIfMatches},
    };
}

void ExtensionRule::optionsFromJson(const QJsonObject &o, QStringList *warnings)
{
    const QString t = type();
    action = ruleoptions::readEnum<Action>(o, QStringLiteral("action"), kActions, t, warnings)
                     .value_or(Action::Lower);
    value = ruleoptions::readString(o, QStringLiteral("value"), t, warnings).value_or(QString());
    onlyIfMatches = ruleoptions::readString(o, QStringLiteral("onlyIfMatches"), t, warnings)
                            .value_or(QString());
}

} // namespace ctrm
