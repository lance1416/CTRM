// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/changecaserule.h"
#include "rules/numberingrule.h"
#include "rules/rulechain.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QtTest>

using namespace ctrm;

namespace {

QString run(const Rule &rule, const QString &name, const RuleContext &ctx = RuleContext{})
{
    RuleChain chain;
    chain.insert(0, rule.clone());
    return chain.apply(name, ctx);
}

ChangeCaseRule caseRule(ChangeCaseRule::Case mode, Target target = Target::Name)
{
    ChangeCaseRule rule;
    rule.caseMode = mode;
    rule.target = target;
    return rule;
}

RuleContext ctxAt(int n, int count = 1)
{
    RuleContext ctx;
    ctx.index = n;
    ctx.includedIndex = n;
    ctx.count = count;
    ctx.includedCount = count;
    return ctx;
}

} // namespace

class TestCaseNumbering : public QObject
{
    Q_OBJECT
private slots:
    void caseDefaults();
    void lowerTurkishLocale();
    void upperUnicode();
    void titleWithSeparators();
    void titleSmallWords();
    void sentence();
    void invert();
    void caseTargets();
    void edgeInputs_data();
    void edgeInputs();
    void caseJsonRoundTrip();
    void caseJsonBadValues();

    void numberingDefaults();
    void numberingAutoPadding();
    void numberingSuffixSeparator();
    void numberingPrefix();
    void numberingAtIndex();
    void numberingAtIndexFromEnd();
    void numberingScopeChecked();
    void numberingStartStep();
    void numberingPaddingNoTruncate();
    void numberingPaddingFixed();
    void numberingEmptyText();
    void numberingNoSeparator();
    void numberingLargeNumbers();
    void autoDigits();
    void jsonRoundTrip();
    void numberingJsonBadValues();
};

void TestCaseNumbering::caseDefaults()
{
    const ChangeCaseRule rule;
    QCOMPARE(rule.type(), QStringLiteral("changeCase"));
    QCOMPARE(rule.caseMode, ChangeCaseRule::Case::Lower);
    QCOMPARE(rule.separators, QStringLiteral(" _-.()[]{}"));
    QVERIFY(rule.smallWords.isEmpty());
    QCOMPARE(rule.builtInSmallWords, false);
    QCOMPARE(rule.target, Target::Stem);
    QCOMPARE(rule.optionKeys(),
             (QStringList{"case", "separators", "smallWords", "builtInSmallWords"}));
    QCOMPARE(ChangeCaseRule::englishSmallWords(),
             (QStringList{"a", "an", "and", "as", "at", "but", "by", "for", "in", "nor", "of", "on",
                          "or", "the", "to", "up", "via"}));
}

void TestCaseNumbering::lowerTurkishLocale()
{
    const QLocale saved;
    QLocale::setDefault(QLocale(QStringLiteral("tr_TR")));
    const QString lower = run(caseRule(ChangeCaseRule::Case::Lower), "I.JPG"); // AC4.7
    const QString upper = run(caseRule(ChangeCaseRule::Case::Upper), "i.jpg");
    QLocale::setDefault(saved);
    QCOMPARE(lower, QStringLiteral("i.jpg"));
    QCOMPARE(upper, QStringLiteral("I.JPG"));
}

void TestCaseNumbering::upperUnicode()
{
    QCOMPARE(run(caseRule(ChangeCaseRule::Case::Upper), "straße"), QStringLiteral("STRASSE"));
    QCOMPARE(run(caseRule(ChangeCaseRule::Case::Lower), "ÀÉÎ"), QStringLiteral("àéî"));
}

void TestCaseNumbering::titleWithSeparators()
{
    const ChangeCaseRule rule = caseRule(ChangeCaseRule::Case::Title);
    QCOMPARE(run(rule, "the_lord-of.rings"), QStringLiteral("The_Lord-Of.Rings"));
    QCOMPARE(run(rule, "hELLO (wORLD) [x]"), QStringLiteral("Hello (World) [X]"));
    QCOMPARE(run(rule, "1st place"), QStringLiteral("1st Place"));
    QCOMPARE(run(rule, "o'neil"), QStringLiteral("O'neil")); // ' is not a separator

    ChangeCaseRule custom = rule;
    custom.separators = QStringLiteral("+");
    QCOMPARE(run(custom, "a b+c"), QStringLiteral("A b+C"));
}

void TestCaseNumbering::titleSmallWords()
{
    ChangeCaseRule rule = caseRule(ChangeCaseRule::Case::Title);
    QCOMPARE(run(rule, "the lord of the rings"), QStringLiteral("The Lord Of The Rings"));
    rule.builtInSmallWords = true;
    QCOMPARE(run(rule, "the lord of the rings"), QStringLiteral("The Lord of the Rings"));
    QCOMPARE(run(rule, "THE LORD OF THE RINGS"), QStringLiteral("The Lord of the Rings"));
    QCOMPARE(run(rule, "_of mice"), QStringLiteral("_Of Mice")); // first word stays capital

    ChangeCaseRule user = caseRule(ChangeCaseRule::Case::Title);
    user.smallWords = QStringList{"VON", "der"};
    QCOMPARE(run(user, "ludwig von der tann"), QStringLiteral("Ludwig von der Tann"));
    QCOMPARE(run(user, "the von"), QStringLiteral("The von"));
}

void TestCaseNumbering::sentence()
{
    const ChangeCaseRule rule = caseRule(ChangeCaseRule::Case::Sentence);
    QCOMPARE(run(rule, "hELLO wORLD"), QStringLiteral("Hello world"));
    QCOMPARE(run(rule, "_hello World"), QStringLiteral("_Hello world"));
    QCOMPARE(run(rule, "01 hELLO"), QStringLiteral("01 Hello"));
    QCOMPARE(run(rule, "123"), QStringLiteral("123"));
}

void TestCaseNumbering::invert()
{
    const ChangeCaseRule rule = caseRule(ChangeCaseRule::Case::Invert);
    QCOMPARE(run(rule, "aBc"), QStringLiteral("AbC"));
    QCOMPARE(run(rule, "Ab1_é"), QStringLiteral("aB1_É"));
}

void TestCaseNumbering::caseTargets()
{
    QCOMPARE(run(caseRule(ChangeCaseRule::Case::Upper, Target::Stem), "photo.jpg"),
             QStringLiteral("PHOTO.jpg"));
    QCOMPARE(run(caseRule(ChangeCaseRule::Case::Lower, Target::Extension), "Photo.JPG"),
             QStringLiteral("Photo.jpg"));
}

void TestCaseNumbering::edgeInputs_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expected");
    using C = ChangeCaseRule::Case;
    const QString longLower = QString(255, QLatin1Char('a'));
    const QString longUpper = QString(255, QLatin1Char('A'));
    for (C c : {C::Lower, C::Upper, C::Title, C::Sentence, C::Invert}) {
        const auto tag = [c](const char *what) {
            return QByteArray(what) + "/" + QByteArray::number(int(c));
        };
        QTest::newRow(tag("empty")) << int(c) << QString() << QString();
        QTest::newRow(tag("emoji")) << int(c) << "🙂" << "🙂";
    }
    QTest::newRow("upper combining") << int(C::Upper) << "éx" << "ÉX";
    QTest::newRow("title combining") << int(C::Title) << "éx" << "Éx";
    QTest::newRow("sentence combining") << int(C::Sentence) << "ÉX" << "Éx";
    QTest::newRow("invert combining") << int(C::Invert) << "éX" << "Éx";
    QTest::newRow("title emoji word") << int(C::Title) << "🙂abc def" << "🙂abc Def";
    QTest::newRow("upper 255") << int(C::Upper) << longLower << longUpper;
    QTest::newRow("title 255") << int(C::Title) << longUpper
                               << QStringLiteral("A") + QString(254, QLatin1Char('a'));
}

void TestCaseNumbering::edgeInputs()
{
    QFETCH(int, mode);
    QFETCH(QString, name);
    QFETCH(QString, expected);
    QCOMPARE(run(caseRule(ChangeCaseRule::Case(mode)), name), expected);
}

void TestCaseNumbering::caseJsonRoundTrip()
{
    ChangeCaseRule rule;
    rule.caseMode = ChangeCaseRule::Case::Title;
    rule.separators = QStringLiteral(" +");
    rule.smallWords = QStringList{"x", "y"};
    rule.builtInSmallWords = true;
    const QJsonObject json = rule.optionsToJson();
    QCOMPARE(json.value("case").toString(), QStringLiteral("title"));
    QCOMPARE(json.value("smallWords").toArray(), (QJsonArray{"x", "y"}));
    QCOMPARE(json.size(), rule.optionKeys().size());

    ChangeCaseRule back;
    QStringList warnings;
    back.optionsFromJson(json, &warnings);
    QVERIFY(warnings.isEmpty());
    QCOMPARE(back.caseMode, rule.caseMode);
    QCOMPARE(back.separators, rule.separators);
    QCOMPARE(back.smallWords, rule.smallWords);
    QCOMPARE(back.builtInSmallWords, true);

    const QList<std::pair<ChangeCaseRule::Case, QString>> names{
            {ChangeCaseRule::Case::Lower, "lower"},
            {ChangeCaseRule::Case::Upper, "upper"},
            {ChangeCaseRule::Case::Title, "title"},
            {ChangeCaseRule::Case::Sentence, "sentence"},
            {ChangeCaseRule::Case::Invert, "invert"}};
    for (const auto &[mode, text] : names) {
        ChangeCaseRule r;
        r.caseMode = mode;
        QCOMPARE(r.optionsToJson().value("case").toString(), text);
    }

    back.optionsFromJson(QJsonObject{}, nullptr);
    QCOMPARE(back.caseMode, ChangeCaseRule::Case::Lower);
    QCOMPARE(back.separators, QStringLiteral(" _-.()[]{}"));
    QVERIFY(back.smallWords.isEmpty());
    QCOMPARE(back.builtInSmallWords, false);
}

void TestCaseNumbering::caseJsonBadValues()
{
    ChangeCaseRule rule;
    QStringList warnings;
    rule.optionsFromJson(QJsonObject{{"case", "camel"},
                                     {"separators", 1},
                                     {"smallWords", QJsonArray{"a", 2}},
                                     {"builtInSmallWords", "no"}},
                         &warnings);
    QCOMPARE(warnings.size(), 4);
    QVERIFY(warnings.contains("Bad value for 'case' in rule changeCase"));
    QCOMPARE(rule.caseMode, ChangeCaseRule::Case::Lower);
    QCOMPARE(rule.separators, QStringLiteral(" _-.()[]{}"));
    QVERIFY(rule.smallWords.isEmpty());
    QCOMPARE(rule.builtInSmallWords, false);
}

void TestCaseNumbering::numberingDefaults()
{
    const NumberingRule rule;
    QCOMPARE(rule.type(), QStringLiteral("numbering"));
    QCOMPARE(rule.start, 1);
    QCOMPARE(rule.step, 1);
    QCOMPARE(rule.padding, -1);
    QCOMPARE(rule.position, NumberingRule::Position::Suffix);
    QCOMPARE(rule.separator, QStringLiteral("_"));
    QCOMPARE(rule.index, 0);
    QCOMPARE(rule.fromEnd, false);
    QCOMPARE(rule.scope, NumberingRule::Scope::Checked);
    QCOMPARE(rule.target, Target::Stem);
    QCOMPARE(rule.optionKeys(), (QStringList{"start", "step", "padding", "position", "separator",
                                             "index", "fromEnd", "scope"}));
}

void TestCaseNumbering::numberingAutoPadding()
{
    NumberingRule rule; // AC4.5
    rule.position = NumberingRule::Position::Replace;
    QCOMPARE(run(rule, "", ctxAt(0, 120)), QStringLiteral("001"));
    QCOMPARE(run(rule, "", ctxAt(9, 120)), QStringLiteral("010"));
    QCOMPARE(run(rule, "", ctxAt(119, 120)), QStringLiteral("120"));
    QCOMPARE(run(rule, "x.jpg", ctxAt(4, 120)), QStringLiteral("005.jpg"));
    QCOMPARE(run(rule, "x", ctxAt(0, 9)), QStringLiteral("1"));
    QCOMPARE(run(rule, "x", ctxAt(0, 10)), QStringLiteral("01"));
}

void TestCaseNumbering::numberingSuffixSeparator()
{
    const NumberingRule rule;
    QCOMPARE(run(rule, "photo", ctxAt(0)), QStringLiteral("photo_1"));
    QCOMPARE(run(rule, "photo.jpg", ctxAt(0)), QStringLiteral("photo_1.jpg"));
}

void TestCaseNumbering::numberingPrefix()
{
    NumberingRule rule;
    rule.position = NumberingRule::Position::Prefix;
    QCOMPARE(run(rule, "photo", ctxAt(0)), QStringLiteral("1_photo"));
    rule.separator = QStringLiteral(" - ");
    QCOMPARE(run(rule, "photo", ctxAt(1, 2)), QStringLiteral("2 - photo"));
}

void TestCaseNumbering::numberingAtIndex()
{
    NumberingRule rule;
    rule.position = NumberingRule::Position::Index;
    rule.index = 2;
    QCOMPARE(run(rule, "abcd", ctxAt(0)), QStringLiteral("ab1_cd"));
    rule.index = 4;
    QCOMPARE(run(rule, "abcd", ctxAt(0)), QStringLiteral("abcd_1"));
    rule.index = 99; // clamps to the end
    QCOMPARE(run(rule, "abcd", ctxAt(0)), QStringLiteral("abcd_1"));
    rule.index = 0;
    QCOMPARE(run(rule, "abcd", ctxAt(0)), QStringLiteral("1_abcd"));
    rule.index = 1; // grapheme index
    QCOMPARE(run(rule, "🙂b", ctxAt(0)), QStringLiteral("🙂1_b"));
}

void TestCaseNumbering::numberingAtIndexFromEnd()
{
    NumberingRule rule;
    rule.position = NumberingRule::Position::Index;
    rule.fromEnd = true;
    rule.index = 1;
    QCOMPARE(run(rule, "abcd", ctxAt(0)), QStringLiteral("abc1_d"));
    rule.index = 0;
    QCOMPARE(run(rule, "abcd", ctxAt(0)), QStringLiteral("abcd_1"));
    rule.index = 99;
    QCOMPARE(run(rule, "abcd", ctxAt(0)), QStringLiteral("1_abcd"));
}

void TestCaseNumbering::numberingScopeChecked()
{
    NumberingRule rule;
    RuleContext excluded;
    excluded.index = 3;
    excluded.includedIndex = -1;
    excluded.count = 5;
    excluded.includedCount = 4;
    QCOMPARE(run(rule, "photo", excluded), QStringLiteral("photo"));

    RuleContext included = excluded;
    included.includedIndex = 2;
    QCOMPARE(run(rule, "photo", included), QStringLiteral("photo_3"));

    rule.scope = NumberingRule::Scope::All;
    QCOMPARE(run(rule, "photo", excluded), QStringLiteral("photo_4"));
    QCOMPARE(run(rule, "photo", included), QStringLiteral("photo_4"));

    // Auto padding counts the numbered items of the scope.
    rule.position = NumberingRule::Position::Replace;
    RuleContext many;
    many.index = 0;
    many.includedIndex = 0;
    many.count = 10;
    many.includedCount = 9;
    QCOMPARE(run(rule, "x", many), QStringLiteral("01"));
    rule.scope = NumberingRule::Scope::Checked;
    QCOMPARE(run(rule, "x", many), QStringLiteral("1"));
}

void TestCaseNumbering::numberingStartStep()
{
    NumberingRule rule;
    rule.position = NumberingRule::Position::Replace;
    rule.start = 0;
    rule.step = 5;
    rule.padding = 0;
    QCOMPARE(run(rule, "x", ctxAt(2, 3)), QStringLiteral("10"));
    QCOMPARE(run(rule, "x", ctxAt(0, 3)), QStringLiteral("0"));
}

void TestCaseNumbering::numberingPaddingNoTruncate()
{
    NumberingRule rule;
    rule.position = NumberingRule::Position::Replace;
    rule.padding = 2;
    QCOMPARE(run(rule, "x", ctxAt(119, 120)), QStringLiteral("120"));
}

void TestCaseNumbering::numberingPaddingFixed()
{
    NumberingRule rule;
    rule.position = NumberingRule::Position::Replace;
    rule.padding = 4;
    QCOMPARE(run(rule, "x", ctxAt(0, 120)), QStringLiteral("0001"));
    rule.padding = 10;
    QCOMPARE(run(rule, "x", ctxAt(0)), QStringLiteral("0000000001"));
}

void TestCaseNumbering::numberingEmptyText()
{
    NumberingRule rule;
    QCOMPARE(run(rule, "", ctxAt(0)), QStringLiteral("1"));
    rule.position = NumberingRule::Position::Prefix;
    QCOMPARE(run(rule, "", ctxAt(0)), QStringLiteral("1"));
    rule.position = NumberingRule::Position::Index;
    QCOMPARE(run(rule, "", ctxAt(0)), QStringLiteral("1"));
    rule.position = NumberingRule::Position::Suffix;
    rule.target = Target::Extension;
    QCOMPARE(run(rule, "a", ctxAt(0)), QStringLiteral("a.1"));
}

void TestCaseNumbering::numberingNoSeparator()
{
    NumberingRule rule;
    rule.separator = QString();
    QCOMPARE(run(rule, "photo", ctxAt(0)), QStringLiteral("photo1"));
}

void TestCaseNumbering::numberingLargeNumbers()
{
    NumberingRule rule;
    rule.position = NumberingRule::Position::Replace;
    rule.start = std::numeric_limits<int>::max();
    rule.step = std::numeric_limits<int>::max();
    // No overflow: the number is computed in 64 bits.
    QCOMPARE(run(rule, "x", ctxAt(2, 3)), QStringLiteral("6442450941"));
}

void TestCaseNumbering::autoDigits()
{
    QCOMPARE(NumberingRule::autoDigits(1, 1, 120), 3);
    QCOMPARE(NumberingRule::autoDigits(1, 1, 9), 1);
    QCOMPARE(NumberingRule::autoDigits(1, 1, 10), 2);
    QCOMPARE(NumberingRule::autoDigits(0, 5, 3), 2);
    QCOMPARE(NumberingRule::autoDigits(0, 1, 1), 1);
    QCOMPARE(NumberingRule::autoDigits(7, 1, 0), 1); // no numbered item
}

void TestCaseNumbering::jsonRoundTrip()
{
    NumberingRule rule;
    QCOMPARE(rule.optionsToJson().value("padding").toString(), QStringLiteral("auto"));
    QCOMPARE(rule.optionsToJson().value("position").toString(), QStringLiteral("suffix"));
    QCOMPARE(rule.optionsToJson().value("scope").toString(), QStringLiteral("checked"));

    rule.start = 7;
    rule.step = 3;
    rule.padding = 4;
    rule.position = NumberingRule::Position::Index;
    rule.separator = QStringLiteral("-");
    rule.index = 2;
    rule.fromEnd = true;
    rule.scope = NumberingRule::Scope::All;
    const QJsonObject json = rule.optionsToJson();
    QCOMPARE(json.value("padding").toInt(), 4);
    QCOMPARE(json.value("position").toString(), QStringLiteral("index"));
    QCOMPARE(json.value("scope").toString(), QStringLiteral("all"));
    QCOMPARE(json.size(), rule.optionKeys().size());

    NumberingRule back;
    QStringList warnings;
    back.optionsFromJson(json, &warnings);
    QVERIFY(warnings.isEmpty());
    QCOMPARE(back.start, 7);
    QCOMPARE(back.step, 3);
    QCOMPARE(back.padding, 4);
    QCOMPARE(back.position, NumberingRule::Position::Index);
    QCOMPARE(back.separator, QStringLiteral("-"));
    QCOMPARE(back.index, 2);
    QCOMPARE(back.fromEnd, true);
    QCOMPARE(back.scope, NumberingRule::Scope::All);

    back.optionsFromJson(QJsonObject{{"padding", "auto"}}, &warnings);
    QCOMPARE(back.padding, -1);
    QCOMPARE(back.start, 1); // missing keys give defaults
    QCOMPARE(back.position, NumberingRule::Position::Suffix);
    QVERIFY(warnings.isEmpty());

    NumberingRule prefix;
    prefix.position = NumberingRule::Position::Prefix;
    QCOMPARE(prefix.optionsToJson().value("position").toString(), QStringLiteral("prefix"));
    prefix.position = NumberingRule::Position::Replace;
    QCOMPARE(prefix.optionsToJson().value("position").toString(), QStringLiteral("replace"));
}

void TestCaseNumbering::numberingJsonBadValues()
{
    NumberingRule rule;
    QStringList warnings;
    rule.optionsFromJson(QJsonObject{{"start", -1},
                                     {"step", 0},
                                     {"padding", 11},
                                     {"position", "middle"},
                                     {"separator", 3},
                                     {"index", 1.5},
                                     {"fromEnd", 1},
                                     {"scope", "some"}},
                         &warnings);
    QCOMPARE(warnings.size(), 8);
    QVERIFY(warnings.contains("Bad value for 'padding' in rule numbering"));
    QCOMPARE(rule.start, 1);
    QCOMPARE(rule.step, 1);
    QCOMPARE(rule.padding, -1);
    QCOMPARE(rule.position, NumberingRule::Position::Suffix);
    QCOMPARE(rule.separator, QStringLiteral("_"));
    QCOMPARE(rule.index, 0);
    QCOMPARE(rule.fromEnd, false);
    QCOMPARE(rule.scope, NumberingRule::Scope::Checked);

    warnings.clear();
    rule.optionsFromJson(QJsonObject{{"padding", "big"}, {"start", 1e12}}, &warnings);
    QCOMPARE(warnings.size(), 2);
    rule.optionsFromJson(QJsonObject{{"padding", 0}, {"start", 0}}, nullptr);
    QCOMPARE(rule.padding, 0);
    QCOMPARE(rule.start, 0);
}

QTEST_GUILESS_MAIN(TestCaseNumbering)
#include "tst_casenumbering.moc"
