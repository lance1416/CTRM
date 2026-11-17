// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/extensionrule.h"
#include "rules/numberingrule.h"
#include "rules/rulechain.h"
#include "rules/ruleregistry.h"
#include "rules/unsupportedrule.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using namespace ctrm;

class TestRuleRegistry : public QObject
{
    Q_OBJECT
private slots:
    void ruleTypeList();
    void createAllTypes();
    void roundTripEachType_data();
    void roundTripEachType();
    void defaultJsonHasAllKeys();
    void missingOptionsUseDefaults();
    void unknownKeyWarnsAndKeeps();
    void unknownKeyDoesNotOverrideOption();
    void badCommonKeysWarn();
    void unknownTypeUnsupported();
    void unsupportedDisabledNoError();
    void unsupportedKeepsName();
    void missingTypeUnsupported();
    void extensionHasNoTarget();
    void extensionTargetIgnored();
    void chainRules();
    void chainOptionsJson();
};

void TestRuleRegistry::ruleTypeList()
{
    QCOMPARE(ruleTypes(), (QStringList{"replace", "insert", "remove", "cleanup", "changeCase",
                                       "extension", "numbering"}));
}

void TestRuleRegistry::createAllTypes()
{
    for (const QString &t : ruleTypes()) {
        const std::unique_ptr<Rule> rule = createRule(t);
        QVERIFY2(rule, qPrintable(t));
        QCOMPARE(rule->type(), t);
        QCOMPARE(rule->enabled, true);
        QVERIFY(!rule->problem().has_value());
    }
    QCOMPARE(createRule("x"), nullptr);
    QCOMPARE(createRule(QString()), nullptr);
    QCOMPARE(createRule("Replace"), nullptr); // exact match only
}

void TestRuleRegistry::roundTripEachType_data()
{
    QTest::addColumn<QJsonObject>("changes");
    const auto row = [](const char *name, QJsonObject changes) { QTest::newRow(name) << changes; };
    row("replace", {{"type", "replace"},
                    {"enabled", false},
                    {"target", "name"},
                    {"find", "a(b)"},
                    {"replace", "$1"},
                    {"mode", "regex"},
                    {"caseSensitive", false},
                    {"occurrence", "last"},
                    {"wholeWord", true}});
    row("insert", {{"type", "insert"},
                   {"target", "extension"},
                   {"text", "X_"},
                   {"position", "index"},
                   {"index", 3},
                   {"fromEnd", true}});
    row("remove", {{"type", "remove"},
                   {"mode", "characters"},
                   {"from", 2},
                   {"count", 4},
                   {"classes", QJsonArray{"digits", "nonAscii"}},
                   {"custom", "#"},
                   {"open", "["},
                   {"close", "]"},
                   {"keepDelimiters", true},
                   {"occurrence", "first"}});
    row("cleanup", {{"type", "cleanup"},
                    {"trim", false},
                    {"collapseSpaces", false},
                    {"spacesTo", "-"},
                    {"replaceInvalid", true},
                    {"replacement", "+"}});
    row("changeCase", {{"type", "changeCase"},
                       {"case", "title"},
                       {"separators", " "},
                       {"smallWords", QJsonArray{"of"}},
                       {"builtInSmallWords", true}});
    row("extension", {{"type", "extension"},
                      {"enabled", false},
                      {"action", "set"},
                      {"value", "jpg"},
                      {"onlyIfMatches", "jpeg;jpe"}});
    row("numbering", {{"type", "numbering"},
                      {"start", 0},
                      {"step", 5},
                      {"padding", 3},
                      {"position", "replace"},
                      {"separator", "-"},
                      {"index", 2},
                      {"fromEnd", true},
                      {"scope", "all"}});
}

void TestRuleRegistry::roundTripEachType()
{
    QFETCH(QJsonObject, changes);
    const QString type = changes.value("type").toString();

    // Full JSON with non-default values: the defaults, then the changes.
    QJsonObject input = ruleToJson(*createRule(type));
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it)
        input.insert(it.key(), it.value());

    QStringList warnings;
    const std::unique_ptr<Rule> rule = ruleFromJson(input, &warnings);
    QVERIFY(rule);
    QCOMPARE(warnings, QStringList());
    QCOMPARE(rule->type(), type);
    QCOMPARE(rule->enabled, input.value("enabled").toBool());
    const QJsonObject output = ruleToJson(*rule);
    QCOMPARE(output, input);
    QVERIFY(output != ruleToJson(*createRule(type))); // the changes took effect
}

void TestRuleRegistry::defaultJsonHasAllKeys()
{
    for (const QString &t : ruleTypes()) {
        const std::unique_ptr<Rule> rule = createRule(t);
        const QJsonObject json = ruleToJson(*rule);
        QCOMPARE(json.value("type").toString(), t);
        QCOMPARE(json.value("enabled").toBool(), true);
        QCOMPARE(json.contains("target"), t != QLatin1String("extension"));
        if (t != QLatin1String("extension"))
            QCOMPARE(json.value("target").toString(), QStringLiteral("stem"));
        for (const QString &key : rule->optionKeys())
            QVERIFY2(json.contains(key), qPrintable(t + ": " + key));
        const int common = t == QLatin1String("extension") ? 2 : 3;
        QCOMPARE(json.size(), common + rule->optionKeys().size());
    }
}

void TestRuleRegistry::missingOptionsUseDefaults()
{
    QStringList warnings;
    const std::unique_ptr<Rule> rule =
            ruleFromJson(QJsonObject{{"type", "numbering"}, {"enabled", true}}, &warnings);
    QVERIFY(warnings.isEmpty());
    const auto *numbering = dynamic_cast<const NumberingRule *>(rule.get());
    QVERIFY(numbering);
    QCOMPARE(numbering->start, 1);
    QCOMPARE(numbering->padding, -1);
    QCOMPARE(numbering->target, Target::Stem);
    QCOMPARE(numbering->enabled, true);

    // "enabled" is missing too: default true.
    const std::unique_ptr<Rule> replace = ruleFromJson(QJsonObject{{"type", "replace"}}, nullptr);
    QVERIFY(replace);
    QCOMPARE(replace->enabled, true);
    QCOMPARE(ruleToJson(*replace), ruleToJson(*createRule("replace")));
}

void TestRuleRegistry::unknownKeyWarnsAndKeeps()
{
    QStringList warnings;
    const QJsonObject input{{"type", "insert"}, {"enabled", true}, {"zzz", 1}};
    const std::unique_ptr<Rule> rule = ruleFromJson(input, &warnings);
    QVERIFY(rule);
    QCOMPARE(warnings, QStringList{"Unknown key 'zzz' in rule insert"});
    QCOMPARE(rule->unknownKeys, (QJsonObject{{"zzz", 1}}));
    const QJsonObject output = ruleToJson(*rule);
    QCOMPARE(output.value("zzz"), QJsonValue(1));
    QCOMPARE(output.value("type").toString(), QStringLiteral("insert"));
}

void TestRuleRegistry::unknownKeyDoesNotOverrideOption()
{
    const std::unique_ptr<Rule> rule = createRule("insert");
    rule->unknownKeys.insert("text", "bad");
    rule->unknownKeys.insert("type", "bad");
    const QJsonObject output = ruleToJson(*rule);
    QCOMPARE(output.value("type").toString(), QStringLiteral("insert"));
    QCOMPARE(output.value("text").toString(), QString());
}

void TestRuleRegistry::badCommonKeysWarn()
{
    QStringList warnings;
    const std::unique_ptr<Rule> rule = ruleFromJson(
            QJsonObject{{"type", "replace"}, {"enabled", "yes"}, {"target", "all"}}, &warnings);
    QVERIFY(rule);
    QCOMPARE(warnings.size(), 2);
    QVERIFY(warnings.contains("Bad value for 'enabled' in rule replace"));
    QVERIFY(warnings.contains("Bad value for 'target' in rule replace"));
    QCOMPARE(rule->enabled, true);
    QCOMPARE(rule->target, Target::Stem);
    QVERIFY(rule->unknownKeys.isEmpty());
}

void TestRuleRegistry::unknownTypeUnsupported()
{
    const QJsonObject input{{"type", "exifDate"}, {"enabled", true}, {"format", "x"}};
    QStringList warnings;
    const std::unique_ptr<Rule> rule = ruleFromJson(input, &warnings); // AC14.3 part
    QVERIFY(rule);
    QVERIFY(dynamic_cast<const UnsupportedRule *>(rule.get()));
    QCOMPARE(rule->type(), QStringLiteral("exifDate"));
    QCOMPARE(rule->hasFixedTarget(), true);
    const auto problem = rule->problem();
    QVERIFY(problem.has_value());
    QVERIFY(problem->unsupported);
    QCOMPARE(problem->message, QStringLiteral("Unsupported rule (exifDate)"));
    QCOMPARE(problem->position, -1);
    QCOMPARE(ruleToJson(*rule), input);

    // With a target key, the output still equals the input.
    const QJsonObject withTarget{{"type", "exifDate"}, {"enabled", true}, {"target", "name"}};
    QCOMPARE(ruleToJson(*ruleFromJson(withTarget, nullptr)), withTarget);

    // An enabled unsupported rule blocks the chain. It does not change the name.
    RuleChain chain;
    chain.insert(0, rule->clone());
    QCOMPARE(chain.firstError()->ruleIndex, 0);
    QVERIFY(chain.firstError()->problem.unsupported);
    QCOMPARE(chain.apply("a.txt", RuleContext{}), QStringLiteral("a.txt"));
}

void TestRuleRegistry::unsupportedDisabledNoError()
{
    const QJsonObject input{{"type", "exifDate"}, {"enabled", false}, {"format", "x"}};
    const std::unique_ptr<Rule> rule = ruleFromJson(input, nullptr);
    QCOMPARE(rule->enabled, false);
    RuleChain chain;
    chain.insert(0, rule->clone());
    QVERIFY(!chain.firstError().has_value());
    QCOMPARE(ruleToJson(*chain.at(0)), input);

    chain.at(0)->enabled = true; // the user enables it again
    QVERIFY(chain.firstError().has_value());
    QCOMPARE(ruleToJson(*chain.at(0)).value("enabled").toBool(), true);
}

void TestRuleRegistry::unsupportedKeepsName()
{
    UnsupportedRule rule(QJsonObject{{"type", "future"}, {"enabled", true}, {"a", 1}});
    QCOMPARE(rule.optionsToJson(), (QJsonObject{{"a", 1}}));
    QCOMPARE(rule.optionKeys(), QStringList{"a"});
    const std::unique_ptr<Rule> copy = rule.clone();
    QCOMPARE(copy->type(), QStringLiteral("future"));
    QCOMPARE(copy->optionsToJson(), rule.optionsToJson());
}

void TestRuleRegistry::missingTypeUnsupported()
{
    const QJsonObject input{{"enabled", true}, {"text", "x"}};
    const std::unique_ptr<Rule> rule = ruleFromJson(input, nullptr);
    QVERIFY(rule);
    QVERIFY(rule->problem().has_value());
    QVERIFY(rule->problem()->unsupported);

    const QJsonObject numberType{{"type", 5}, {"enabled", true}};
    const std::unique_ptr<Rule> other = ruleFromJson(numberType, nullptr);
    QVERIFY(other->problem().has_value());
    QVERIFY(other->problem()->unsupported);
}

void TestRuleRegistry::extensionHasNoTarget()
{
    const ExtensionRule rule;
    QVERIFY(!ruleToJson(rule).contains("target"));
    QCOMPARE(ruleToJson(rule).value("type").toString(), QStringLiteral("extension"));
}

void TestRuleRegistry::extensionTargetIgnored()
{
    QStringList warnings;
    const std::unique_ptr<Rule> rule = ruleFromJson(
            QJsonObject{{"type", "extension"}, {"enabled", true}, {"target", "stem"}}, &warnings);
    QCOMPARE(warnings, QStringList{"Ignored key 'target' in rule extension"});
    QCOMPARE(rule->target, Target::Extension);
    QVERIFY(rule->unknownKeys.isEmpty());
    QVERIFY(!ruleToJson(*rule).contains("target"));
}

void TestRuleRegistry::chainRules()
{
    RuleChain chain;
    chain.insert(0, createRule("replace"));
    chain.insert(1, createRule("extension"));
    chain.at(1)->enabled = false;
    const QJsonArray rules = chainRulesToJson(chain);
    QCOMPARE(rules.size(), 2);
    QCOMPARE(rules.at(0).toObject(), ruleToJson(*chain.at(0)));
    QCOMPARE(rules.at(1).toObject().value("enabled").toBool(), false);
    QVERIFY(chainRulesToJson(RuleChain()).isEmpty());
}

void TestRuleRegistry::chainOptionsJson()
{
    QStringList warnings;
    QCOMPARE(chainOptionsFromJson(QJsonObject{{"portableNames", false}}, &warnings),
             (ChainOptions{false, false, false}));
    QVERIFY(warnings.isEmpty());
    QCOMPARE(chainOptionsFromJson(QJsonObject{}, &warnings), ChainOptions{});

    const ChainOptions options{false, true, true};
    const QJsonObject json = chainOptionsToJson(options);
    QCOMPARE(json, (QJsonObject{{"portableNames", false},
                                {"autoResolveDuplicates", true},
                                {"folderExtensions", true}}));
    QCOMPARE(chainOptionsFromJson(json, &warnings), options);
    QVERIFY(warnings.isEmpty());

    const ChainOptions bad = chainOptionsFromJson(
            QJsonObject{{"portableNames", "no"}, {"folderExtensions", true}, {"zzz", 1}},
            &warnings);
    QCOMPARE(bad, (ChainOptions{true, false, true}));
    QCOMPARE(warnings, (QStringList{"Bad value for 'portableNames' in options",
                                    "Unknown key 'zzz' in options"}));
    chainOptionsFromJson(QJsonObject{{"portableNames", 1}}, nullptr); // no crash
}

QTEST_GUILESS_MAIN(TestRuleRegistry)
#include "tst_ruleregistry.moc"
