// SPDX-License-Identifier: GPL-3.0-or-later
#include "models/rulelistmodel.h"
#include "rules/changecaserule.h"
#include "rules/cleanuprule.h"
#include "rules/extensionrule.h"
#include "rules/insertrule.h"
#include "rules/numberingrule.h"
#include "rules/removerule.h"
#include "rules/replacerule.h"
#include "rules/ruleregistry.h"
#include "rules/rulesummary.h"
#include "rules/unsupportedrule.h"

#include <QAbstractItemModelTester>
#include <QJsonObject>
#include <QMimeData>
#include <QSignalSpy>
#include <QtTest>

#include <functional>
#include <memory>

using namespace ctrm;
using namespace ctrm::app;

using RuleFactory = std::function<std::unique_ptr<Rule>()>;
Q_DECLARE_METATYPE(RuleFactory)

namespace {

template <typename R>
std::unique_ptr<R> make(const std::function<void(R &)> &setup)
{
    auto rule = std::make_unique<R>();
    setup(*rule);
    return rule;
}

RuleChain chainOf(std::initializer_list<const char *> types)
{
    RuleChain chain;
    for (const char *type : types)
        chain.insert(chain.size(), createRule(QLatin1StringView(type)));
    return chain;
}

QStringList types(const RuleChain &chain)
{
    QStringList result;
    for (int i = 0; i < chain.size(); ++i)
        result.append(chain.at(i)->type());
    return result;
}

} // namespace

class TestAppRuleList : public QObject
{
    Q_OBJECT
private slots:
    void displayNames();
    void summaries_data();
    void summaries();
    void summaryTargetSuffix();
    void summaryOff();
    void summaryError();
    void summaryEmptyNoTarget();
    void numberingSummaryDigits();
    void modelTester();
    void modelRoles();
    void moveEmitsChain();
    void checkStateDisables();
    void dragAndDrop();
    void chainOptions();
};

void TestAppRuleList::displayNames()
{
    QCOMPARE(ruleDisplayName("replace"), QStringLiteral("Replace"));
    QCOMPARE(ruleDisplayName("insert"), QStringLiteral("Insert"));
    QCOMPARE(ruleDisplayName("remove"), QStringLiteral("Remove"));
    QCOMPARE(ruleDisplayName("changeCase"), QStringLiteral("Change case"));
    QCOMPARE(ruleDisplayName("numbering"), QStringLiteral("Numbering"));
    QCOMPARE(ruleDisplayName("extension"), QStringLiteral("Extension"));
    QCOMPARE(ruleDisplayName("cleanup"), QStringLiteral("Clean up"));
    QCOMPARE(ruleDisplayName("blur"), QStringLiteral("Unsupported rule"));
    for (const QString &type : ruleTypes())
        QVERIFY(ruleDisplayName(type) != QStringLiteral("Unsupported rule"));
}

void TestAppRuleList::summaries_data()
{
    QTest::addColumn<RuleFactory>("factory");
    QTest::addColumn<QString>("summary");

    auto row = [](const char *name, RuleFactory f, const QString &summary) {
        QTest::newRow(name) << f << summary;
    };
    row(
            "replace literal", [] { return make<ReplaceRule>([](auto &r) { r.find = "IMG_"; }); },
            QStringLiteral("Replace \"IMG_\" with nothing"));
    row(
            "replace with text",
            [] {
                return make<ReplaceRule>([](auto &r) {
                    r.find = "a";
                    r.replace = "b";
                });
            },
            QStringLiteral("Replace \"a\" with \"b\""));
    row(
            "replace regex",
            [] {
                return make<ReplaceRule>([](auto &r) {
                    r.mode = ReplaceRule::Mode::Regex;
                    r.find = "^(\\d+)_";
                    r.replace = "$1-";
                });
            },
            QStringLiteral("Regex ^(\\d+)_ → $1-"));
    row(
            "replace wildcard",
            [] {
                return make<ReplaceRule>([](auto &r) {
                    r.mode = ReplaceRule::Mode::Wildcard;
                    r.find = "IMG*";
                });
            },
            QStringLiteral("Wildcard IMG* → nothing"));
    row(
            "replace first",
            [] {
                return make<ReplaceRule>([](auto &r) {
                    r.find = "a";
                    r.occurrence = ReplaceRule::Occurrence::First;
                });
            },
            QStringLiteral("Replace \"a\" with nothing, first only"));
    row(
            "insert prefix", [] { return make<InsertRule>([](auto &r) { r.text = "Trip_"; }); },
            QStringLiteral("Insert \"Trip_\" as prefix"));
    row(
            "insert suffix",
            [] {
                return make<InsertRule>([](auto &r) {
                    r.text = "_x";
                    r.position = InsertRule::Position::Suffix;
                });
            },
            QStringLiteral("Insert \"_x\" as suffix"));
    row(
            "insert percent text",
            [] {
                return make<InsertRule>([](auto &r) {
                    r.text = "%2";
                    r.position = InsertRule::Position::Index;
                    r.index = 5;
                });
            },
            QStringLiteral("Insert \"%2\" at 5"));
    row(
            "remove range",
            [] {
                return make<RemoveRule>([](auto &r) {
                    r.from = 0;
                    r.count = 3;
                });
            },
            QStringLiteral("Remove 3 characters from 0"));
    row(
            "remove one", [] { return make<RemoveRule>([](auto &) {}); },
            QStringLiteral("Remove 1 character from 0"));
    row(
            "remove range end",
            [] {
                return make<RemoveRule>([](auto &r) {
                    r.from = 2;
                    r.count = 4;
                    r.fromEnd = true;
                });
            },
            QStringLiteral("Remove 4 characters from 2 (from the end)"));
    row(
            "remove digits",
            [] {
                return make<RemoveRule>([](auto &r) {
                    r.mode = RemoveRule::Mode::Characters;
                    r.classes = RemoveRule::Digits;
                });
            },
            QStringLiteral("Remove digits"));
    row(
            "remove classes",
            [] {
                return make<RemoveRule>([](auto &r) {
                    r.mode = RemoveRule::Mode::Characters;
                    r.classes = RemoveRule::Digits | RemoveRule::Spaces | RemoveRule::Custom;
                    r.custom = "#";
                });
            },
            QStringLiteral("Remove digits, spaces, \"#\""));
    row(
            "remove between",
            [] { return make<RemoveRule>([](auto &r) { r.mode = RemoveRule::Mode::Between; }); },
            QStringLiteral("Remove text between ( )"));
    row(
            "title case",
            [] {
                return make<ChangeCaseRule>(
                        [](auto &r) { r.caseMode = ChangeCaseRule::Case::Title; });
            },
            QStringLiteral("Title Case"));
    row(
            "lower case", [] { return make<ChangeCaseRule>([](auto &) {}); },
            QStringLiteral("lower case"));
    row(
            "numbering suffix auto", [] { return make<NumberingRule>([](auto &) {}); },
            QStringLiteral("Suffix _001, from 1"));
    row(
            "numbering prefix",
            [] {
                return make<NumberingRule>([](auto &r) {
                    r.position = NumberingRule::Position::Prefix;
                    r.padding = 2;
                    r.start = 5;
                    r.step = 5;
                    r.separator = "-";
                });
            },
            QStringLiteral("Prefix 05-, from 5, step 5"));
    row(
            "numbering wide", [] { return make<NumberingRule>([](auto &r) { r.start = 12345; }); },
            QStringLiteral("Suffix _12345, from 12345"));
    row(
            "extension lower", [] { return make<ExtensionRule>([](auto &) {}); },
            QStringLiteral("Extension to lower case"));
    row(
            "extension set",
            [] {
                return make<ExtensionRule>([](auto &r) {
                    r.action = ExtensionRule::Action::Set;
                    r.value = "jpg";
                    r.onlyIfMatches = "jpeg;jpe";
                });
            },
            QStringLiteral("Extension to \"jpg\", only \"jpeg;jpe\""));
    row(
            "clean up", [] { return make<CleanUpRule>([](auto &) {}); },
            QStringLiteral("Trim, single spaces"));
    row(
            "clean up spaces",
            [] {
                return make<CleanUpRule>([](auto &r) {
                    r.trim = false;
                    r.collapseSpaces = false;
                    r.spacesTo = CleanUpRule::Spaces::Underscore;
                    r.replaceInvalid = true;
                });
            },
            QStringLiteral("Spaces to \"_\", replace bad characters with \"_\""));
    row(
            "clean up nothing",
            [] {
                return make<CleanUpRule>([](auto &r) {
                    r.trim = false;
                    r.collapseSpaces = false;
                });
            },
            QStringLiteral("No change"));
    // S10: a rule with no main text says what is missing, with no empty quotes.
    row(
            "replace empty", [] { return make<ReplaceRule>([](auto &) {}); },
            QStringLiteral("Replace: no Find text yet"));
    row(
            "insert empty", [] { return make<InsertRule>([](auto &) {}); },
            QStringLiteral("Insert: no text yet"));
    row(
            "remove no characters",
            [] {
                return make<RemoveRule>([](auto &r) {
                    r.mode = RemoveRule::Mode::Characters;
                    r.classes = RemoveRule::Custom;
                });
            },
            QStringLiteral("Remove: no characters yet"));
    row(
            "remove no marks",
            [] {
                return make<RemoveRule>([](auto &r) {
                    r.mode = RemoveRule::Mode::Between;
                    r.close.clear();
                });
            },
            QStringLiteral("Remove: no marks yet"));
    row(
            "unsupported",
            [] {
                return std::unique_ptr<Rule>(std::make_unique<UnsupportedRule>(
                        QJsonObject{{"type", "blur"}, {"radius", 3}}));
            },
            QStringLiteral("Unsupported rule (blur)"));
}

void TestAppRuleList::summaries()
{
    QFETCH(RuleFactory, factory);
    QFETCH(QString, summary);
    QCOMPARE(ruleSummary(*factory()), summary);
}

void TestAppRuleList::summaryTargetSuffix()
{
    auto rule = make<InsertRule>([](auto &r) {
        r.text = "x";
        r.target = Target::Extension;
    });
    QVERIFY(ruleSummary(*rule).endsWith(QStringLiteral(" (extension only)")));
    rule->target = Target::Name;
    QVERIFY(ruleSummary(*rule).endsWith(QStringLiteral(" (name and extension)")));
    rule->target = Target::Stem;
    QCOMPARE(ruleSummary(*rule), QStringLiteral("Insert \"x\" as prefix"));
    // The Extension rule has a fixed target: no suffix.
    QCOMPARE(ruleSummary(ExtensionRule()), QStringLiteral("Extension to lower case"));
}

void TestAppRuleList::summaryOff()
{
    auto rule = make<ChangeCaseRule>([](auto &r) {
        r.caseMode = ChangeCaseRule::Case::Upper;
        r.enabled = false;
    });
    QVERIFY(ruleSummary(*rule).contains(QStringLiteral("(off)")));
    QCOMPARE(ruleSummary(*rule), QStringLiteral("UPPER CASE (off)"));
}

void TestAppRuleList::summaryError()
{
    auto rule = make<ReplaceRule>([](auto &r) {
        r.mode = ReplaceRule::Mode::Regex;
        r.find = "(";
    });
    QVERIFY(rule->problem().has_value());
    QCOMPARE(ruleSummary(*rule), QStringLiteral("Error"));
}

void TestAppRuleList::summaryEmptyNoTarget()
{
    // S10: the text says what is missing; the target and "(off)" do not hide it.
    auto rule = make<InsertRule>([](auto &r) {
        r.target = Target::Extension;
        r.enabled = false;
    });
    QCOMPARE(ruleSummary(*rule), QStringLiteral("Insert: no text yet (off)"));
}

void TestAppRuleList::numberingSummaryDigits()
{
    // M24: with Auto padding, the summary has the digits of the editor example and the preview.
    NumberingRule rule;
    QCOMPARE(ruleSummary(rule, 10), QStringLiteral("Suffix _01, from 1"));
    QCOMPARE(ruleSummary(rule, 1), QStringLiteral("Suffix _1, from 1"));
    QCOMPARE(ruleSummary(rule, 120), QStringLiteral("Suffix _001, from 1"));

    RuleListModel model;
    RuleChain chain;
    chain.insert(0, std::make_unique<NumberingRule>());
    model.setChain(chain);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.setNumberedCount(10);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.index(0).data(RuleListModel::SummaryRole).toString(),
             QStringLiteral("Suffix _01, from 1"));
}

void TestAppRuleList::modelTester()
{
    // DisplayRole holds the position (plan: "<position> <name>"), so the display text of the
    // next row changes on insert and remove. The tester checks that text ("c.next"). Fail on all
    // other tester warnings.
    QTest::failOnWarning(QRegularExpression(QStringLiteral("^(?![\\s\\S]*c\\.next)")));
    RuleListModel model;
    QAbstractItemModelTester tester(&model,
                                    QAbstractItemModelTester::FailureReportingMode::Warning);
    model.setChain(chainOf({"replace", "insert", "remove"}));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.insertRule(1, createRule("numbering")), 1);
    QCOMPARE(model.insertRule(99, createRule("extension")), 4);
    QCOMPARE(types(model.chain()),
             QStringList({"replace", "numbering", "insert", "remove", "extension"}));
    model.removeRule(0);
    model.duplicateRule(1);
    QCOMPARE(types(model.chain()),
             QStringList({"numbering", "insert", "insert", "remove", "extension"}));
    model.moveRule(0, 4);
    model.moveRule(3, 1);
    QCOMPARE(types(model.chain()),
             QStringList({"insert", "extension", "insert", "remove", "numbering"}));
    model.setEnabled(2, false);
    model.ruleChanged(2);
    QVERIFY(model.moveRows({}, 4, 1, {}, 0));
    QCOMPARE(types(model.chain()),
             QStringList({"numbering", "insert", "extension", "insert", "remove"}));
    QVERIFY(model.moveRows({}, 0, 1, {}, 5)); // to the end
    QCOMPARE(types(model.chain()),
             QStringList({"insert", "extension", "insert", "remove", "numbering"}));
    QVERIFY(!model.moveRows({}, 0, 1, {}, 1)); // no move
    model.setChain(RuleChain());
    QCOMPARE(model.rowCount(), 0);
}

void TestAppRuleList::modelRoles()
{
    RuleListModel model;
    RuleChain chain = chainOf({"replace", "changeCase"});
    static_cast<ReplaceRule *>(chain.at(0))->find = QStringLiteral("IMG_");
    model.setChain(chain);
    const QModelIndex first = model.index(0);
    const QModelIndex second = model.index(1);
    QCOMPARE(first.data(Qt::DisplayRole).toString(), QStringLiteral("1 Replace"));
    QCOMPARE(second.data(Qt::DisplayRole).toString(), QStringLiteral("2 Change case"));
    QCOMPARE(first.data(RuleListModel::SummaryRole).toString(),
             QStringLiteral("Replace \"IMG_\" with nothing"));
    QCOMPARE(first.data(RuleListModel::TypeRole).toString(), QStringLiteral("replace"));
    QCOMPARE(first.data(RuleListModel::PositionRole).toInt(), 1);
    QCOMPARE(first.data(RuleListModel::HasErrorRole).toBool(), false);
    QCOMPARE(first.data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Checked);
    QVERIFY(model.flags(first) & Qt::ItemIsUserCheckable);
    QVERIFY(model.flags(first) & Qt::ItemIsDragEnabled);

    // An editor changes a rule, then calls ruleChanged().
    QSignalSpy data(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy chainSpy(&model, &RuleListModel::chainChanged);
    auto *replace = static_cast<ReplaceRule *>(model.chain().at(0));
    replace->mode = ReplaceRule::Mode::Regex;
    replace->find = QStringLiteral("(");
    model.ruleChanged(0);
    QCOMPARE(data.count(), 1);
    QCOMPARE(chainSpy.count(), 1);
    QCOMPARE(first.data(RuleListModel::HasErrorRole).toBool(), true);
    QCOMPARE(first.data(RuleListModel::SummaryRole).toString(), QStringLiteral("Error"));

    // Positions after an insert are new.
    model.insertRule(0, createRule("insert"));
    QCOMPARE(model.index(1).data(RuleListModel::PositionRole).toInt(), 2);
    QCOMPARE(model.index(2).data(Qt::DisplayRole).toString(), QStringLiteral("3 Change case"));
}

void TestAppRuleList::moveEmitsChain()
{
    RuleListModel model;
    model.setChain(chainOf({"replace", "insert", "remove"}));
    QSignalSpy spy(&model, &RuleListModel::chainChanged);
    model.moveRule(0, 2);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(types(model.chain()), QStringList({"insert", "remove", "replace"}));
    QCOMPARE(model.index(2).data(Qt::DisplayRole).toString(), QStringLiteral("3 Replace"));
    model.moveRule(1, 1);
    QCOMPARE(spy.count(), 1);
}

void TestAppRuleList::checkStateDisables()
{
    RuleListModel model;
    model.setChain(chainOf({"replace", "insert"}));
    QSignalSpy spy(&model, &RuleListModel::chainChanged);
    QVERIFY(model.setData(model.index(0), Qt::Unchecked, Qt::CheckStateRole));
    QCOMPARE(model.chain().at(0)->enabled, false);
    QCOMPARE(spy.count(), 1);
    QVERIFY(model.index(0).data(RuleListModel::SummaryRole).toString().endsWith(" (off)"));
    QVERIFY(model.setData(model.index(0), Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(model.chain().at(0)->enabled, true);
    QVERIFY(!model.setData(model.index(0), QStringLiteral("x"), Qt::EditRole));
}

void TestAppRuleList::dragAndDrop()
{
    RuleListModel model;
    model.setChain(chainOf({"replace", "insert", "remove"}));
    QCOMPARE(model.supportedDropActions(), Qt::MoveAction);
    std::unique_ptr<QMimeData> mime(model.mimeData({model.index(0)}));
    QVERIFY(mime);
    QVERIFY(model.canDropMimeData(mime.get(), Qt::MoveAction, 3, 0, {}));
    QVERIFY(!model.canDropMimeData(mime.get(), Qt::CopyAction, 3, 0, {}));
    QSignalSpy spy(&model, &RuleListModel::chainChanged);
    // The model moves the row itself; false tells the view not to remove the source row.
    QVERIFY(!model.dropMimeData(mime.get(), Qt::MoveAction, 3, 0, {}));
    QCOMPARE(types(model.chain()), QStringList({"insert", "remove", "replace"}));
    QCOMPARE(spy.count(), 1);

    // A drop on empty space (row -1, no parent) goes to the end.
    mime.reset(model.mimeData({model.index(0)}));
    QVERIFY(!model.dropMimeData(mime.get(), Qt::MoveAction, -1, -1, {}));
    QCOMPARE(types(model.chain()), QStringList({"remove", "replace", "insert"}));
    // A drop above a middle row: from 2 to before row 1.
    QSignalSpy data(&model, &QAbstractItemModel::dataChanged);
    mime.reset(model.mimeData({model.index(2)}));
    QVERIFY(!model.dropMimeData(mime.get(), Qt::MoveAction, 1, 0, {}));
    QCOMPARE(types(model.chain()), QStringList({"remove", "insert", "replace"}));
    QCOMPARE(data.count(), 1);
    QCOMPARE(data.first().at(0).toModelIndex().row(), 1);
    QCOMPARE(data.first().at(1).toModelIndex().row(), 2);
    // A drop of a row on its own place does nothing.
    spy.clear();
    mime.reset(model.mimeData({model.index(1)}));
    QVERIFY(!model.dropMimeData(mime.get(), Qt::MoveAction, 1, 0, {}));
    QVERIFY(!model.dropMimeData(mime.get(), Qt::MoveAction, 2, 0, {}));
    QCOMPARE(spy.count(), 0);
    // setData with an int check state.
    QVERIFY(model.setData(model.index(0), int(Qt::Unchecked), Qt::CheckStateRole));
    QCOMPARE(model.chain().at(0)->enabled, false);
}

void TestAppRuleList::chainOptions()
{
    RuleListModel model;
    QSignalSpy spy(&model, &RuleListModel::chainChanged);
    ChainOptions options;
    options.autoResolveDuplicates = true;
    model.setChainOptions(options);
    QCOMPARE(model.chain().options, options);
    QCOMPARE(spy.count(), 1);
    model.setChainOptions(options);
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestAppRuleList)
#include "tst_app_rulelist.moc"
