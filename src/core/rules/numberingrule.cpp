// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/numberingrule.h"

#include "rules/graphemes.h"
#include "rules/ruleoptions.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace ctrm {

namespace {

constexpr std::array<std::pair<NumberingRule::Position, const char *>, 4> kPositions{{
        {NumberingRule::Position::Prefix, "prefix"},
        {NumberingRule::Position::Suffix, "suffix"},
        {NumberingRule::Position::Index, "index"},
        {NumberingRule::Position::Replace, "replace"},
}};

constexpr std::array<std::pair<NumberingRule::Scope, const char *>, 2> kScopes{{
        {NumberingRule::Scope::Checked, "checked"},
        {NumberingRule::Scope::All, "all"},
}};

constexpr int kIntMax = std::numeric_limits<int>::max();
constexpr int kMaxPadding = 10;

// The number in ASCII digits. 64 bits, so start + step * n cannot overflow for int inputs.
qint64 numberAt(int start, int step, qint64 n)
{
    return qint64(start) + qint64(step) * n;
}

} // namespace

int NumberingRule::autoDigits(int start, int step, int numberedCount)
{
    const qint64 largest = numberAt(start, step, std::max(numberedCount, 1) - 1);
    return int(QString::number(largest).size());
}

QString NumberingRule::type() const
{
    return QStringLiteral("numbering");
}

std::unique_ptr<Rule> NumberingRule::clone() const
{
    return std::make_unique<NumberingRule>(*this);
}

QStringList NumberingRule::optionKeys() const
{
    return {QStringLiteral("start"),    QStringLiteral("step"),      QStringLiteral("padding"),
            QStringLiteral("position"), QStringLiteral("separator"), QStringLiteral("index"),
            QStringLiteral("fromEnd"),  QStringLiteral("scope")};
}

QJsonObject NumberingRule::optionsToJson() const
{
    const QJsonValue paddingValue =
            padding < 0 ? QJsonValue(QStringLiteral("auto")) : QJsonValue(padding);
    return {
            {QStringLiteral("start"), start},
            {QStringLiteral("step"), step},
            {QStringLiteral("padding"), paddingValue},
            {QStringLiteral("position"), ruleoptions::enumText(position, kPositions)},
            {QStringLiteral("separator"), separator},
            {QStringLiteral("index"), index},
            {QStringLiteral("fromEnd"), fromEnd},
            {QStringLiteral("scope"), ruleoptions::enumText(scope, kScopes)},
    };
}

void NumberingRule::optionsFromJson(const QJsonObject &o, QStringList *warnings)
{
    const QString t = type();
    start = ruleoptions::readInt(o, QStringLiteral("start"), 0, kIntMax, t, warnings).value_or(1);
    step = ruleoptions::readInt(o, QStringLiteral("step"), 1, kIntMax, t, warnings).value_or(1);
    const QString paddingKey = QStringLiteral("padding");
    if (o.value(paddingKey) == QJsonValue(QStringLiteral("auto")))
        padding = -1;
    else
        padding = ruleoptions::readInt(o, paddingKey, 0, kMaxPadding, t, warnings).value_or(-1);
    position =
            ruleoptions::readEnum<Position>(o, QStringLiteral("position"), kPositions, t, warnings)
                    .value_or(Position::Suffix);
    separator = ruleoptions::readString(o, QStringLiteral("separator"), t, warnings)
                        .value_or(QStringLiteral("_"));
    index = ruleoptions::readInt(o, QStringLiteral("index"), 0, kIntMax, t, warnings).value_or(0);
    fromEnd = ruleoptions::readBool(o, QStringLiteral("fromEnd"), t, warnings).value_or(false);
    scope = ruleoptions::readEnum<Scope>(o, QStringLiteral("scope"), kScopes, t, warnings)
                    .value_or(Scope::Checked);
}

QString NumberingRule::transform(const QString &text, const RuleContext &ctx) const
{
    const bool all = scope == Scope::All;
    const int n = all ? ctx.index : ctx.includedIndex;
    if (n < 0)
        return text; // excluded item, scope Checked: no number
    const int numberedCount = all ? ctx.count : ctx.includedCount;
    const int digits = padding < 0 ? autoDigits(start, step, numberedCount) : padding;
    const QString number =
            QString::number(numberAt(start, step, n)).rightJustified(digits, QLatin1Char('0'));

    switch (position) {
    case Position::Replace:
        return number;
    case Position::Prefix:
        return text.isEmpty() ? number : number + separator + text;
    case Position::Suffix:
        return text.isEmpty() ? number : text + separator + number;
    case Position::Index: {
        const int count = graphemeCount(text);
        const int at = fromEnd ? count - std::min(index, count) : std::min(index, count);
        const int offset = graphemeOffset(text, at);
        const QString before = text.left(offset);
        const QString after = text.mid(offset);
        if (!after.isEmpty())
            return before + number + separator + after;
        if (!before.isEmpty())
            return before + separator + number;
        return number;
    }
    }
    Q_UNREACHABLE_RETURN(text);
}

} // namespace ctrm
