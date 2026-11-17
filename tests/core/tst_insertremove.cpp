// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/insertrule.h"
#include "rules/removerule.h"
#include "rules/rulechain.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

#include <functional>
#include <iterator>

using namespace ctrm;

namespace {

QString applyRule(const Rule &rule, const QString &name, bool isFolder = false)
{
    RuleChain chain;
    chain.insert(0, rule.clone());
    RuleContext ctx;
    ctx.isFolder = isFolder;
    return chain.apply(name, ctx);
}

InsertRule insertRule(const QString &text, InsertRule::Position position, int index = 0,
                      bool fromEnd = false)
{
    InsertRule r;
    r.text = text;
    r.position = position;
    r.index = index;
    r.fromEnd = fromEnd;
    return r;
}

RemoveRule rangeRule(int from, int count, bool fromEnd = false)
{
    RemoveRule r;
    r.mode = RemoveRule::Mode::Range;
    r.from = from;
    r.count = count;
    r.fromEnd = fromEnd;
    return r;
}

RemoveRule classRule(quint32 classes, const QString &custom = {})
{
    RemoveRule r;
    r.mode = RemoveRule::Mode::Characters;
    r.classes = classes;
    r.custom = custom;
    return r;
}

RemoveRule betweenRule(const QString &open, const QString &close, bool keep = false,
                       RemoveRule::Occurrence occurrence = RemoveRule::Occurrence::All)
{
    RemoveRule r;
    r.mode = RemoveRule::Mode::Between;
    r.open = open;
    r.close = close;
    r.keepDelimiters = keep;
    r.occurrence = occurrence;
    return r;
}

const QString kLongStem = QString(251, QLatin1Char('a'));
const QString kLongName = kLongStem + QStringLiteral(".txt"); // 255 characters

} // namespace

using RuleFactory = std::function<std::unique_ptr<Rule>()>;
Q_DECLARE_METATYPE(RuleFactory)

class TestInsertRemove : public QObject
{
    Q_OBJECT
private slots:
    void types();
    void insertPrefixSuffix();
    void insertAtIndexGrapheme();
    void insertIndexClamps();
    void insertFromEnd();
    void insertTargets();
    void removeRange();
    void removeRangeClamps();
    void removeRangeGrapheme();
    void removeClasses_data();
    void removeClasses();
    void removeBetween();
    void removeBetweenFirst();
    void removeBetweenNoClose();
    void removeBetweenGraphemeAligned();
    void edgeInputs_data();
    void edgeInputs();
    void jsonRoundTrip();
    void jsonMissingKeysUseDefaults();
    void jsonBadValuesWarn();
};

void TestInsertRemove::types()
{
    QCOMPARE(InsertRule().type(), QStringLiteral("insert"));
    QCOMPARE(RemoveRule().type(), QStringLiteral("remove"));
    QCOMPARE(InsertRule().optionKeys(), QStringList({"text", "position", "index", "fromEnd"}));
    QCOMPARE(RemoveRule().optionKeys(),
             QStringList({"mode", "from", "count", "fromEnd", "classes", "custom", "open", "close",
                          "keepDelimiters", "occurrence"}));
    QVERIFY(!InsertRule().problem());
    QVERIFY(!RemoveRule().problem());
    QVERIFY(!InsertRule().hasFixedTarget());
    QVERIFY(!RemoveRule().hasFixedTarget());

    // PRD 8.2 defaults.
    const InsertRule ins;
    QCOMPARE(ins.text, QString());
    QCOMPARE(ins.position, InsertRule::Position::Prefix);
    QCOMPARE(ins.index, 0);
    QCOMPARE(ins.fromEnd, false);
    QCOMPARE(ins.target, Target::Stem);
    const RemoveRule rem;
    QCOMPARE(rem.mode, RemoveRule::Mode::Range);
    QCOMPARE(rem.from, 0);
    QCOMPARE(rem.count, 1);
    QCOMPARE(rem.fromEnd, false);
    QCOMPARE(rem.classes, 0u);
    QCOMPARE(rem.custom, QString());
    QCOMPARE(rem.open, QStringLiteral("("));
    QCOMPARE(rem.close, QStringLiteral(")"));
    QCOMPARE(rem.keepDelimiters, false);
    QCOMPARE(rem.occurrence, RemoveRule::Occurrence::All);
}

void TestInsertRemove::insertPrefixSuffix()
{
    QCOMPARE(applyRule(insertRule("Trip_", InsertRule::Position::Prefix), "photo"),
             QStringLiteral("Trip_photo"));
    QCOMPARE(applyRule(insertRule("_edit", InsertRule::Position::Suffix), "photo.jpg"),
             QStringLiteral("photo_edit.jpg"));
    // Empty text: no effect.
    QCOMPARE(applyRule(insertRule("", InsertRule::Position::Suffix), "photo.jpg"),
             QStringLiteral("photo.jpg"));
}

void TestInsertRemove::insertAtIndexGrapheme()
{
    QCOMPARE(applyRule(insertRule("X", InsertRule::Position::Index, 1), "🙂abc"),
             QStringLiteral("🙂Xabc"));
    QCOMPARE(applyRule(insertRule("X", InsertRule::Position::Index, 1), "e\u0301b"),
             QStringLiteral("e\u0301Xb"));
    QCOMPARE(applyRule(insertRule("X", InsertRule::Position::Index, 0), "abc"),
             QStringLiteral("Xabc"));
}

void TestInsertRemove::insertIndexClamps()
{
    QCOMPARE(applyRule(insertRule("X", InsertRule::Position::Index, 99), "abc"),
             QStringLiteral("abcX"));
    QCOMPARE(applyRule(insertRule("X", InsertRule::Position::Index, -5), "abc"),
             QStringLiteral("Xabc"));
    QCOMPARE(applyRule(insertRule("X", InsertRule::Position::Index, 99, true), "abc"),
             QStringLiteral("Xabc"));
}

void TestInsertRemove::insertFromEnd()
{
    QCOMPARE(applyRule(insertRule("X", InsertRule::Position::Index, 1, true), "abc"),
             QStringLiteral("abXc"));
    QCOMPARE(applyRule(insertRule("X", InsertRule::Position::Index, 0, true), "abc"),
             QStringLiteral("abcX"));
    QCOMPARE(applyRule(insertRule("X", InsertRule::Position::Index, 1, true), "ab🙂"),
             QStringLiteral("abX🙂"));
}

void TestInsertRemove::insertTargets()
{
    InsertRule ext = insertRule("x", InsertRule::Position::Suffix);
    ext.target = Target::Extension;
    QCOMPARE(applyRule(ext, "a.jp"), QStringLiteral("a.jpx"));

    InsertRule name = insertRule("_v2", InsertRule::Position::Suffix);
    name.target = Target::Name;
    QCOMPARE(applyRule(name, "a.jpg"), QStringLiteral("a.jpg_v2"));
}

void TestInsertRemove::removeRange()
{
    QCOMPARE(applyRule(rangeRule(0, 4), "IMG_0001"), QStringLiteral("0001"));
    QCOMPARE(applyRule(rangeRule(0, 2, true), "IMG_0001"), QStringLiteral("IMG_00"));
    QCOMPARE(applyRule(rangeRule(2, 2, true), "IMG_0001"), QStringLiteral("IMG_01"));
    QCOMPARE(applyRule(rangeRule(1, 2), "abcd.txt"), QStringLiteral("ad.txt"));
}

void TestInsertRemove::removeRangeClamps()
{
    QCOMPARE(applyRule(rangeRule(10, 5), "abc"), QStringLiteral("abc"));
    QCOMPARE(applyRule(rangeRule(1, 99), "abc"), QStringLiteral("a"));
    QCOMPARE(applyRule(rangeRule(10, 5, true), "abc"), QStringLiteral("abc"));
    QCOMPARE(applyRule(rangeRule(1, 99, true), "abc"), QStringLiteral("c"));
    QCOMPARE(applyRule(rangeRule(0, 0), "abc"), QStringLiteral("abc"));
    QCOMPARE(applyRule(rangeRule(-3, 1), "abc"), QStringLiteral("bc"));
    QCOMPARE(applyRule(rangeRule(0, -1), "abc"), QStringLiteral("abc"));
}

void TestInsertRemove::removeRangeGrapheme()
{
    QCOMPARE(applyRule(rangeRule(0, 1), "e\u0301a"), QStringLiteral("a"));
    QCOMPARE(applyRule(rangeRule(0, 1), "éa"), QStringLiteral("a"));
    QCOMPARE(applyRule(rangeRule(0, 1, true), "a🙂"), QStringLiteral("a"));
    QCOMPARE(applyRule(rangeRule(1, 1), "👨‍👩‍👧x"),
             QStringLiteral("👨‍👩‍👧"));
}

void TestInsertRemove::removeClasses_data()
{
    QTest::addColumn<quint32>("classes");
    QTest::addColumn<QString>("custom");
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("digits") << quint32(RemoveRule::Digits) << "" << "a1b2" << "ab";
    QTest::newRow("digits arabic-indic") << quint32(RemoveRule::Digits) << "" << "a\u0661b" << "ab";
    QTest::newRow("digits not superscript")
            << quint32(RemoveRule::Digits) << "" << "a\u00B2" << "a\u00B2";
    QTest::newRow("letters") << quint32(RemoveRule::Letters) << "" << "a1Éb_ж" << "1_";
    QTest::newRow("letters with mark") << quint32(RemoveRule::Letters) << "" << "e\u0301-1" << "-1";
    QTest::newRow("spaces") << quint32(RemoveRule::Spaces) << "" << "a b" << "ab";
    QTest::newRow("spaces nbsp tab ideographic")
            << quint32(RemoveRule::Spaces) << "" << "a\u00A0b\tc\u3000d" << "abcd";
    QTest::newRow("punctuation") << quint32(RemoveRule::Punctuation) << "" << "a-b!" << "ab";
    QTest::newRow("punctuation brackets")
            << quint32(RemoveRule::Punctuation) << "" << "(a)_[b]" << "ab";
    QTest::newRow("symbols") << quint32(RemoveRule::Symbols) << "" << "a+b©" << "ab";
    QTest::newRow("symbols currency") << quint32(RemoveRule::Symbols) << "" << "5€$" << "5";
    QTest::newRow("nonAscii") << quint32(RemoveRule::NonAscii) << "" << "café🙂" << "caf";
    QTest::newRow("nonAscii combining")
            << quint32(RemoveRule::NonAscii) << "" << "e\u0301x" << "ex";
    QTest::newRow("nonAscii zwj emoji")
            << quint32(RemoveRule::NonAscii) << "" << "a👨‍👩‍👧b" << "ab";
    QTest::newRow("custom") << quint32(RemoveRule::Custom) << "xy" << "axbyc" << "abc";
    QTest::newRow("custom cluster exact")
            << quint32(RemoveRule::Custom) << "e\u0301" << "e\u0301éex" << "éex";
    QTest::newRow("custom emoji") << quint32(RemoveRule::Custom) << "🙂" << "a🙂b" << "ab";
    QTest::newRow("custom without class bit") << quint32(0) << "xy" << "axbyc" << "axbyc";
    QTest::newRow("no classes") << quint32(0) << "" << "a1 b" << "a1 b";
    QTest::newRow("digits and spaces")
            << quint32(RemoveRule::Digits | RemoveRule::Spaces) << "" << "a 1 b 2" << "ab";
    QTest::newRow("letters and nonAscii")
            << quint32(RemoveRule::Letters | RemoveRule::NonAscii) << "" << "a1é2🙂" << "12";
}

void TestInsertRemove::removeClasses()
{
    QFETCH(quint32, classes);
    QFETCH(QString, custom);
    QFETCH(QString, input);
    QFETCH(QString, expected);

    RemoveRule rule = classRule(classes, custom);
    rule.target = Target::Name;
    QCOMPARE(applyRule(rule, input), expected);
}

void TestInsertRemove::removeBetween()
{
    QCOMPARE(applyRule(betweenRule("[", "]"), "Movie [1080p] x"), QStringLiteral("Movie  x"));
    QCOMPARE(applyRule(betweenRule("[", "]", true), "Movie [1080p] x"),
             QStringLiteral("Movie [] x"));
    QCOMPARE(applyRule(betweenRule("(", ")"), "(a)b(c)"), QStringLiteral("b"));
    QCOMPARE(applyRule(betweenRule("<<", ">>"), "a<<b>>c<<d>>"), QStringLiteral("ac"));
    // No nesting: the first close after the open ends the range.
    QCOMPARE(applyRule(betweenRule("(", ")"), "a(b(c)d)e"), QStringLiteral("ad)e"));
    // Same open and close.
    QCOMPARE(applyRule(betweenRule("_", "_"), "a_b_c_d_e"), QStringLiteral("ace"));
    // Empty delimiter: no effect.
    QCOMPARE(applyRule(betweenRule("", ")"), "a(b)"), QStringLiteral("a(b)"));
    QCOMPARE(applyRule(betweenRule("(", ""), "a(b)"), QStringLiteral("a(b)"));
}

void TestInsertRemove::removeBetweenFirst()
{
    QCOMPARE(applyRule(betweenRule("(", ")", false, RemoveRule::Occurrence::First), "(a)(b)"),
             QStringLiteral("(b)"));
    QCOMPARE(applyRule(betweenRule("(", ")", true, RemoveRule::Occurrence::First), "(a)(b)"),
             QStringLiteral("()(b)"));
}

void TestInsertRemove::removeBetweenNoClose()
{
    QCOMPARE(applyRule(betweenRule("(", ")"), "a(b"), QStringLiteral("a(b"));
    QCOMPARE(applyRule(betweenRule("(", ")"), "(a)b(c"), QStringLiteral("b(c"));
    QCOMPARE(applyRule(betweenRule("(", ")"), "a)b(c"), QStringLiteral("a)b(c"));
}

void TestInsertRemove::removeBetweenGraphemeAligned()
{
    // "e" is not a full cluster in "e\u0301", so it is not a delimiter there.
    QCOMPARE(applyRule(betweenRule("e", "x"), "e\u0301ax"), QStringLiteral("e\u0301ax"));
    QCOMPARE(applyRule(betweenRule("e", "x"), "e\u0301aeyx"), QStringLiteral("e\u0301a"));
}

void TestInsertRemove::edgeInputs_data()
{
    QTest::addColumn<RuleFactory>("factory");
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("isFolder");
    QTest::addColumn<QString>("expected");

    const RuleFactory prefix = [] {
        return insertRule("X_", InsertRule::Position::Prefix).clone();
    };
    const RuleFactory suffix = [] {
        return insertRule("_Y", InsertRule::Position::Suffix).clone();
    };
    const RuleFactory index1 = [] {
        return insertRule("X", InsertRule::Position::Index, 1).clone();
    };
    const RuleFactory range = [] { return rangeRule(0, 1).clone(); };
    const RuleFactory chars = [] {
        return classRule(RemoveRule::Digits | RemoveRule::NonAscii).clone();
    };
    const RuleFactory between = [] { return betweenRule("(", ")").clone(); };

    struct Edge
    {
        const char *tag;
        QString name;
        bool isFolder;
    };
    const Edge edges[] = {
            {"empty", QString(), false},
            {"dotfile", QStringLiteral(".bashrc"), false},
            {"noext", QStringLiteral("noext"), false},
            {"folder", QStringLiteral("Pro.v2"), true},
            {"emoji", QStringLiteral("🙂1.png"), false},
            {"combining", QStringLiteral("e\u0301(1).txt"), false},
            {"255", kLongName, false},
    };
    const QStringList prefixOut = {"X_",        "X_.bashrc",        "X_noext",       "X_Pro.v2",
                                   "X_🙂1.png", "X_e\u0301(1).txt", "X_" + kLongName};
    const QStringList suffixOut = {"_Y",        ".bashrc_Y",        "noext_Y",           "Pro.v2_Y",
                                   "🙂1_Y.png", "e\u0301(1)_Y.txt", kLongStem + "_Y.txt"};
    const QStringList index1Out = {"X",
                                   ".Xbashrc",
                                   "nXoext",
                                   "PXro.v2",
                                   "🙂X1.png",
                                   "e\u0301X(1).txt",
                                   "aX" + kLongName.mid(1)};
    const QStringList rangeOut = {"",      "bashrc",  "oext",          "ro.v2",
                                  "1.png", "(1).txt", kLongName.mid(1)};
    const QStringList charsOut = {"", ".bashrc", "noext", "Pro.v", ".png", "e().txt", kLongName};
    const QStringList betweenOut = {"",        ".bashrc",     "noext",  "Pro.v2",
                                    "🙂1.png", "e\u0301.txt", kLongName};

    const auto addRows = [&](const char *rule, const RuleFactory &f, const QStringList &out) {
        for (qsizetype i = 0; i < std::ssize(edges); ++i) {
            QTest::addRow("%s %s", rule, edges[i].tag)
                    << f << edges[i].name << edges[i].isFolder << out.at(i);
        }
    };
    addRows("prefix", prefix, prefixOut);
    addRows("suffix", suffix, suffixOut);
    addRows("index1", index1, index1Out);
    addRows("range", range, rangeOut);
    addRows("chars", chars, charsOut);
    addRows("between", between, betweenOut);
}

void TestInsertRemove::edgeInputs()
{
    QFETCH(RuleFactory, factory);
    QFETCH(QString, input);
    QFETCH(bool, isFolder);
    QFETCH(QString, expected);

    QCOMPARE(applyRule(*factory(), input, isFolder), expected);
}

void TestInsertRemove::jsonRoundTrip()
{
    InsertRule ins = insertRule("Hi 🙂", InsertRule::Position::Index, 3, true);
    const QJsonObject insJson = ins.optionsToJson();
    QCOMPARE(insJson,
             QJsonObject(
                     {{"text", "Hi 🙂"}, {"position", "index"}, {"index", 3}, {"fromEnd", true}}));
    InsertRule insBack;
    QStringList warnings;
    insBack.optionsFromJson(insJson, &warnings);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    QCOMPARE(insBack.text, ins.text);
    QCOMPARE(insBack.position, ins.position);
    QCOMPARE(insBack.index, ins.index);
    QCOMPARE(insBack.fromEnd, ins.fromEnd);
    QCOMPARE(insBack.optionsToJson(), insJson);
    QCOMPARE(insertRule("", InsertRule::Position::Suffix).optionsToJson().value("position"),
             QJsonValue("suffix"));

    RemoveRule rem;
    rem.mode = RemoveRule::Mode::Between;
    rem.from = 2;
    rem.count = 5;
    rem.fromEnd = true;
    rem.classes = RemoveRule::Custom | RemoveRule::Digits | RemoveRule::NonAscii;
    rem.custom = QStringLiteral("_-");
    rem.open = QStringLiteral("[");
    rem.close = QStringLiteral("]");
    rem.keepDelimiters = true;
    rem.occurrence = RemoveRule::Occurrence::First;
    const QJsonObject remJson = rem.optionsToJson();
    QCOMPARE(remJson, QJsonObject({{"mode", "between"},
                                   {"from", 2},
                                   {"count", 5},
                                   {"fromEnd", true},
                                   {"classes", QJsonArray({"digits", "nonAscii", "custom"})},
                                   {"custom", "_-"},
                                   {"open", "["},
                                   {"close", "]"},
                                   {"keepDelimiters", true},
                                   {"occurrence", "first"}}));
    RemoveRule remBack;
    remBack.optionsFromJson(remJson, &warnings);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    QCOMPARE(remBack.mode, rem.mode);
    QCOMPARE(remBack.from, rem.from);
    QCOMPARE(remBack.count, rem.count);
    QCOMPARE(remBack.fromEnd, rem.fromEnd);
    QCOMPARE(remBack.classes, rem.classes);
    QCOMPARE(remBack.custom, rem.custom);
    QCOMPARE(remBack.open, rem.open);
    QCOMPARE(remBack.close, rem.close);
    QCOMPARE(remBack.keepDelimiters, rem.keepDelimiters);
    QCOMPARE(remBack.occurrence, rem.occurrence);
    QCOMPARE(remBack.optionsToJson(), remJson);

    // All classes, and the keys of optionsToJson match optionKeys.
    RemoveRule all;
    all.classes = RemoveRule::Digits | RemoveRule::Letters | RemoveRule::Spaces |
                  RemoveRule::Punctuation | RemoveRule::Symbols | RemoveRule::NonAscii |
                  RemoveRule::Custom;
    QCOMPARE(all.optionsToJson().value("classes"),
             QJsonValue(QJsonArray({"digits", "letters", "spaces", "punctuation", "symbols",
                                    "nonAscii", "custom"})));
    QCOMPARE(QStringList(all.optionsToJson().keys()).size(), all.optionKeys().size());
    for (const QString &key : all.optionKeys())
        QVERIFY2(all.optionsToJson().contains(key), qPrintable(key));
    for (const QString &key : ins.optionKeys())
        QVERIFY2(insJson.contains(key), qPrintable(key));
}

void TestInsertRemove::jsonMissingKeysUseDefaults()
{
    InsertRule ins = insertRule("x", InsertRule::Position::Index, 4, true);
    QStringList warnings;
    ins.optionsFromJson(QJsonObject{}, &warnings);
    QVERIFY(warnings.isEmpty());
    QCOMPARE(ins.optionsToJson(), InsertRule().optionsToJson());

    RemoveRule rem = classRule(RemoveRule::Digits, "q");
    rem.optionsFromJson(QJsonObject{{"mode", "characters"}}, &warnings);
    QVERIFY(warnings.isEmpty());
    RemoveRule expected;
    expected.mode = RemoveRule::Mode::Characters;
    QCOMPARE(rem.optionsToJson(), expected.optionsToJson());

    // A null warnings pointer is permitted.
    rem.optionsFromJson(QJsonObject{{"mode", 5}}, nullptr);
    QCOMPARE(rem.mode, RemoveRule::Mode::Range);
}

void TestInsertRemove::jsonBadValuesWarn()
{
    QStringList warnings;
    InsertRule ins;
    ins.optionsFromJson(
            QJsonObject{{"text", 5}, {"position", "middle"}, {"index", -1}, {"fromEnd", "yes"}},
            &warnings);
    QCOMPARE(warnings.size(), 4);
    QCOMPARE(ins.optionsToJson(), InsertRule().optionsToJson());
    QCOMPARE(warnings.at(1), QStringLiteral("Bad value for 'position' in rule insert"));

    warnings.clear();
    RemoveRule rem;
    rem.optionsFromJson(QJsonObject{{"mode", "all"},
                                    {"count", 1.5},
                                    {"classes", QJsonArray({"digits", "emoji", 3})},
                                    {"occurrence", "last"}},
                        &warnings);
    QCOMPARE(warnings, QStringList({"Bad value for 'mode' in rule remove",
                                    "Bad value for 'count' in rule remove",
                                    "Bad value for 'classes' in rule remove",
                                    "Bad value for 'classes' in rule remove",
                                    "Bad value for 'occurrence' in rule remove"}));
    QCOMPARE(rem.mode, RemoveRule::Mode::Range);
    QCOMPARE(rem.count, 1);
    QCOMPARE(rem.classes, quint32(RemoveRule::Digits)); // the valid entries stay
    QCOMPARE(rem.occurrence, RemoveRule::Occurrence::All);

    warnings.clear();
    rem.optionsFromJson(QJsonObject{{"classes", "digits"}}, &warnings);
    QCOMPARE(warnings.size(), 1);
    QCOMPARE(rem.classes, 0u);
}

QTEST_GUILESS_MAIN(TestInsertRemove)
#include "tst_insertremove.moc"
