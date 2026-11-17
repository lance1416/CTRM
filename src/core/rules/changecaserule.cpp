// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/changecaserule.h"

#include "rules/graphemes.h"
#include "rules/optionjson.h"

#include <QJsonArray>
#include <QSet>

namespace ctrm {

namespace {

constexpr optionjson::EnumTable<ChangeCaseRule::Case, 5> kCases{{
        {ChangeCaseRule::Case::Lower, "lower"},
        {ChangeCaseRule::Case::Upper, "upper"},
        {ChangeCaseRule::Case::Title, "title"},
        {ChangeCaseRule::Case::Sentence, "sentence"},
        {ChangeCaseRule::Case::Invert, "invert"},
}};

const QString kDefaultSeparators = QStringLiteral(" _-.()[]{}");

// The text split into grapheme clusters.
QStringList graphemes(const QString &s)
{
    const QList<int> bounds = graphemeBoundaries(s);
    QStringList out;
    out.reserve(bounds.size() - 1);
    for (qsizetype i = 0; i + 1 < bounds.size(); ++i)
        out.append(s.mid(bounds[i], bounds[i + 1] - bounds[i]));
    return out;
}

bool startsWithLetter(const QString &grapheme)
{
    return !grapheme.isEmpty() && QChar::isLetter(grapheme.toUcs4().value(0));
}

// First grapheme upper case, the rest lower case.
QString capitalize(const QString &word)
{
    const int first = graphemeOffset(word, 1);
    return word.left(first).toUpper() + word.mid(first).toLower();
}

QString titleCase(const QString &text, const QString &separators, const QStringList &small)
{
    QSet<QString> separatorSet;
    for (const QString &g : graphemes(separators))
        separatorSet.insert(g);
    QSet<QString> smallSet;
    for (const QString &w : small)
        smallSet.insert(w.toCaseFolded());

    QString out;
    QString word;
    bool firstWord = true;
    const auto flushWord = [&] {
        if (word.isEmpty())
            return;
        if (!firstWord && smallSet.contains(word.toCaseFolded()))
            out += word.toLower();
        else
            out += capitalize(word);
        firstWord = false;
        word.clear();
    };
    for (const QString &g : graphemes(text)) {
        if (separatorSet.contains(g)) {
            flushWord();
            out += g;
        } else {
            word += g;
        }
    }
    flushWord();
    return out;
}

// All lower case, then the first letter upper case.
QString sentenceCase(const QString &text)
{
    QStringList parts = graphemes(text.toLower());
    for (QString &g : parts) {
        if (startsWithLetter(g)) {
            g = g.toUpper();
            break;
        }
    }
    return parts.join(QString());
}

QString invertCase(const QString &text)
{
    QString out;
    for (const QString &g : graphemes(text)) {
        const QString lower = g.toLower();
        out += lower == g ? g.toUpper() : lower;
    }
    return out;
}

} // namespace

QStringList ChangeCaseRule::englishSmallWords()
{
    return {QStringLiteral("a"),   QStringLiteral("an"),  QStringLiteral("and"),
            QStringLiteral("as"),  QStringLiteral("at"),  QStringLiteral("but"),
            QStringLiteral("by"),  QStringLiteral("for"), QStringLiteral("in"),
            QStringLiteral("nor"), QStringLiteral("of"),  QStringLiteral("on"),
            QStringLiteral("or"),  QStringLiteral("the"), QStringLiteral("to"),
            QStringLiteral("up"),  QStringLiteral("via")};
}

QString ChangeCaseRule::type() const
{
    return QStringLiteral("changeCase");
}

std::unique_ptr<Rule> ChangeCaseRule::clone() const
{
    return std::make_unique<ChangeCaseRule>(*this);
}

QStringList ChangeCaseRule::optionKeys() const
{
    return {QStringLiteral("case"), QStringLiteral("separators"), QStringLiteral("smallWords"),
            QStringLiteral("builtInSmallWords")};
}

QJsonObject ChangeCaseRule::optionsToJson() const
{
    return {
            {QStringLiteral("case"), optionjson::enumToString(kCases, caseMode)},
            {QStringLiteral("separators"), separators},
            {QStringLiteral("smallWords"), QJsonArray::fromStringList(smallWords)},
            {QStringLiteral("builtInSmallWords"), builtInSmallWords},
    };
}

void ChangeCaseRule::optionsFromJson(const QJsonObject &o, QStringList *warnings)
{
    const optionjson::Reader r(o, type(), warnings);
    caseMode = r.enumValue(QStringLiteral("case"), kCases, Case::Lower);
    separators = r.string(QStringLiteral("separators"), kDefaultSeparators);
    smallWords = r.stringList(QStringLiteral("smallWords"), {});
    builtInSmallWords = r.boolean(QStringLiteral("builtInSmallWords"), false);
}

QString ChangeCaseRule::transform(const QString &text, const RuleContext &) const
{
    switch (caseMode) {
    case Case::Lower:
        return text.toLower();
    case Case::Upper:
        return text.toUpper();
    case Case::Title: {
        QStringList small = smallWords;
        if (builtInSmallWords)
            small += englishSmallWords();
        return titleCase(text, separators, small);
    }
    case Case::Sentence:
        return sentenceCase(text);
    case Case::Invert:
        return invertCase(text);
    }
    Q_UNREACHABLE_RETURN(text);
}

} // namespace ctrm
