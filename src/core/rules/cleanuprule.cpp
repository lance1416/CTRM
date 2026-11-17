// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/cleanuprule.h"

#include "rules/graphemes.h"
#include "rules/ruleoptions.h"
#include "validate/namevalidator.h"

#include <array>
#include <utility>

namespace ctrm {

namespace {

using Spaces = CleanUpRule::Spaces;

const std::array<std::pair<Spaces, QLatin1StringView>, 5> kSpaces = {{
        {Spaces::Keep, QLatin1StringView("keep")},
        {Spaces::Underscore, QLatin1StringView("_")},
        {Spaces::Dash, QLatin1StringView("-")},
        {Spaces::Dot, QLatin1StringView(".")},
        {Spaces::Remove, QLatin1StringView("")},
}};

struct Cluster
{
    QString text;
    bool space = false;
};

// The base code point decides: a cluster is a space when its first code point is White_Space.
// QChar::isSpace() is true for exactly the White_Space code points.
bool startsWithSpace(QStringView cluster)
{
    if (cluster.isEmpty())
        return false;
    const QChar c = cluster.front();
    if (c.isHighSurrogate() && cluster.size() > 1 && cluster[1].isLowSurrogate())
        return QChar::isSpace(QChar::surrogateToUcs4(c, cluster[1]));
    return c.isSpace();
}

QList<Cluster> clustersOf(const QString &text)
{
    QList<Cluster> out;
    const QList<int> b = graphemeBoundaries(text);
    for (qsizetype i = 0; i + 1 < b.size(); ++i) {
        const QString piece = text.mid(b[i], b[i + 1] - b[i]);
        out.append({piece, startsWithSpace(piece)});
    }
    return out;
}

} // namespace

QString CleanUpRule::type() const
{
    return QStringLiteral("cleanup");
}

std::unique_ptr<Rule> CleanUpRule::clone() const
{
    return std::make_unique<CleanUpRule>(*this);
}

QString CleanUpRule::transform(const QString &text, const RuleContext &ctx) const
{
    QList<Cluster> clusters = clustersOf(text);

    if (trim) {
        while (!clusters.isEmpty() && clusters.front().space)
            clusters.removeFirst();
        while (!clusters.isEmpty() && clusters.back().space)
            clusters.removeLast();
    }

    if (collapseSpaces) {
        QList<Cluster> collapsed;
        for (qsizetype i = 0; i < clusters.size();) {
            if (!clusters[i].space) {
                collapsed.append(clusters[i++]);
                continue;
            }
            qsizetype end = i;
            while (end < clusters.size() && clusters[end].space)
                ++end;
            if (end - i >= 2)
                collapsed.append({QStringLiteral(" "), true});
            else
                collapsed.append(clusters[i]);
            i = end;
        }
        clusters = std::move(collapsed);
    }

    QString out;
    out.reserve(text.size());
    const QString spaceText = ruleoptions::enumText(spacesTo, kSpaces);
    for (const Cluster &c : clusters) {
        if (c.space && spacesTo != Spaces::Keep)
            out += spaceText;
        else
            out += c.text;
    }

    if (replaceInvalid)
        out = NameValidator(ctx.os, ctx.portableNames).replaceForbidden(out, replacement);
    return out;
}

QStringList CleanUpRule::optionKeys() const
{
    return {QStringLiteral("trim"), QStringLiteral("collapseSpaces"), QStringLiteral("spacesTo"),
            QStringLiteral("replaceInvalid"), QStringLiteral("replacement")};
}

QJsonObject CleanUpRule::optionsToJson() const
{
    return {
            {QStringLiteral("trim"), trim},
            {QStringLiteral("collapseSpaces"), collapseSpaces},
            {QStringLiteral("spacesTo"), ruleoptions::enumText(spacesTo, kSpaces)},
            {QStringLiteral("replaceInvalid"), replaceInvalid},
            {QStringLiteral("replacement"), replacement},
    };
}

void CleanUpRule::optionsFromJson(const QJsonObject &o, QStringList *warnings)
{
    using namespace ruleoptions;
    const QString t = type();
    trim = readBool(o, QStringLiteral("trim"), t, warnings).value_or(true);
    collapseSpaces = readBool(o, QStringLiteral("collapseSpaces"), t, warnings).value_or(true);
    spacesTo = readEnum<Spaces>(o, QStringLiteral("spacesTo"), kSpaces, t, warnings)
                       .value_or(Spaces::Keep);
    replaceInvalid = readBool(o, QStringLiteral("replaceInvalid"), t, warnings).value_or(false);
    replacement =
            readString(o, QStringLiteral("replacement"), t, warnings).value_or(QStringLiteral("_"));
}

} // namespace ctrm
