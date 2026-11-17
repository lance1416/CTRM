// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/graphemes.h"
#include "rules/rule.h"
#include "rules/rulechain.h"

#include <QJsonObject>
#include <QtTest>

using namespace ctrm;

namespace {

// Appends a fixed text to its target. Records the last text that transform() received.
class AppendRule : public Rule
{
public:
    explicit AppendRule(QString text, Target t = Target::Stem)
        : text(std::move(text))
    {
        target = t;
    }
    QString type() const override { return QStringLiteral("append"); }
    std::unique_ptr<Rule> clone() const override { return std::make_unique<AppendRule>(*this); }
    QStringList optionKeys() const override { return {QStringLiteral("text")}; }
    QJsonObject optionsToJson() const override { return {{QStringLiteral("text"), text}}; }
    void optionsFromJson(const QJsonObject &o, QStringList *) override
    {
        text = o.value(QStringLiteral("text")).toString();
    }

    QString text;
    mutable QString seen;

protected:
    QString transform(const QString &input, const RuleContext &) const override
    {
        seen = input;
        return input + text;
    }
};

// Always reports a problem. Does not change the name.
class ErrorRule : public Rule
{
public:
    QString type() const override { return QStringLiteral("error"); }
    std::unique_ptr<Rule> clone() const override { return std::make_unique<ErrorRule>(*this); }
    std::optional<RuleProblem> problem() const override
    {
        return RuleProblem{QStringLiteral("bad"), 3, false};
    }
    QStringList optionKeys() const override { return {}; }
    QJsonObject optionsToJson() const override { return {}; }
    void optionsFromJson(const QJsonObject &, QStringList *) override {}

protected:
    QString transform(const QString &input, const RuleContext &) const override { return input; }
};

RuleChain chainOf(std::unique_ptr<Rule> rule)
{
    RuleChain chain;
    chain.insert(0, std::move(rule));
    return chain;
}

} // namespace

class TestRuleChain : public QObject
{
    Q_OBJECT
private slots:
    void graphemes();
    void targetStrings();
    void applyToStem();
    void applyToExtension();
    void applyToName();
    void orderMatters();
    void disabledRuleHasNoEffect();
    void firstErrorSkipsDisabled();
    void copyIsDeep();
    void moveKeepsRules();
    void duplicateInsertsAfter();
    void takeAndClear();
    void folderNoExtension();
    void defaultsAreFixed();
};

void TestRuleChain::graphemes()
{
    const QString smile = QStringLiteral("🙂abc");
    QCOMPARE(graphemeCount(smile), 4);
    QCOMPARE(graphemeCount(QStringLiteral("éx")), 2); // e + combining acute
    QCOMPARE(graphemeCount(QStringLiteral("éx")), 2);
    QCOMPARE(graphemeCount(QString()), 0);
    QCOMPARE(graphemeOffset(smile, 1), 2);
    QCOMPARE(graphemeOffset(smile, 0), 0);
    QCOMPARE(graphemeOffset(smile, 4), smile.size());
    QCOMPARE(graphemeOffset(QStringLiteral("abc"), 99), 3);
    QCOMPARE(graphemeOffset(QStringLiteral("abc"), -1), 0);
    QCOMPARE(graphemeBoundaries(smile), (QList<int>{0, 2, 3, 4, 5}));
    QCOMPARE(graphemeBoundaries(QString()), (QList<int>{0}));
}

void TestRuleChain::targetStrings()
{
    for (Target t : {Target::Stem, Target::Extension, Target::Name})
        QCOMPARE(targetFromString(targetToString(t)), std::optional<Target>(t));
    QCOMPARE(targetToString(Target::Stem), QStringLiteral("stem"));
    QCOMPARE(targetToString(Target::Extension), QStringLiteral("extension"));
    QCOMPARE(targetToString(Target::Name), QStringLiteral("name"));
    QCOMPARE(targetFromString(QStringLiteral("bad")), std::nullopt);
}

void TestRuleChain::applyToStem()
{
    const RuleChain chain = chainOf(std::make_unique<AppendRule>("_x", Target::Stem));
    QCOMPARE(chain.apply("a.jpg", RuleContext{}), QStringLiteral("a_x.jpg"));
}

void TestRuleChain::applyToExtension()
{
    const RuleChain chain = chainOf(std::make_unique<AppendRule>("_x", Target::Extension));
    QCOMPARE(chain.apply("a.jpg", RuleContext{}), QStringLiteral("a.jpg_x"));
    QCOMPARE(chain.apply("a", RuleContext{}), QStringLiteral("a._x"));
}

void TestRuleChain::applyToName()
{
    const RuleChain chain = chainOf(std::make_unique<AppendRule>("_x", Target::Name));
    QCOMPARE(chain.apply("a.jpg", RuleContext{}), QStringLiteral("a.jpg_x"));
}

void TestRuleChain::orderMatters()
{
    RuleChain chain;
    chain.insert(0, std::make_unique<AppendRule>("1"));
    chain.insert(1, std::make_unique<AppendRule>("2"));
    QCOMPARE(chain.apply("n", RuleContext{}), QStringLiteral("n12"));
    chain.move(1, 0);
    QCOMPARE(chain.apply("n", RuleContext{}), QStringLiteral("n21"));
}

void TestRuleChain::disabledRuleHasNoEffect()
{
    RuleChain chain;
    chain.insert(0, std::make_unique<AppendRule>("1"));
    chain.insert(1, std::make_unique<AppendRule>("2"));
    chain.at(0)->enabled = false;
    QCOMPARE(chain.apply("n", RuleContext{}), QStringLiteral("n2"));
    chain.at(1)->enabled = false;
    QCOMPARE(chain.apply("n.txt", RuleContext{}), QStringLiteral("n.txt"));
}

void TestRuleChain::firstErrorSkipsDisabled()
{
    RuleChain chain;
    chain.insert(0, std::make_unique<ErrorRule>());
    chain.at(0)->enabled = false;
    QVERIFY(!chain.firstError().has_value());

    chain.insert(0, std::make_unique<AppendRule>("1"));
    chain.insert(2, std::make_unique<ErrorRule>());
    const auto error = chain.firstError();
    QVERIFY(error.has_value());
    QCOMPARE(error->ruleIndex, 2);
    QCOMPARE(error->problem.message, QStringLiteral("bad"));
    QCOMPARE(error->problem.position, 3);

    chain.at(1)->enabled = true;
    QCOMPARE(chain.firstError()->ruleIndex, 1);
}

void TestRuleChain::copyIsDeep()
{
    RuleChain original = chainOf(std::make_unique<AppendRule>("_a"));
    original.options.autoResolveDuplicates = true;

    RuleChain copy(original);
    static_cast<AppendRule *>(copy.at(0))->text = QStringLiteral("_b");
    QCOMPARE(original.apply("n", RuleContext{}), QStringLiteral("n_a"));
    QCOMPARE(copy.apply("n", RuleContext{}), QStringLiteral("n_b"));
    QCOMPARE(copy.options, original.options);

    RuleChain assigned;
    assigned = original;
    static_cast<AppendRule *>(assigned.at(0))->text = QStringLiteral("_c");
    QCOMPARE(original.apply("n", RuleContext{}), QStringLiteral("n_a"));
    QCOMPARE(assigned.apply("n", RuleContext{}), QStringLiteral("n_c"));
    QCOMPARE(assigned.options, original.options);
}

void TestRuleChain::moveKeepsRules()
{
    RuleChain source = chainOf(std::make_unique<AppendRule>("_a"));
    RuleChain moved(std::move(source));
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.apply("n", RuleContext{}), QStringLiteral("n_a"));

    RuleChain target;
    target = std::move(moved);
    QCOMPARE(target.size(), 1);
    QCOMPARE(target.apply("n", RuleContext{}), QStringLiteral("n_a"));
}

void TestRuleChain::duplicateInsertsAfter()
{
    RuleChain chain;
    chain.insert(0, std::make_unique<AppendRule>("_a"));
    chain.insert(1, std::make_unique<AppendRule>("_z"));
    chain.at(0)->enabled = false;
    chain.duplicate(0);
    QCOMPARE(chain.size(), 3);
    QVERIFY(chain.at(1) != chain.at(0));
    QCOMPARE(chain.at(1)->enabled, false);
    QCOMPARE(static_cast<const AppendRule *>(chain.at(1))->text, QStringLiteral("_a"));
    QCOMPARE(static_cast<const AppendRule *>(chain.at(2))->text, QStringLiteral("_z"));
}

void TestRuleChain::takeAndClear()
{
    RuleChain chain;
    chain.insert(0, std::make_unique<AppendRule>("_a"));
    chain.insert(1, std::make_unique<AppendRule>("_b"));
    const std::unique_ptr<Rule> taken = chain.take(0);
    QCOMPARE(static_cast<const AppendRule *>(taken.get())->text, QStringLiteral("_a"));
    QCOMPARE(chain.size(), 1);
    QCOMPARE(chain.apply("n", RuleContext{}), QStringLiteral("n_b"));
    chain.clear();
    QCOMPARE(chain.size(), 0);
    QCOMPARE(chain.apply("n.txt", RuleContext{}), QStringLiteral("n.txt"));
}

void TestRuleChain::folderNoExtension()
{
    auto rule = std::make_unique<AppendRule>("_x", Target::Extension);
    const AppendRule *raw = rule.get();
    const RuleChain chain = chainOf(std::move(rule));
    RuleContext ctx;
    ctx.isFolder = true;
    ctx.folderExtensions = false;
    chain.apply("Pro.v2", ctx);
    QCOMPARE(raw->seen, QString());

    ctx.folderExtensions = true;
    QCOMPARE(chain.apply("Pro.v2", ctx), QStringLiteral("Pro.v2_x"));
    QCOMPARE(raw->seen, QStringLiteral("v2"));
}

void TestRuleChain::defaultsAreFixed()
{
    QCOMPARE(ChainOptions{}, (ChainOptions{true, false, false}));
    const RuleChain chain;
    QCOMPARE(chain.options, (ChainOptions{true, false, false}));
    QCOMPARE(chain.size(), 0);
}

QTEST_GUILESS_MAIN(TestRuleChain)
#include "tst_rulechain.moc"
