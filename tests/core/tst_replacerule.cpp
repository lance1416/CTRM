// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/replacerule.h"
#include "rules/rulechain.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QtTest>

using namespace ctrm;

namespace {

ReplaceRule makeRule(const QString &find, const QString &replace,
                     ReplaceRule::Mode mode = ReplaceRule::Mode::Literal)
{
    ReplaceRule rule;
    rule.target = Target::Name;
    rule.find = find;
    rule.replace = replace;
    rule.mode = mode;
    return rule;
}

QString run(const ReplaceRule &rule, const QString &name, const RuleContext &ctx = RuleContext{})
{
    RuleChain chain;
    chain.insert(0, rule.clone());
    return chain.apply(name, ctx);
}

} // namespace

class TestReplaceRule : public QObject
{
    Q_OBJECT
private slots:
    void defaults();
    void emptyFindNoEffect();
    void allOccurrences();
    void firstAndLast();
    void caseInsensitiveLiteral();
    void literalEscapesRegex();
    void wholeWord();
    void wholeWordLiteralOnly();
    void regexGroups();
    void regexDollarSyntax();
    void literalReplaceIsLiteral();
    void regexBadPattern();
    void literalAndWildcardHaveNoProblem();
    void regexEmptyMatchNoLoop();
    void regexOccurrenceFirstLast();
    void wildcard();
    void wildcardEscapesRegex();
    void emojiAndCombining();
    void edgeInputs_data();
    void edgeInputs();
    void jsonRoundTrip();
    void jsonBadValuesWarn();
    void cloneCopiesOptions();
};

void TestReplaceRule::defaults()
{
    const ReplaceRule rule;
    QCOMPARE(rule.type(), QStringLiteral("replace"));
    QCOMPARE(rule.find, QString());
    QCOMPARE(rule.replace, QString());
    QCOMPARE(rule.mode, ReplaceRule::Mode::Literal);
    QCOMPARE(rule.caseSensitive, true);
    QCOMPARE(rule.occurrence, ReplaceRule::Occurrence::All);
    QCOMPARE(rule.wholeWord, false);
    QCOMPARE(rule.target, Target::Stem);
    QCOMPARE(rule.enabled, true);
    QCOMPARE(rule.hasFixedTarget(), false);
    QCOMPARE(rule.optionKeys(),
             (QStringList{"find", "replace", "mode", "caseSensitive", "occurrence", "wholeWord"}));
}

void TestReplaceRule::emptyFindNoEffect()
{
    for (auto mode :
         {ReplaceRule::Mode::Literal, ReplaceRule::Mode::Regex, ReplaceRule::Mode::Wildcard}) {
        QCOMPARE(run(makeRule("", "x", mode), "a-b.txt"), QStringLiteral("a-b.txt"));
        QVERIFY(!makeRule("", "x", mode).problem().has_value());
    }
}

void TestReplaceRule::allOccurrences()
{
    QCOMPARE(run(makeRule("-", "_"), "a-b-c"), QStringLiteral("a_b_c")); // AC4.2
    QCOMPARE(run(makeRule("aa", "b"), "aaaaa"), QStringLiteral("bba"));  // non-overlapping
    QCOMPARE(run(makeRule("-", ""), "--x--"), QStringLiteral("x"));
}

void TestReplaceRule::firstAndLast()
{
    ReplaceRule rule = makeRule("-", "_");
    rule.occurrence = ReplaceRule::Occurrence::First;
    QCOMPARE(run(rule, "a-b-c"), QStringLiteral("a_b-c"));
    rule.occurrence = ReplaceRule::Occurrence::Last;
    QCOMPARE(run(rule, "a-b-c"), QStringLiteral("a-b_c"));
    QCOMPARE(run(rule, "abc"), QStringLiteral("abc"));
}

void TestReplaceRule::caseInsensitiveLiteral()
{
    ReplaceRule rule = makeRule("img", "x");
    QCOMPARE(run(rule, "IMG_img"), QStringLiteral("IMG_x"));
    rule.caseSensitive = false;
    QCOMPARE(run(rule, "IMG_img"), QStringLiteral("x_x"));
}

void TestReplaceRule::literalEscapesRegex()
{
    QCOMPARE(run(makeRule("a.b", "x"), "axb a.b"), QStringLiteral("axb x"));
    QCOMPARE(run(makeRule("(1)", "[2]"), "f (1)"), QStringLiteral("f [2]"));
    QCOMPARE(run(makeRule("$", "\\"), "a$b"), QStringLiteral("a\\b"));
}

void TestReplaceRule::wholeWord()
{
    ReplaceRule rule = makeRule("cat", "dog");
    rule.wholeWord = true;
    QCOMPARE(run(rule, "cat catalog"), QStringLiteral("dog catalog"));
    QCOMPARE(run(rule, "bobcat cat_x cat"), QStringLiteral("bobcat cat_x dog"));
    QCOMPARE(run(rule, "café cat"), QStringLiteral("café dog"));
    QCOMPARE(run(rule, "écat cat"), QStringLiteral("écat dog")); // Unicode letter is a word char
}

void TestReplaceRule::wholeWordLiteralOnly()
{
    ReplaceRule rule = makeRule("c.t", "dog", ReplaceRule::Mode::Regex);
    rule.wholeWord = true;
    QCOMPARE(run(rule, "catalog"), QStringLiteral("dogalog"));
}

void TestReplaceRule::regexGroups()
{
    const ReplaceRule rule = makeRule("^(\\d+)_(.*)$", "$2_$1", ReplaceRule::Mode::Regex);
    QCOMPARE(run(rule, "01_song"), QStringLiteral("song_01")); // AC4.3
}

void TestReplaceRule::regexDollarSyntax()
{
    const auto regex = ReplaceRule::Mode::Regex;
    QCOMPARE(run(makeRule("b+", "[$0]", regex), "abbc"), QStringLiteral("a[bb]c"));
    QCOMPARE(run(makeRule("b", "$$", regex), "abc"), QStringLiteral("a$c"));
    QCOMPARE(run(makeRule("(?<x>b)", "<${x}>", regex), "abc"), QStringLiteral("a<b>c"));
    QCOMPARE(run(makeRule("(b)", "\\1", regex), "abc"), QStringLiteral("a\\1c"));
    // Two digits when that group exists, else one digit and a literal digit.
    QCOMPARE(run(makeRule("(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)", "$11", regex), "abcdefghijk"),
             QStringLiteral("k"));
    QCOMPARE(run(makeRule("(a)", "$10", regex), "a"), QStringLiteral("a0"));
    // A reference to a group that does not exist, or a lone '$', stays literal.
    QCOMPARE(run(makeRule("(a)", "$2", regex), "a"), QStringLiteral("$2"));
    QCOMPARE(run(makeRule("a", "${nope}", regex), "a"), QStringLiteral("${nope}"));
    QCOMPARE(run(makeRule("a", "x$", regex), "a"), QStringLiteral("x$"));
    QCOMPARE(run(makeRule("a", "${x", regex), "a"), QStringLiteral("${x"));
    // A group that did not take part in the match gives empty text.
    QCOMPARE(run(makeRule("(x)?a", "[$1]", regex), "a"), QStringLiteral("[]"));
}

void TestReplaceRule::literalReplaceIsLiteral()
{
    QCOMPARE(run(makeRule("b", "$0$$"), "abc"), QStringLiteral("a$0$$c"));
    QCOMPARE(run(makeRule("b*", "$0", ReplaceRule::Mode::Wildcard), "abc"), QStringLiteral("a$0"));
}

void TestReplaceRule::regexBadPattern()
{
    const ReplaceRule rule = makeRule("(", "x", ReplaceRule::Mode::Regex); // AC4.4
    const auto problem = rule.problem();
    QVERIFY(problem.has_value());
    QVERIFY(!problem->message.isEmpty());
    QVERIFY(problem->position >= 0);
    QVERIFY(!problem->unsupported);
    QCOMPARE(run(rule, "a(b"), QStringLiteral("a(b")); // no crash, no change

    // The position counts graphemes in the find text.
    const ReplaceRule emoji = makeRule("🙂🙂(", "x", ReplaceRule::Mode::Regex);
    QCOMPARE(emoji.problem()->position, 3);
}

void TestReplaceRule::literalAndWildcardHaveNoProblem()
{
    QVERIFY(!makeRule("(", "x").problem().has_value());
    QVERIFY(!makeRule("([*?", "x", ReplaceRule::Mode::Wildcard).problem().has_value());
    QVERIFY(!makeRule("(a)", "x", ReplaceRule::Mode::Regex).problem().has_value());
}

void TestReplaceRule::regexEmptyMatchNoLoop()
{
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(run(makeRule("x*", "-", ReplaceRule::Mode::Regex), "abc"), QStringLiteral("-a-b-c-"));
    QCOMPARE(run(makeRule("x*", "-", ReplaceRule::Mode::Regex), "axb"), QStringLiteral("-a--b-"));
    // An empty match never splits a grapheme cluster or a surrogate pair.
    QCOMPARE(run(makeRule("", "-", ReplaceRule::Mode::Regex), "🙂"), QStringLiteral("🙂"));
    QCOMPARE(run(makeRule("(?:)", "-", ReplaceRule::Mode::Regex), "🙂é"), QStringLiteral("-🙂-é-"));
    QVERIFY(timer.elapsed() < 1000);
}

void TestReplaceRule::regexOccurrenceFirstLast()
{
    ReplaceRule rule = makeRule("(\\d)", "<$1>", ReplaceRule::Mode::Regex);
    rule.occurrence = ReplaceRule::Occurrence::First;
    QCOMPARE(run(rule, "a1b2c3"), QStringLiteral("a<1>b2c3"));
    rule.occurrence = ReplaceRule::Occurrence::Last;
    QCOMPARE(run(rule, "a1b2c3"), QStringLiteral("a1b2c<3>"));
}

void TestReplaceRule::wildcard()
{
    const auto wild = ReplaceRule::Mode::Wildcard;
    QCOMPARE(run(makeRule("IMG_*", "P", wild), "IMG_0001"), QStringLiteral("P"));
    QCOMPARE(run(makeRule("?bc", "X", wild), "🙂bc"), QStringLiteral("X"));
    QCOMPARE(run(makeRule("?", "X", wild), "é"), QStringLiteral("X"));
    QCOMPARE(run(makeRule("a?c", "X", wild), "abc-ac"), QStringLiteral("X-ac"));

    ReplaceRule insensitive = makeRule("img_*", "P", wild);
    QCOMPARE(run(insensitive, "IMG_1"), QStringLiteral("IMG_1"));
    insensitive.caseSensitive = false;
    QCOMPARE(run(insensitive, "IMG_1"), QStringLiteral("P"));
}

void TestReplaceRule::wildcardEscapesRegex()
{
    const auto wild = ReplaceRule::Mode::Wildcard;
    QCOMPARE(run(makeRule("a.b", "X", wild), "axb"), QStringLiteral("axb"));
    QCOMPARE(run(makeRule("a.b", "X", wild), "a.b"), QStringLiteral("X"));
    QCOMPARE(run(makeRule("(*)", "X", wild), "a(b)c"), QStringLiteral("aXc"));
    QCOMPARE(run(makeRule("[?]", "X", wild), "[1]"), QStringLiteral("X"));
}

void TestReplaceRule::emojiAndCombining()
{
    QCOMPARE(run(makeRule("é", "e"), "café"), QStringLiteral("cafe"));
    QCOMPARE(run(makeRule("🙂", "x"), "a🙂b🙂"), QStringLiteral("axbx"));
    QCOMPARE(run(makeRule("́", ""), "café"), QStringLiteral("cafe"));
}

void TestReplaceRule::edgeInputs_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expected");
    const QString longName = QString(251, QLatin1Char('a')) + QStringLiteral(".txt");
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow(".bashrc") << ".bashrc" << ".bAshrc";
    QTest::newRow("noext") << "noext" << "noext";
    QTest::newRow("a.a") << "a.a" << "A.a";
    QTest::newRow("255 chars") << longName << QString(251, QLatin1Char('A')) + ".txt";
}

void TestReplaceRule::edgeInputs()
{
    QFETCH(QString, name);
    QFETCH(QString, expected);
    ReplaceRule rule; // target Stem (default)
    rule.find = QStringLiteral("a");
    rule.replace = QStringLiteral("A");
    QCOMPARE(run(rule, name), expected);
}

void TestReplaceRule::jsonRoundTrip()
{
    ReplaceRule rule;
    rule.find = QStringLiteral("x(\\d)");
    rule.replace = QStringLiteral("$1");
    rule.mode = ReplaceRule::Mode::Regex;
    rule.caseSensitive = false;
    rule.occurrence = ReplaceRule::Occurrence::Last;
    rule.wholeWord = true;

    const QJsonObject json = rule.optionsToJson();
    QCOMPARE(json.value("mode").toString(), QStringLiteral("regex"));
    QCOMPARE(json.value("occurrence").toString(), QStringLiteral("last"));
    QCOMPARE(QStringList(json.keys()).size(), rule.optionKeys().size());

    ReplaceRule back;
    QStringList warnings;
    back.optionsFromJson(json, &warnings);
    QVERIFY(warnings.isEmpty());
    QCOMPARE(back.find, rule.find);
    QCOMPARE(back.replace, rule.replace);
    QCOMPARE(back.mode, rule.mode);
    QCOMPARE(back.caseSensitive, rule.caseSensitive);
    QCOMPARE(back.occurrence, rule.occurrence);
    QCOMPARE(back.wholeWord, rule.wholeWord);

    for (auto [mode, text] : {std::pair{ReplaceRule::Mode::Literal, "literal"},
                              std::pair{ReplaceRule::Mode::Wildcard, "wildcard"}}) {
        ReplaceRule r;
        r.mode = mode;
        QCOMPARE(r.optionsToJson().value("mode").toString(), QString::fromLatin1(text));
    }
    ReplaceRule first;
    first.occurrence = ReplaceRule::Occurrence::First;
    QCOMPARE(first.optionsToJson().value("occurrence").toString(), QStringLiteral("first"));
    QCOMPARE(ReplaceRule().optionsToJson().value("occurrence").toString(), QStringLiteral("all"));

    // Missing keys give the defaults. Null warnings is allowed.
    back.optionsFromJson(QJsonObject{}, nullptr);
    QCOMPARE(back.find, QString());
    QCOMPARE(back.replace, QString());
    QCOMPARE(back.mode, ReplaceRule::Mode::Literal);
    QCOMPARE(back.caseSensitive, true);
    QCOMPARE(back.occurrence, ReplaceRule::Occurrence::All);
    QCOMPARE(back.wholeWord, false);
}

void TestReplaceRule::jsonBadValuesWarn()
{
    ReplaceRule rule;
    QStringList warnings;
    rule.optionsFromJson(QJsonObject{{"find", 5},
                                     {"mode", "fuzzy"},
                                     {"occurrence", "middle"},
                                     {"caseSensitive", "yes"}},
                         &warnings);
    QCOMPARE(warnings.size(), 4);
    QVERIFY(warnings.join('\n').contains("mode"));
    QCOMPARE(rule.find, QString());
    QCOMPARE(rule.mode, ReplaceRule::Mode::Literal);
    QCOMPARE(rule.occurrence, ReplaceRule::Occurrence::All);
    QCOMPARE(rule.caseSensitive, true);
    rule.optionsFromJson(QJsonObject{{"mode", "fuzzy"}}, nullptr); // no crash
}

void TestReplaceRule::cloneCopiesOptions()
{
    ReplaceRule rule = makeRule("a", "b", ReplaceRule::Mode::Wildcard);
    rule.enabled = false;
    rule.unknownKeys.insert("zzz", 1);
    const std::unique_ptr<Rule> copy = rule.clone();
    const auto *replace = dynamic_cast<const ReplaceRule *>(copy.get());
    QVERIFY(replace);
    QCOMPARE(replace->find, QStringLiteral("a"));
    QCOMPARE(replace->mode, ReplaceRule::Mode::Wildcard);
    QCOMPARE(replace->enabled, false);
    QCOMPARE(replace->target, Target::Name);
    QCOMPARE(replace->unknownKeys, rule.unknownKeys);
}

QTEST_GUILESS_MAIN(TestReplaceRule)
#include "tst_replacerule.moc"
