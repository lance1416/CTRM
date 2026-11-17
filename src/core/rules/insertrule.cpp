// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/insertrule.h"

#include "rules/graphemes.h"
#include "rules/ruleoptions.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace ctrm {

namespace {

const QString kType = QStringLiteral("insert");
constexpr int kIntMax = std::numeric_limits<int>::max();

const std::array<std::pair<InsertRule::Position, QLatin1StringView>, 3> kPositions = {{
        {InsertRule::Position::Prefix, QLatin1StringView("prefix")},
        {InsertRule::Position::Suffix, QLatin1StringView("suffix")},
        {InsertRule::Position::Index, QLatin1StringView("index")},
}};

} // namespace

QString InsertRule::type() const
{
    return kType;
}

std::unique_ptr<Rule> InsertRule::clone() const
{
    return std::make_unique<InsertRule>(*this);
}

QStringList InsertRule::optionKeys() const
{
    return {QStringLiteral("text"), QStringLiteral("position"), QStringLiteral("index"),
            QStringLiteral("fromEnd")};
}

QJsonObject InsertRule::optionsToJson() const
{
    return QJsonObject{
            {QStringLiteral("text"), text},
            {QStringLiteral("position"), ruleoptions::enumText(position, kPositions)},
            {QStringLiteral("index"), index},
            {QStringLiteral("fromEnd"), fromEnd},
    };
}

void InsertRule::optionsFromJson(const QJsonObject &o, QStringList *warnings)
{
    using namespace ruleoptions;
    const InsertRule defaults;
    text = readString(o, QStringLiteral("text"), kType, warnings).value_or(defaults.text);
    position = readEnum<Position>(o, QStringLiteral("position"), kPositions, kType, warnings)
                       .value_or(defaults.position);
    index = readInt(o, QStringLiteral("index"), 0, kIntMax, kType, warnings)
                    .value_or(defaults.index);
    fromEnd = readBool(o, QStringLiteral("fromEnd"), kType, warnings).value_or(defaults.fromEnd);
}

QString InsertRule::transform(const QString &input, const RuleContext &) const
{
    if (text.isEmpty())
        return input;
    switch (position) {
    case Position::Prefix:
        return text + input;
    case Position::Suffix:
        return input + text;
    case Position::Index:
        break;
    }
    const int length = graphemeCount(input);
    const int clamped = std::clamp(index, 0, length);
    const int grapheme = fromEnd ? length - clamped : clamped;
    QString result = input;
    result.insert(graphemeOffset(input, grapheme), text);
    return result;
}

} // namespace ctrm
