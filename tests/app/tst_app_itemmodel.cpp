// SPDX-License-Identifier: GPL-3.0-or-later
#include "models/itemfilterproxy.h"
#include "models/itemtablemodel.h"
#include "text/messages.h"

#include <QAbstractItemModelTester>
#include <QMimeData>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace ctrm;
using namespace ctrm::app;

namespace {

Item makeItem(const QString &name, const QString &folder = QStringLiteral("/r"))
{
    Item it;
    it.folder = folder;
    it.name = name;
    return it;
}

QList<Item> makeItems(const QStringList &names)
{
    QList<Item> list;
    for (const QString &n : names)
        list.append(makeItem(n));
    return list;
}

ItemPreview row(Status s, const QString &newName, QList<Diagnostic> problems = {},
                QList<Diagnostic> warnings = {})
{
    ItemPreview p;
    p.status = s;
    p.newName = newName;
    p.problems = std::move(problems);
    p.warnings = std::move(warnings);
    return p;
}

Diagnostic diag(DiagCode c, const QString &text = {}, int count = 0, int limit = 0,
                int ruleIndex = -1)
{
    Diagnostic d{c, text};
    d.count = count;
    d.limit = limit;
    d.ruleIndex = ruleIndex;
    return d;
}

QStringList column(const QAbstractItemModel &m, int col)
{
    QStringList out;
    for (int r = 0; r < m.rowCount(); ++r)
        out.append(m.index(r, col).data().toString());
    return out;
}

void setPreview(ItemTableModel &m, const QList<ItemPreview> &rows)
{
    PreviewResult r;
    r.items = rows;
    m.setPreview(r);
}

} // namespace

Q_DECLARE_METATYPE(ctrm::Diagnostic)

class TestItemModel : public QObject
{
    Q_OBJECT
private slots:
    void modelTester();
    void addIgnoresDuplicates();
    void removeAndClear();
    void includeToggle();
    void moveRowsChangesOrder();
    void moveRowsKeepsPreview();
    void dragDropMovesRows();
    void sortOnceIsOneTime();
    void sortDescending();
    void sortThenMoveClearsArrow();
    void previewOfOtherSizeIgnored();
    void filterChanged();
    void filterProblems();
    void filterWarnings();
    void filterText();
    void texts_data();
    void texts();
    void statusTexts();
    void ruleErrorWithName();
    void blockedOldNameMessage();
    void tooltipListsAllMessages();
    void postRunClearsOnChange();
    void applyRenamesFollowsFolder();
    void applyRenamesNestedAndSwaps();
    void replaceIdentitiesById();
};

void TestItemModel::modelTester()
{
    ItemTableModel m;
    QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
    m.addItems(makeItems({QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")}));
    setPreview(m, {row(Status::Changed, QStringLiteral("z")), row(Status::Unchanged, "a"),
                   row(Status::Changed, QStringLiteral("y"))});
    m.moveRows({2}, 0);
    m.sortOnce(ItemTableModel::OldName, Qt::AscendingOrder);
    m.sortOnce(ItemTableModel::NewName, Qt::DescendingOrder);
    m.setIncluded({m.items()[0].id}, false);
    m.removeItems({m.items()[1].id});
    m.addItems(makeItems({QStringLiteral("d")}));
    m.clear();
    QCOMPARE(m.rowCount(), 0);
}

void TestItemModel::addIgnoresDuplicates()
{
    ItemTableModel m;
    QCOMPARE(m.addItems(makeItems({QStringLiteral("a"), QStringLiteral("b")})), 0);
    QCOMPARE(m.addItems({makeItem(QStringLiteral("a")), makeItem(QStringLiteral("c")),
                         makeItem(QStringLiteral("c"))}),
             2);
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(column(m, ItemTableModel::OldName),
             (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
    // Ids are unique and not 0.
    QSet<ItemId> ids;
    for (const Item &it : m.items())
        ids.insert(it.id);
    QCOMPARE(ids.size(), 3);
    QVERIFY(!ids.contains(0));
    // The same name in another folder is another item.
    QCOMPARE(m.addItems({makeItem(QStringLiteral("a"), QStringLiteral("/s"))}), 0);
    QCOMPARE(m.rowCount(), 4);
}

void TestItemModel::removeAndClear()
{
    ItemTableModel m;
    m.addItems(makeItems({"a", "b", "c", "d", "e"}));
    const QList<Item> items = m.items();
    QSignalSpy changed(&m, &ItemTableModel::listChanged);
    const quint64 v = m.listVersion();
    m.removeItems({items[1].id, items[2].id, items[4].id});
    QCOMPARE(column(m, ItemTableModel::OldName), (QStringList{"a", "d"}));
    QCOMPARE(m.preview().items.size(), 2);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m.listVersion(), v + 1);
    // A removed item can be added again.
    QCOMPARE(m.addItems({makeItem("b")}), 0);
    m.clear();
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.addItems({makeItem("a")}), 0);

    // Many separate rows (one model reset): previews stay with their items.
    ItemTableModel big;
    QAbstractItemModelTester tester(&big, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QStringList names;
    QList<ItemPreview> previews;
    for (int i = 0; i < 300; ++i) {
        names.append(QStringLiteral("n%1").arg(i));
        previews.append(row(Status::Changed, QStringLiteral("p%1").arg(i)));
    }
    big.addItems(makeItems(names));
    setPreview(big, previews);
    QList<ItemId> odd;
    for (int i = 1; i < 300; i += 2)
        odd.append(big.items()[i].id);
    big.removeItems(odd);
    QCOMPARE(big.rowCount(), 150);
    QCOMPARE(big.index(149, ItemTableModel::OldName).data().toString(), QStringLiteral("n298"));
    QCOMPARE(big.index(149, ItemTableModel::NewName).data().toString(), QStringLiteral("p298"));
}

void TestItemModel::includeToggle()
{
    ItemTableModel m;
    m.addItems(makeItems({QStringLiteral("a")}));
    QSignalSpy changed(&m, &ItemTableModel::listChanged);
    const quint64 v = m.listVersion();
    const QModelIndex idx = m.index(0, ItemTableModel::Include);
    QVERIFY(m.flags(idx) & Qt::ItemIsUserCheckable);
    QCOMPARE(idx.data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Checked);
    QVERIFY(m.setData(idx, Qt::Unchecked, Qt::CheckStateRole));
    QVERIFY(!m.items()[0].included);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m.listVersion(), v + 1);
    QVERIFY(idx.data(ItemTableModel::IsExcludedRole).toBool());
    QCOMPARE(m.index(0, ItemTableModel::Message).data().toString(), QStringLiteral("Excluded"));
    // No change, no signal.
    m.setIncluded({m.items()[0].id}, false);
    QCOMPARE(changed.count(), 1);
}

void TestItemModel::moveRowsChangesOrder()
{
    ItemTableModel m;
    m.addItems(makeItems({"a", "b", "c", "d"}));
    const quint64 v = m.listVersion();
    m.moveRows({2}, 0);
    QCOMPARE(column(m, ItemTableModel::OldName), (QStringList{"c", "a", "b", "d"}));
    QCOMPARE(m.listVersion(), v + 1);
    m.moveRows({0, 2}, 4); // c and b to the end
    QCOMPARE(column(m, ItemTableModel::OldName), (QStringList{"a", "d", "c", "b"}));
    m.moveRows({3, 1}, 1); // b and d before d: relative order d, b is kept as in the list
    QCOMPARE(column(m, ItemTableModel::OldName), (QStringList{"a", "d", "b", "c"}));
    const quint64 v2 = m.listVersion();
    m.moveRows({1}, 1); // no change
    QCOMPARE(m.listVersion(), v2);
}

void TestItemModel::moveRowsKeepsPreview()
{
    ItemTableModel m;
    m.addItems(makeItems({"a", "b"}));
    setPreview(m, {row(Status::Changed, "a2"), row(Status::Changed, "b2")});
    m.moveRows({1}, 0);
    QCOMPARE(column(m, ItemTableModel::NewName), (QStringList{"b2", "a2"}));
}

void TestItemModel::dragDropMovesRows()
{
    ItemTableModel m;
    m.addItems(makeItems({"a", "b", "c"}));
    QCOMPARE(m.supportedDropActions(), Qt::MoveAction);
    QCOMPARE(m.mimeTypes(), QStringList{QStringLiteral("application/x-ctrm-rows")});
    std::unique_ptr<QMimeData> mime(m.mimeData({m.index(2, 2), m.index(2, 3)}));
    QVERIFY(m.canDropMimeData(mime.get(), Qt::MoveAction, 0, 0, {}));
    m.dropMimeData(mime.get(), Qt::MoveAction, 0, 0, {});
    QCOMPARE(column(m, ItemTableModel::OldName), (QStringList{"c", "a", "b"}));
    // Old drag data (the list changed after the drag started) does nothing.
    std::unique_ptr<QMimeData> old(m.mimeData({m.index(0, 2)}));
    m.addItems(makeItems({"d"}));
    m.dropMimeData(old.get(), Qt::MoveAction, 4, 0, {});
    QCOMPARE(column(m, ItemTableModel::OldName), (QStringList{"c", "a", "b", "d"}));
}

void TestItemModel::sortOnceIsOneTime()
{
    ItemTableModel m;
    m.addItems(makeItems({"a", "b", "c"}));
    setPreview(m, {row(Status::Changed, "file10"), row(Status::Changed, "file2"),
                   row(Status::Changed, "File3")});
    m.sortOnce(ItemTableModel::NewName, Qt::AscendingOrder);
    QCOMPARE(column(m, ItemTableModel::OldName), (QStringList{"b", "c", "a"}));
    QCOMPARE(m.sortedColumn(), int(ItemTableModel::NewName));
    // New names change: rows do not move.
    setPreview(m,
               {row(Status::Changed, "z"), row(Status::Changed, "y"), row(Status::Changed, "x")});
    QCOMPARE(column(m, ItemTableModel::OldName), (QStringList{"b", "c", "a"}));
}

void TestItemModel::sortDescending()
{
    ItemTableModel m;
    m.addItems(makeItems({"b", "a2", "a10"}));
    m.sortOnce(ItemTableModel::OldName, Qt::DescendingOrder);
    QCOMPARE(column(m, ItemTableModel::OldName), (QStringList{"b", "a10", "a2"}));
    QCOMPARE(m.sortedOrder(), Qt::DescendingOrder);
}

void TestItemModel::sortThenMoveClearsArrow()
{
    ItemTableModel m;
    m.addItems(makeItems({"b", "a"}));
    m.sortOnce(ItemTableModel::OldName, Qt::AscendingOrder);
    QCOMPARE(m.sortedColumn(), int(ItemTableModel::OldName));
    m.moveRows({1}, 0);
    QCOMPARE(m.sortedColumn(), -1);
    m.sortOnce(ItemTableModel::OldName, Qt::AscendingOrder);
    m.addItems(makeItems({"c"}));
    QCOMPARE(m.sortedColumn(), -1);
}

void TestItemModel::previewOfOtherSizeIgnored()
{
    ItemTableModel m;
    m.addItems(makeItems({"a", "b"}));
    setPreview(m, {row(Status::Changed, "x")});
    QCOMPARE(column(m, ItemTableModel::NewName), (QStringList{"a", "b"}));
    PreviewResult cancelled;
    cancelled.items = {row(Status::Changed, "x"), row(Status::Changed, "y")};
    cancelled.cancelled = true;
    m.setPreview(cancelled);
    QCOMPARE(column(m, ItemTableModel::NewName), (QStringList{"a", "b"}));
}

void TestItemModel::filterChanged()
{
    ItemTableModel m;
    m.addItems(makeItems({"a", "b", "c", "d", "e"}));
    setPreview(m,
               {row(Status::Changed, "a2"), row(Status::Unchanged, "b"), row(Status::CaseOnly, "C"),
                row(Status::Changed, ".d", {}, {diag(DiagCode::BecomesHidden)}),
                row(Status::Conflict, "x", {diag(DiagCode::DuplicateInBatch, {}, 1)})});
    ItemFilterProxy proxy;
    QAbstractItemModelTester tester(&proxy, QAbstractItemModelTester::FailureReportingMode::QtTest);
    proxy.setSourceModel(&m);
    proxy.setShowMode(ShowMode::Changed);
    QCOMPARE(column(proxy, ItemTableModel::OldName), (QStringList{"a", "c", "d"}));
    proxy.setShowMode(ShowMode::All);
    QCOMPARE(proxy.rowCount(), 5);
}

void TestItemModel::filterProblems()
{
    ItemTableModel m;
    m.addItems(makeItems({"a", "b", "c", "d", "e", "f", "g"}));
    setPreview(m, {row(Status::Conflict, "x", {diag(DiagCode::ExistsInFolder, "x")}),
                   row(Status::Invalid, "?", {diag(DiagCode::ForbiddenChar, "?")}),
                   row(Status::Missing, "c", {diag(DiagCode::NotOnDisk)}),
                   row(Status::RuleError, "d", {diag(DiagCode::RuleFailed, "bad", -1, 0, 0)}),
                   row(Status::Changed, ".e", {}, {diag(DiagCode::BecomesHidden)}),
                   row(Status::Changed, "f2"), row(Status::Unchanged, "g")});
    ItemFilterProxy proxy;
    proxy.setSourceModel(&m);
    proxy.setShowMode(ShowMode::Problems);
    QCOMPARE(column(proxy, ItemTableModel::OldName), (QStringList{"a", "b", "c", "d"}));

    // After a run: Failed rows are problems too.
    const QList<Item> items = m.items();
    m.setPostRun({{items[5].id, {PostRun::Failed, QStringLiteral("Permission denied")}},
                  {items[6].id, {PostRun::Renamed, {}}}});
    QCOMPARE(column(proxy, ItemTableModel::OldName), (QStringList{"a", "b", "c", "d", "f"}));

    // An excluded row is never a problem.
    m.setIncluded({items[0].id}, false);
    QCOMPARE(column(proxy, ItemTableModel::OldName), (QStringList{"b", "c", "d"}));
}

void TestItemModel::filterWarnings()
{
    ItemTableModel m;
    m.addItems(makeItems({"a", "b", "c"}));
    setPreview(m, {row(Status::Changed, ".a", {}, {diag(DiagCode::BecomesHidden)}),
                   row(Status::Changed, "b2"),
                   row(Status::Conflict, "x", {diag(DiagCode::ExistsInFolder, "x")},
                       {diag(DiagCode::ExtensionChanged)})});
    ItemFilterProxy proxy;
    proxy.setSourceModel(&m);
    proxy.setShowMode(ShowMode::Warnings);
    QCOMPARE(column(proxy, ItemTableModel::OldName), QStringList{"a"});
}

void TestItemModel::filterText()
{
    ItemTableModel m;
    m.addItems(makeItems({"IMG_1.jpg", "photo.jpg", "doc.txt"}));
    setPreview(m, {row(Status::Unchanged, "IMG_1.jpg"), row(Status::Changed, "img_2.jpg"),
                   row(Status::Unchanged, "doc.txt")});
    ItemFilterProxy proxy;
    proxy.setSourceModel(&m);
    proxy.setText(QStringLiteral("img"));
    QCOMPARE(column(proxy, ItemTableModel::OldName), (QStringList{"IMG_1.jpg", "photo.jpg"}));
    // AND with the Show value.
    proxy.setShowMode(ShowMode::Changed);
    QCOMPARE(column(proxy, ItemTableModel::OldName), QStringList{"photo.jpg"});
    proxy.setText({});
    proxy.setShowMode(ShowMode::All);
    QCOMPARE(proxy.rowCount(), 3);
}

void TestItemModel::texts_data()
{
    QTest::addColumn<Diagnostic>("d");
    QTest::addColumn<bool>("portable");
    QTest::addColumn<QString>("expected");

    auto add = [](const char *tag, Diagnostic d, bool portable, const char *text) {
        QTest::newRow(tag) << d << portable << QString::fromUtf8(text);
    };
    add("duplicate", diag(DiagCode::DuplicateInBatch, {}, 2), true,
        "Same new name as 2 other items");
    add("duplicateOne", diag(DiagCode::DuplicateInBatch, {}, 1), true,
        "Same new name as 1 other item");
    add("exists", diag(DiagCode::ExistsInFolder, "a.txt"), true,
        "An item named 'a.txt' is already in this folder");
    add("ignoringCase", diag(DiagCode::ExistsIgnoringCase, "Trip.jpg"), true,
        "Same name as 'Trip.jpg' when letter case is ignored. This folder ignores letter case.");
    add("normalization", diag(DiagCode::ExistsNormalization, "Cafe.jpg"), true,
        "Same name as 'Cafe.jpg' in this folder (the folder treats these characters as equal).");
    add("empty", diag(DiagCode::EmptyName), true, "Invalid: the name is empty");
    add("dot", diag(DiagCode::DotName), true, "Invalid: '.' and '..' are not permitted");
    add("forbiddenPortable", diag(DiagCode::ForbiddenChar, "?"), true,
        "Invalid: '?' is not permitted in portable names");
    add("forbidden", diag(DiagCode::ForbiddenChar, "/"), false,
        "Invalid: '/' is not permitted in names");
    add("reserved", diag(DiagCode::ReservedName, "CON"), true,
        "Invalid: 'CON' is a reserved name on Windows");
    add("trailing", diag(DiagCode::TrailingSpaceOrDot), true,
        "Invalid: the name ends with a space or a dot");
    add("nameTooLong", diag(DiagCode::NameTooLong, "utf16", 262, 255), true,
        "Invalid: the name is too long (262 of 255)");
    add("pathTooLong", diag(DiagCode::PathTooLong, {}, 1100, 1023), true,
        "Invalid: the path is too long (1100 of 1023)");
    add("notOnDisk", diag(DiagCode::NotOnDisk), true,
        "Missing: the item is not on disk. Refresh or remove it.");
    add("identityChanged", diag(DiagCode::IdentityChanged), true,
        "Missing: the item changed on disk. Refresh or remove it.");
    add("ruleFailedNoPosition", diag(DiagCode::RuleFailed, "bad option", -1, 0, 0), true,
        "Rule error in rule 1: bad option");
    add("ruleFailedPosition", diag(DiagCode::RuleFailed, "missing )", 6, 0, 1), true,
        "Rule error in rule 2: missing ) at position 7");
    add("unsupported", diag(DiagCode::UnsupportedRule, "future", 0, 0, 2), true,
        "Rule error in rule 3: Unsupported rule (future)");
    add("hidden", diag(DiagCode::BecomesHidden), true,
        "Will rename. Warning: the item will become hidden");
    add("visible", diag(DiagCode::BecomesVisible), true,
        "Will rename. Warning: the item will become visible");
    add("extension", diag(DiagCode::ExtensionChanged), true,
        "Will rename. Warning: the extension changes");
    add("path260", diag(DiagCode::PathLongerThan260), true,
        "Will rename. Warning: the path is longer than 260 characters");
    add("endsWith", diag(DiagCode::EndsWithSpaceOrDot), true,
        "Will rename. Warning: the name ends with a space or a dot");
    add("whitespace", diag(DiagCode::LeadingOrTrailingWhitespace), true,
        "Will rename. Warning: the name starts or ends with a space");
    add("weak", diag(DiagCode::WeakIdentity, "exfat"), true,
        "Will rename. Warning: weak identity check");
}

void TestItemModel::texts()
{
    QFETCH(Diagnostic, d);
    QFETCH(bool, portable);
    QFETCH(QString, expected);
    QCOMPARE(diagnosticText(d, portable), expected);
}

void TestItemModel::statusTexts()
{
    QCOMPARE(statusText(row(Status::Unchanged, "a"), true), QStringLiteral("No change"));
    QCOMPARE(statusText(row(Status::Changed, "a"), true), QStringLiteral("Will rename"));
    QCOMPARE(statusText(row(Status::CaseOnly, "a"), true),
             QStringLiteral("Will rename (letter case only)"));
    ItemPreview excluded = row(Status::Unchanged, "a");
    excluded.included = false;
    QCOMPARE(statusText(excluded, true), QStringLiteral("Excluded"));
    QCOMPARE(statusText(row(Status::CaseOnly, ".a", {}, {diag(DiagCode::BecomesHidden)}), true),
             QStringLiteral("Will rename. Warning: the item will become hidden"));
    QCOMPARE(statusText(row(Status::Invalid, "a?", {diag(DiagCode::ForbiddenChar, "?")}), false),
             QStringLiteral("Invalid: '?' is not permitted in names"));

    QCOMPARE(postRunText({PostRun::Renamed, {}}), QStringLiteral("Renamed"));
    QCOMPARE(postRunText({PostRun::Failed, QStringLiteral("Permission denied")}),
             QStringLiteral("Failed: Permission denied"));
    QCOMPARE(postRunText({PostRun::Skipped, QStringLiteral("conflict")}),
             QStringLiteral("Skipped: conflict"));
    QCOMPARE(postRunText({PostRun::RolledBack, {}}), QStringLiteral("Rolled back to the old name"));
    QCOMPARE(postRunText({}), QString());
}

void TestItemModel::ruleErrorWithName()
{
    const Diagnostic d = diag(DiagCode::RuleFailed, "missing )", 6, 0, 1);
    QCOMPARE(diagnosticText(d, true, {QStringLiteral("Insert"), QStringLiteral("Replace")}),
             QStringLiteral("Rule error in rule 2 (Replace): missing ) at position 7"));
    ItemTableModel m;
    m.addItems(makeItems({"a"}));
    setPreview(m, {row(Status::RuleError, "a", {d})});
    m.setRuleNames({QStringLiteral("Insert"), QStringLiteral("Replace")});
    QCOMPARE(m.index(0, ItemTableModel::Message).data().toString(),
             QStringLiteral("Rule error in rule 2 (Replace): missing ) at position 7"));
}

void TestItemModel::blockedOldNameMessage()
{
    // AC-B3: A is Invalid. B wants A's old name.
    ItemTableModel m;
    m.addItems(makeItems({"a.txt", "b.txt", "c.txt", "d.txt", "e.txt"}));
    setPreview(m, {row(Status::Invalid, "a?.txt", {diag(DiagCode::ForbiddenChar, "?")}),
                   row(Status::Conflict, "a.txt", {diag(DiagCode::ExistsInFolder, "a.txt")}),
                   // A conflict with a name on disk that is not a blocked item.
                   row(Status::Conflict, "z.txt", {diag(DiagCode::ExistsInFolder, "z.txt")}),
                   // A conflict with an item that is not blocked (Unchanged keeps its name).
                   row(Status::Conflict, "e.txt", {diag(DiagCode::ExistsInFolder, "e.txt")}),
                   row(Status::Unchanged, "e.txt")});
    QCOMPARE(m.index(1, ItemTableModel::Message).data().toString(),
             QStringLiteral("Conflicts with the old name of 'a.txt', which is blocked."));
    QCOMPARE(m.index(2, ItemTableModel::Message).data().toString(),
             QStringLiteral("An item named 'z.txt' is already in this folder"));
    QCOMPARE(m.index(3, ItemTableModel::Message).data().toString(),
             QStringLiteral("An item named 'e.txt' is already in this folder"));

    // The same name in another folder is not the blocked item.
    ItemTableModel other;
    other.addItems({makeItem("a.txt", "/s"), makeItem("b.txt", "/r")});
    setPreview(other, {row(Status::Invalid, "a?.txt", {diag(DiagCode::ForbiddenChar, "?")}),
                       row(Status::Conflict, "a.txt", {diag(DiagCode::ExistsInFolder, "a.txt")})});
    QCOMPARE(other.index(1, ItemTableModel::Message).data().toString(),
             QStringLiteral("An item named 'a.txt' is already in this folder"));
}

void TestItemModel::tooltipListsAllMessages()
{
    ItemTableModel m;
    m.addItems(makeItems({"a"}));
    setPreview(m, {row(Status::Conflict, ".x", {diag(DiagCode::DuplicateInBatch, {}, 1)},
                       {diag(DiagCode::BecomesHidden)})});
    const QString tip = m.index(0, ItemTableModel::NewName).data(Qt::ToolTipRole).toString();
    QVERIFY(tip.startsWith(QStringLiteral(".x\n")));
    QVERIFY(tip.contains(QStringLiteral("Same new name as 1 other item")));
    QVERIFY(tip.contains(QStringLiteral("the item will become hidden")));
}

void TestItemModel::postRunClearsOnChange()
{
    ItemTableModel m;
    m.addItems(makeItems({"a", "b"}));
    const QList<Item> items = m.items();
    m.setPostRun({{items[0].id, {PostRun::Renamed, {}}}});
    QCOMPARE(m.index(0, 0).data(ItemTableModel::PostRunRole).toInt(), int(PostRun::Renamed));
    QCOMPARE(m.index(0, ItemTableModel::Message).data().toString(), QStringLiteral("Renamed"));
    m.moveRows({1}, 0);
    QCOMPARE(m.index(1, 0).data(ItemTableModel::PostRunRole).toInt(), int(PostRun::None));
    QCOMPARE(m.index(1, ItemTableModel::Message).data().toString(), QStringLiteral("No change"));
}

void TestItemModel::applyRenamesFollowsFolder()
{
    ItemTableModel m;
    Item dir = makeItem(QStringLiteral("d"), QStringLiteral("/r"));
    dir.isFolder = true;
    m.addItems({dir, makeItem("x.txt", "/r/d"), makeItem("y", "/r/d/sub"), makeItem("z", "/r/dd"),
                makeItem("w.txt", "/r/d")});
    const QList<Item> items = m.items();
    setPreview(m, {row(Status::Changed, "e"), row(Status::Changed, "x2.txt"),
                   row(Status::Unchanged, "y"), row(Status::Unchanged, "z"),
                   row(Status::Changed, "w2.txt")});
    m.setPostRun({{items[0].id, {PostRun::Renamed, {}}}});
    const quint64 version = m.listVersion();

    ReportRow folderRow;
    folderRow.item = items[0].id;
    folderRow.oldPath = QStringLiteral("/r/d");
    folderRow.finalPath = QStringLiteral("/r/e");
    folderRow.result = ReportRow::Result::Renamed;
    ReportRow childRow; // path under the old folder path
    childRow.item = items[1].id;
    childRow.oldPath = QStringLiteral("/r/d/x.txt");
    childRow.finalPath = QStringLiteral("/r/d/x2.txt");
    childRow.result = ReportRow::Result::Renamed;
    ReportRow failed; // a failed item keeps its name
    failed.item = items[4].id;
    failed.oldPath = QStringLiteral("/r/d/w.txt");
    failed.finalPath = QStringLiteral("/r/d/w.txt");
    failed.result = ReportRow::Result::Failed;
    m.applyRenames({folderRow, childRow, failed});

    const QList<Item> after = m.items();
    QCOMPARE(after[0].path(), QStringLiteral("/r/e"));
    QCOMPARE(after[1].path(), QStringLiteral("/r/e/x2.txt"));
    QCOMPARE(after[2].path(), QStringLiteral("/r/e/sub/y"));
    QCOMPARE(after[3].path(), QStringLiteral("/r/dd/z")); // "/r/dd" is not in "/r/d"
    QCOMPARE(after[4].path(), QStringLiteral("/r/e/w.txt"));
    // Rows show the current names until the next preview.
    QCOMPARE(column(m, ItemTableModel::NewName), (QStringList{"e", "x2.txt", "y", "z", "w.txt"}));
    QCOMPARE(m.listVersion(), version);
    QCOMPARE(m.index(0, 0).data(ItemTableModel::PostRunRole).toInt(), int(PostRun::Renamed));
    // The new paths are the duplicate check paths now.
    QCOMPARE(m.addItems({makeItem("x2.txt", "/r/e")}), 1);
    QCOMPARE(m.addItems({makeItem("x.txt", "/r/d")}), 0);
}

void TestItemModel::applyRenamesNestedAndSwaps()
{
    auto report = [](ItemId id, const QString &from, const QString &to) {
        ReportRow rr;
        rr.item = id;
        rr.oldPath = from;
        rr.finalPath = to;
        rr.result = ReportRow::Result::Renamed;
        return rr;
    };
    ItemTableModel m;
    Item d = makeItem("d", "/r");
    d.isFolder = true;
    Item s = makeItem("s", "/r/d");
    s.isFolder = true;
    Item a = makeItem("a", "/r");
    a.isFolder = true;
    Item b = makeItem("b", "/r");
    b.isFolder = true;
    m.addItems({d, s, makeItem("f", "/r/d/s"), a, b, makeItem("x", "/r/a"), makeItem("y", "/r/b"),
                makeItem("p", "/q"), makeItem("q", "/q")});
    const QList<Item> it = m.items();
    m.applyRenames({// Nested: /r/d -> e and /r/d/s -> t (all paths from before the batch).
                    report(it[0].id, "/r/d", "/r/e"), report(it[1].id, "/r/d/s", "/r/d/t"),
                    // Folder swap a <-> b.
                    report(it[3].id, "/r/a", "/r/b"), report(it[4].id, "/r/b", "/r/a"),
                    // Name swap p <-> q.
                    report(it[7].id, "/q/p", "/q/q"), report(it[8].id, "/q/q", "/q/p")});
    QStringList paths;
    for (const Item &i : m.items())
        paths.append(i.path());
    QCOMPARE(paths, (QStringList{"/r/e", "/r/e/t", "/r/e/t/f", "/r/b", "/r/a", "/r/b/x", "/r/a/y",
                                 "/q/q", "/q/p"}));
    // Both swapped names are still known as list paths.
    QCOMPARE(m.addItems({makeItem("p", "/q"), makeItem("q", "/q")}), 2);
}

void TestItemModel::replaceIdentitiesById()
{
    ItemTableModel m;
    m.addItems(makeItems({"a", "b"}));
    QList<Item> fresh = {m.items()[1]};
    fresh[0].identity.type = IdentityType::Strong;
    fresh[0].identity.idLow = 42;
    m.replaceIdentities(fresh);
    QCOMPARE(m.items()[1].identity.idLow, quint64(42));
    QCOMPARE(m.items()[0].identity.idLow, quint64(0));
}

QTEST_MAIN(TestItemModel)
#include "tst_app_itemmodel.moc"
