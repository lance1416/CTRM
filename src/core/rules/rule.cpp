// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/rule.h"

namespace ctrm {

QString targetToString(Target t)
{
    switch (t) {
    case Target::Stem:
        return QStringLiteral("stem");
    case Target::Extension:
        return QStringLiteral("extension");
    case Target::Name:
        return QStringLiteral("name");
    }
    Q_UNREACHABLE_RETURN(QString());
}

std::optional<Target> targetFromString(const QString &s)
{
    if (s == QLatin1String("stem"))
        return Target::Stem;
    if (s == QLatin1String("extension"))
        return Target::Extension;
    if (s == QLatin1String("name"))
        return Target::Name;
    return std::nullopt;
}

std::optional<RuleProblem> Rule::problem() const
{
    return std::nullopt;
}

bool Rule::hasFixedTarget() const
{
    return false;
}

void Rule::apply(NameParts &parts, const RuleContext &ctx) const
{
    switch (target) {
    case Target::Stem:
        parts.stem = transform(parts.stem, ctx);
        break;
    case Target::Extension:
        parts.extension = transform(parts.extension, ctx);
        if (!parts.extension.isEmpty())
            parts.hasDot = true;
        break;
    case Target::Name:
        parts = splitName(transform(joinName(parts), ctx), ctx.isFolder, ctx.folderExtensions);
        break;
    }
}

} // namespace ctrm
