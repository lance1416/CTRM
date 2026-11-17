// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/replacerule.h"

#include "rules/graphemes.h"
#include "rules/ruleoptions.h"

#include <QList>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

#include <algorithm>
#include <array>
#include <utility>

namespace ctrm {

namespace {

constexpr std::array<std::pair<ReplaceRule::Mode, const char *>, 3> kModes{{
        {ReplaceRule::Mode::Literal, "literal"},
        {ReplaceRule::Mode::Regex, "regex"},
        {ReplaceRule::Mode::Wildcard, "wildcard"},
}};

constexpr std::array<std::pair<ReplaceRule::Occurrence, const char *>, 3> kOccurrences{{
        {ReplaceRule::Occurrence::All, "all"},
        {ReplaceRule::Occurrence::First, "first"},
        {ReplaceRule::Occurrence::Last, "last"},
}};

// Wildcard: '*' = any text, '?' = one grapheme cluster. All other characters are literal.
QString wildcardToPattern(const QString &find)
{
    QString pattern;
    QString literal;
    const auto flushLiteral = [&] {
        pattern += QRegularExpression::escape(literal);
        literal.clear();
    };
    for (const QChar c : find) {
        if (c == QLatin1Char('*')) {
            flushLiteral();
            pattern += QStringLiteral(".*");
        } else if (c == QLatin1Char('?')) {
            flushLiteral();
            pattern += QStringLiteral("\\X");
        } else {
            literal += c;
        }
    }
    flushLiteral();
    return pattern;
}

QRegularExpression buildRegex(const ReplaceRule &rule)
{
    QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
    if (!rule.caseSensitive)
        options |= QRegularExpression::CaseInsensitiveOption;

    QString pattern;
    switch (rule.mode) {
    case ReplaceRule::Mode::Literal:
        pattern = QRegularExpression::escape(rule.find);
        // Unicode word boundaries. The lookarounds also work when find starts or ends
        // with a character that is not a word character.
        if (rule.wholeWord)
            pattern = QStringLiteral("(?<!\\w)(?:%1)(?!\\w)").arg(pattern);
        break;
    case ReplaceRule::Mode::Regex:
        pattern = rule.find;
        break;
    case ReplaceRule::Mode::Wildcard:
        pattern = wildcardToPattern(rule.find);
        options |= QRegularExpression::DotMatchesEverythingOption;
        break;
    }
    return QRegularExpression(pattern, options);
}

bool isAsciiDigit(QChar c)
{
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

// Expands $0-$99, ${name} and $$. Everything else is literal text.
QString expandReplacement(const QString &replacement, const QRegularExpressionMatch &m,
                          const QRegularExpression &re)
{
    const int groups = re.captureCount();
    const QStringList names = re.namedCaptureGroups();
    QString out;
    const qsizetype n = replacement.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = replacement.at(i);
        if (c != QLatin1Char('$') || i + 1 >= n) {
            out += c;
            continue;
        }
        const QChar next = replacement.at(i + 1);
        if (next == QLatin1Char('$')) {
            out += QLatin1Char('$');
            ++i;
            continue;
        }
        if (isAsciiDigit(next)) {
            int group = next.digitValue();
            qsizetype used = 1;
            if (i + 2 < n && isAsciiDigit(replacement.at(i + 2))) {
                const int two = group * 10 + replacement.at(i + 2).digitValue();
                if (two <= groups) {
                    group = two;
                    used = 2;
                }
            }
            if (group <= groups) {
                out += m.captured(group);
                i += used;
            } else {
                out += c; // not a group: literal '$'
            }
            continue;
        }
        if (next == QLatin1Char('{')) {
            const qsizetype close = replacement.indexOf(QLatin1Char('}'), i + 2);
            if (close > i + 2) {
                const QString name = replacement.mid(i + 2, close - i - 2);
                if (names.contains(name)) {
                    out += m.captured(name);
                    i = close;
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}

} // namespace

QString ReplaceRule::type() const
{
    return QStringLiteral("replace");
}

std::unique_ptr<Rule> ReplaceRule::clone() const
{
    return std::make_unique<ReplaceRule>(*this);
}

std::optional<RuleProblem> ReplaceRule::problem() const
{
    if (mode != Mode::Regex || find.isEmpty())
        return std::nullopt;
    const QRegularExpression re = buildRegex(*this);
    if (re.isValid())
        return std::nullopt;
    // Grapheme index of the character at the UTF-16 error offset.
    const qsizetype offset = std::max<qsizetype>(0, re.patternErrorOffset());
    const QList<int> bounds = graphemeBoundaries(find);
    const auto before =
            std::count_if(bounds.cbegin(), bounds.cend(), [offset](int b) { return b <= offset; });
    return RuleProblem{re.errorString(), int(before) - 1, false};
}

QStringList ReplaceRule::optionKeys() const
{
    return {QStringLiteral("find"),       QStringLiteral("replace"),
            QStringLiteral("mode"),       QStringLiteral("caseSensitive"),
            QStringLiteral("occurrence"), QStringLiteral("wholeWord")};
}

QJsonObject ReplaceRule::optionsToJson() const
{
    return {
            {QStringLiteral("find"), find},
            {QStringLiteral("replace"), replace},
            {QStringLiteral("mode"), ruleoptions::enumText(mode, kModes)},
            {QStringLiteral("caseSensitive"), caseSensitive},
            {QStringLiteral("occurrence"), ruleoptions::enumText(occurrence, kOccurrences)},
            {QStringLiteral("wholeWord"), wholeWord},
    };
}

void ReplaceRule::optionsFromJson(const QJsonObject &o, QStringList *warnings)
{
    const QString t = type();
    find = ruleoptions::readString(o, QStringLiteral("find"), t, warnings).value_or(QString());
    replace =
            ruleoptions::readString(o, QStringLiteral("replace"), t, warnings).value_or(QString());
    mode = ruleoptions::readEnum<Mode>(o, QStringLiteral("mode"), kModes, t, warnings)
                   .value_or(Mode::Literal);
    caseSensitive =
            ruleoptions::readBool(o, QStringLiteral("caseSensitive"), t, warnings).value_or(true);
    occurrence = ruleoptions::readEnum<Occurrence>(o, QStringLiteral("occurrence"), kOccurrences, t,
                                                   warnings)
                         .value_or(Occurrence::All);
    wholeWord = ruleoptions::readBool(o, QStringLiteral("wholeWord"), t, warnings).value_or(false);
}

QString ReplaceRule::transform(const QString &text, const RuleContext &) const
{
    if (find.isEmpty())
        return text;
    const QRegularExpression re = buildRegex(*this);
    if (!re.isValid())
        return text; // problem() reports it; the chain blocks the batch

    // All non-overlapping matches, left to right. After an empty match, the search
    // goes on at the next grapheme boundary, so it never loops and never splits a cluster.
    const QList<int> bounds = graphemeBoundaries(text);
    QList<QRegularExpressionMatch> matches;
    qsizetype pos = 0;
    while (pos <= text.size()) {
        QRegularExpressionMatch m = re.match(text, pos);
        if (!m.hasMatch())
            break;
        const qsizetype end = m.capturedEnd();
        const bool empty = m.capturedLength() == 0;
        matches.append(std::move(m));
        if (!empty) {
            pos = end;
            continue;
        }
        const auto nextBound = std::upper_bound(bounds.cbegin(), bounds.cend(), end);
        if (nextBound == bounds.cend())
            break;
        pos = *nextBound;
    }
    if (matches.isEmpty())
        return text;

    if (occurrence == Occurrence::First)
        matches = matches.mid(0, 1);
    else if (occurrence == Occurrence::Last)
        matches = matches.mid(matches.size() - 1);

    QString out;
    qsizetype last = 0;
    for (const QRegularExpressionMatch &m : std::as_const(matches)) {
        out += QStringView(text).mid(last, m.capturedStart() - last);
        out += mode == Mode::Regex ? expandReplacement(replace, m, re) : replace;
        last = m.capturedEnd();
    }
    out += QStringView(text).mid(last);
    return out;
}

} // namespace ctrm
