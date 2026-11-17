// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/removerule.h"

#include "rules/graphemes.h"
#include "rules/ruleoptions.h"

#include <QChar>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QStringView>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace ctrm {

namespace {

const QString kType = QStringLiteral("remove");
constexpr int kIntMax = std::numeric_limits<int>::max();

const std::array<std::pair<RemoveRule::Mode, QLatin1StringView>, 3> kModes = {{
        {RemoveRule::Mode::Range, QLatin1StringView("range")},
        {RemoveRule::Mode::Characters, QLatin1StringView("characters")},
        {RemoveRule::Mode::Between, QLatin1StringView("between")},
}};

const std::array<std::pair<RemoveRule::Occurrence, QLatin1StringView>, 2> kOccurrences = {{
        {RemoveRule::Occurrence::All, QLatin1StringView("all")},
        {RemoveRule::Occurrence::First, QLatin1StringView("first")},
}};

// JSON order of the classes (PRD 8.2).
const std::array<std::pair<RemoveRule::Class, QLatin1StringView>, 7> kClasses = {{
        {RemoveRule::Digits, QLatin1StringView("digits")},
        {RemoveRule::Letters, QLatin1StringView("letters")},
        {RemoveRule::Spaces, QLatin1StringView("spaces")},
        {RemoveRule::Punctuation, QLatin1StringView("punctuation")},
        {RemoveRule::Symbols, QLatin1StringView("symbols")},
        {RemoveRule::NonAscii, QLatin1StringView("nonAscii")},
        {RemoveRule::Custom, QLatin1StringView("custom")},
}};

// The grapheme clusters of s, as strings.
QStringList clusters(const QString &s)
{
    const QList<int> bounds = graphemeBoundaries(s);
    QStringList result;
    for (qsizetype i = 0; i + 1 < bounds.size(); ++i)
        result.append(s.mid(bounds.at(i), bounds.at(i + 1) - bounds.at(i)));
    return result;
}

// True when the base code point (the first one) of the cluster is in one of the classes.
bool baseInClasses(QStringView cluster, quint32 classes)
{
    const char32_t base = cluster.toUcs4().value(0);
    if ((classes & RemoveRule::Spaces) && QChar::isSpace(base))
        return true;
    switch (QChar::category(base)) {
    case QChar::Number_DecimalDigit:
        return classes & RemoveRule::Digits;
    case QChar::Letter_Uppercase:
    case QChar::Letter_Lowercase:
    case QChar::Letter_Titlecase:
    case QChar::Letter_Modifier:
    case QChar::Letter_Other:
        return classes & RemoveRule::Letters;
    case QChar::Punctuation_Connector:
    case QChar::Punctuation_Dash:
    case QChar::Punctuation_Open:
    case QChar::Punctuation_Close:
    case QChar::Punctuation_InitialQuote:
    case QChar::Punctuation_FinalQuote:
    case QChar::Punctuation_Other:
        return classes & RemoveRule::Punctuation;
    case QChar::Symbol_Math:
    case QChar::Symbol_Currency:
    case QChar::Symbol_Modifier:
    case QChar::Symbol_Other:
        return classes & RemoveRule::Symbols;
    default:
        return false;
    }
}

// Removes each code point above 0x7F.
QString withoutNonAscii(QStringView s)
{
    QString result;
    for (const char32_t c : s.toUcs4()) {
        if (c <= 0x7F)
            result.append(QChar(char16_t(c)));
    }
    return result;
}

} // namespace

QString RemoveRule::type() const
{
    return kType;
}

std::unique_ptr<Rule> RemoveRule::clone() const
{
    return std::make_unique<RemoveRule>(*this);
}

QStringList RemoveRule::optionKeys() const
{
    return {QStringLiteral("mode"),           QStringLiteral("from"),
            QStringLiteral("count"),          QStringLiteral("fromEnd"),
            QStringLiteral("classes"),        QStringLiteral("custom"),
            QStringLiteral("open"),           QStringLiteral("close"),
            QStringLiteral("keepDelimiters"), QStringLiteral("occurrence")};
}

QJsonObject RemoveRule::optionsToJson() const
{
    QJsonArray classArray;
    for (const auto &[bit, text] : kClasses) {
        if (classes & bit)
            classArray.append(QString(text));
    }
    return QJsonObject{
            {QStringLiteral("mode"), ruleoptions::enumText(mode, kModes)},
            {QStringLiteral("from"), from},
            {QStringLiteral("count"), count},
            {QStringLiteral("fromEnd"), fromEnd},
            {QStringLiteral("classes"), classArray},
            {QStringLiteral("custom"), custom},
            {QStringLiteral("open"), open},
            {QStringLiteral("close"), close},
            {QStringLiteral("keepDelimiters"), keepDelimiters},
            {QStringLiteral("occurrence"), ruleoptions::enumText(occurrence, kOccurrences)},
    };
}

void RemoveRule::optionsFromJson(const QJsonObject &o, QStringList *warnings)
{
    using namespace ruleoptions;
    const RemoveRule d;
    mode = readEnum<Mode>(o, QStringLiteral("mode"), kModes, kType, warnings).value_or(d.mode);
    from = readInt(o, QStringLiteral("from"), 0, kIntMax, kType, warnings).value_or(d.from);
    count = readInt(o, QStringLiteral("count"), 0, kIntMax, kType, warnings).value_or(d.count);
    fromEnd = readBool(o, QStringLiteral("fromEnd"), kType, warnings).value_or(d.fromEnd);

    // An unknown entry gives a warning; the known entries stay.
    classes = d.classes;
    const QString classesKey = QStringLiteral("classes");
    if (const QJsonValue v = o.value(classesKey); v.isArray()) {
        for (const QJsonValue entry : v.toArray()) {
            const auto *match = std::find_if(kClasses.begin(), kClasses.end(), [&](const auto &c) {
                return entry.isString() && entry.toString() == c.second;
            });
            if (match != kClasses.end())
                classes |= match->first;
            else
                warnBadValue(warnings, classesKey, kType);
        }
    } else if (!v.isUndefined()) {
        warnBadValue(warnings, classesKey, kType);
    }

    custom = readString(o, QStringLiteral("custom"), kType, warnings).value_or(d.custom);
    open = readString(o, QStringLiteral("open"), kType, warnings).value_or(d.open);
    close = readString(o, QStringLiteral("close"), kType, warnings).value_or(d.close);
    keepDelimiters = readBool(o, QStringLiteral("keepDelimiters"), kType, warnings)
                             .value_or(d.keepDelimiters);
    occurrence =
            readEnum<Occurrence>(o, QStringLiteral("occurrence"), kOccurrences, kType, warnings)
                    .value_or(d.occurrence);
}

QString RemoveRule::transform(const QString &input, const RuleContext &) const
{
    switch (mode) {
    case Mode::Range: {
        const QList<int> bounds = graphemeBoundaries(input);
        const int length = int(bounds.size()) - 1;
        const int start = std::clamp(from, 0, length);
        const int n = std::clamp(count, 0, length - start);
        // fromEnd: the range [start, start + n) counts back from the last grapheme.
        const int first = fromEnd ? length - start - n : start;
        const int begin = bounds.at(first);
        const int end = bounds.at(first + n);
        return input.left(begin) + input.mid(end);
    }
    case Mode::Characters: {
        if (classes == 0)
            return input;
        QSet<QString> customClusters;
        if (classes & Custom) {
            for (const QString &c : clusters(custom))
                customClusters.insert(c);
        }
        QString result;
        for (const QString &cluster : clusters(input)) {
            if (baseInClasses(cluster, classes) || customClusters.contains(cluster))
                continue;
            if (classes & NonAscii)
                result += withoutNonAscii(cluster);
            else
                result += cluster;
        }
        return result;
    }
    case Mode::Between: {
        if (open.isEmpty() || close.isEmpty())
            return input;
        const QList<int> bounds = graphemeBoundaries(input);
        const QSet<int> isBoundary(bounds.begin(), bounds.end());
        // Finds `needle` at or after `pos` so that it starts and ends on cluster boundaries.
        const auto findAligned = [&](const QString &needle, qsizetype pos) -> qsizetype {
            for (qsizetype i = input.indexOf(needle, pos); i >= 0;
                 i = input.indexOf(needle, i + 1)) {
                if (isBoundary.contains(int(i)) && isBoundary.contains(int(i + needle.size())))
                    return i;
            }
            return -1;
        };
        QString result;
        qsizetype pos = 0;
        while (pos < input.size()) {
            const qsizetype openAt = findAligned(open, pos);
            if (openAt < 0)
                break;
            const qsizetype inner = openAt + open.size();
            const qsizetype closeAt = findAligned(close, inner);
            if (closeAt < 0)
                break;
            result += QStringView(input).sliced(pos, openAt - pos);
            if (keepDelimiters)
                result += open + close;
            pos = closeAt + close.size();
            if (occurrence == Occurrence::First)
                break;
        }
        result += QStringView(input).sliced(pos);
        return result;
    }
    }
    Q_UNREACHABLE_RETURN(input);
}

} // namespace ctrm
