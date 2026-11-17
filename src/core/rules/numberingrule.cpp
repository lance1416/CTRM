// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/numberingrule.h"

#include "rules/graphemes.h"
#include "rules/optionjson.h"

#include <algorithm>
#include <limits>

namespace ctrm {

namespace {

constexpr optionjson::EnumTable<NumberingRule::Position, 4> kPositions{{
        {NumberingRule::Position::Prefix, "prefix"},
        {NumberingRule::Position::Suffix, "suffix"},
        {NumberingRule::Position::Index, "index"},
        {NumberingRule::Position::Replace, "replace"},
}};

constexpr optionjson::EnumTable<NumberingRule::Scope, 2> kScopes{{
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
            {QStringLiteral("position"), optionjson::enumToString(kPositions, position)},
            {QStringLiteral("separator"), separator},
            {QStringLiteral("index"), index},
            {QStringLiteral("fromEnd"), fromEnd},
            {QStringLiteral("scope"), optionjson::enumToString(kScopes, scope)},
    };
}

void NumberingRule::optionsFromJson(const QJsonObject &o, QStringList *warnings)
{
    const optionjson::Reader r(o, type(), warnings);
    start = r.integer(QStringLiteral("start"), 1, 0, kIntMax);
    step = r.integer(QStringLiteral("step"), 1, 1, kIntMax);

    const QString paddingKey = QStringLiteral("padding");
    const QJsonValue p = o.value(paddingKey);
    if (p.isUndefined() || p == QJsonValue(QStringLiteral("auto")))
        padding = -1;
    else if (const auto n = optionjson::Reader::toInt(p, 0, kMaxPadding))
        padding = *n;
    else
        padding = r.bad(paddingKey, -1);

    position = r.enumValue(QStringLiteral("position"), kPositions, Position::Suffix);
    separator = r.string(QStringLiteral("separator"), QStringLiteral("_"));
    index = r.integer(QStringLiteral("index"), 0, 0, kIntMax);
    fromEnd = r.boolean(QStringLiteral("fromEnd"), false);
    scope = r.enumValue(QStringLiteral("scope"), kScopes, Scope::Checked);
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
