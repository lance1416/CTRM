// SPDX-License-Identifier: GPL-3.0-or-later
#include "fakefilesystem.h"
#include "fs/folderinfo.h"
#include "preview/previewengine.h"
#include "rules/changecaserule.h"
#include "rules/insertrule.h"
#include "rules/numberingrule.h"
#include "rules/replacerule.h"
#include "rules/unsupportedrule.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QtTest>

using namespace ctrm;

namespace {

const QString kFolder = QStringLiteral("/f");

VolumeInfo volume(CaseSensitivity cs, bool nfd = false)
{
    return {cs, nfd, IdentityType::Strong, QStringLiteral("fakefs")};
}

// "Caf" + U+00E9 (NFC) and "Cafe" + U+0301 (NFD).
QString cafeNfc(const QString &tail = QStringLiteral(".txt"))
{
    return QStringLiteral("Caf") + QChar(0x00E9) + tail;
}
QString cafeNfd(const QString &tail = QStringLiteral(".txt"))
{
    return QStringLiteral("Cafe") + QChar(0x0301) + tail;
}

// One folder "/f" in a fake file system. Items are files on disk unless noted.
class Fixture
{
public:
    explicit Fixture(VolumeInfo v = volume(CaseSensitivity::Sensitive))
    {
        fs.addFolder(kFolder, v);
        in.os = OsRules::Linux;
    }

    void disk(const QString &name) { fs.addFile(kFolder + QLatin1Char('/') + name); }

    ItemId item(const QString &name, bool included = true, bool onDisk = true)
    {
        if (onDisk)
            disk(name);
        Item it;
        it.id = m_next++;
        it.folder = kFolder;
        it.name = name;
        it.included = included;
        in.items.append(it);
        return it.id;
    }

    void addRule(std::unique_ptr<Rule> r) { in.chain.insert(in.chain.size(), std::move(r)); }

    void refresh() { in.folders[kFolder] = readFolderInfo(fs, kFolder); }

    PreviewResult preview()
    {
        refresh();
        return computePreview(in);
    }

    // Evaluates fixed new names (one per item, in order).
    PreviewResult evaluate(const QStringList &newNames)
    {
        refresh();
        QList<ProposedName> names;
        for (const QString &n : newNames)
            names.append({n, std::nullopt});
        return evaluateNames(in, names, NameValidator(in.os, in.chain.options.portableNames));
    }

    int indexOf(ItemId id) const
    {
        for (int i = 0; i < in.items.size(); ++i) {
            if (in.items[i].id == id)
                return i;
        }
        return -1;
    }

    FakeFileSystem fs;
    PreviewInput in;

private:
    ItemId m_next = 1;
};

std::unique_ptr<Rule> insertPrefix(const QString &text)
{
    auto r = std::make_unique<InsertRule>();
    r->text = text;
    r->position = InsertRule::Position::Prefix;
    return r;
}

std::unique_ptr<Rule> replaceName(const QString &find, const QString &replace,
                                  ReplaceRule::Mode mode = ReplaceRule::Mode::Literal)
{
    auto r = std::make_unique<ReplaceRule>();
    r->find = find;
    r->replace = replace;
    r->mode = mode;
    r->target = Target::Name;
    return r;
}

bool hasCode(const QList<Diagnostic> &list, DiagCode code)
{
    return std::any_of(list.cbegin(), list.cend(),
                       [code](const Diagnostic &d) { return d.code == code; });
}

Diagnostic firstProblem(const ItemPreview &p)
{
    return p.problems.isEmpty() ? Diagnostic{DiagCode::EmptyName, QStringLiteral("<none>")}
                                : p.problems.first();
}

} // namespace

class TestPreviewEngine : public QObject
{
    Q_OBJECT
private slots:
    void unchangedAndChanged();
    void caseOnly();
    void caseOnlyNormalization();
    void duplicateConflict();
    void existingConflict();
    void caseInsensitiveConflict();
    void unknownCaseIsInsensitive();
    void nfdConflictMac();
    void chainIsNotConflict();
    void swapIsNotConflict();
    void excludedKeepsName();
    void unchangedKeepsName();
    void conflictCascade();
    void sameKeyTwiceOnDisk();
    void invalidPortable();
    void invalidPath();
    void ruleErrorAllItems();
    void unsupportedBlocks();
    void disabledUnsupportedDoesNotBlock();
    void missingItem();
    void unreadableFolderMissing();
    void identityChangedMissing();
    void extensionWarning();
    void hiddenWarning();
    void nameWarnings();
    void autoResolve();
    void autoResolveSkipsTaken();
    void autoResolveFirstTakenOnDisk();
    void autoResolveSingleItemVsDisk();
    void autoResolveNoExtension();
    void autoResolveTooLong();
    void autoResolveOff();
    void numberingFollowsOrder();
    void skipBlockedCascade();
    void skipOwnConflictNotCascade();
    void skipCascadeChain();
    void skipKeepsNumbering();
    void skipInputMarksSkipped();
    void summaryCounts();
    void cancelStops();
    void perf10k();
};

void TestPreviewEngine::unchangedAndChanged()
{
    Fixture f;
    f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"));
    PreviewResult r = f.preview();
    QCOMPARE(r.items.size(), 2);
    for (const ItemPreview &p : r.items) {
        QCOMPARE(p.status, Status::Unchanged);
        QVERIFY(p.problems.isEmpty());
    }
    QCOMPARE(r.items[0].newName, QStringLiteral("a.txt"));
    QCOMPARE(r.summary.unchanged, 2);

    f.addRule(insertPrefix(QStringLiteral("x")));
    r = f.preview();
    QCOMPARE(r.items[0].status, Status::Changed);
    QCOMPARE(r.items[0].newName, QStringLiteral("xa.txt"));
    QCOMPARE(r.items[1].newName, QStringLiteral("xb.txt"));
    QCOMPARE(r.summary.willChange, 2);
    QCOMPARE(r.summary.blocked(), 0);
}

void TestPreviewEngine::caseOnly()
{
    Fixture f(volume(CaseSensitivity::Insensitive));
    f.item(QStringLiteral("A.txt"));
    auto rule = std::make_unique<ChangeCaseRule>();
    rule->caseMode = ChangeCaseRule::Case::Lower;
    f.addRule(std::move(rule));
    const PreviewResult r = f.preview();
    QCOMPARE(r.items[0].newName, QStringLiteral("a.txt"));
    QCOMPARE(r.items[0].status, Status::CaseOnly);
    QCOMPARE(r.summary.willChange, 1);

    // Also CaseOnly on a case-sensitive folder (the status does not depend on the volume).
    Fixture s;
    s.item(QStringLiteral("A.txt"));
    QCOMPARE(s.evaluate({QStringLiteral("a.txt")}).items[0].status, Status::CaseOnly);
}

void TestPreviewEngine::caseOnlyNormalization()
{
    Fixture f(volume(CaseSensitivity::Insensitive, true));
    f.item(cafeNfc());
    f.item(QStringLiteral("x.txt"));
    // Normalization only, and normalization + case.
    const PreviewResult r = f.evaluate({cafeNfd(), QStringLiteral("X.txt")});
    QCOMPARE(r.items[0].status, Status::CaseOnly);
    QCOMPARE(r.items[1].status, Status::CaseOnly);

    Fixture g(volume(CaseSensitivity::Insensitive, true));
    g.item(cafeNfc());
    const QString lowerNfd = QStringLiteral("cafe") + QChar(0x0301) + QStringLiteral(".txt");
    QCOMPARE(g.evaluate({lowerNfd}).items[0].status, Status::CaseOnly);
}

void TestPreviewEngine::duplicateConflict()
{
    Fixture f;
    f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"));
    f.item(QStringLiteral("c.txt"));
    PreviewResult r =
            f.evaluate({QStringLiteral("x.txt"), QStringLiteral("x.txt"), QStringLiteral("c.txt")});
    for (int i = 0; i < 2; ++i) {
        QCOMPARE(r.items[i].status, Status::Conflict);
        QCOMPARE(firstProblem(r.items[i]).code, DiagCode::DuplicateInBatch);
        QCOMPARE(firstProblem(r.items[i]).count, 1);
    }
    QCOMPARE(r.items[2].status, Status::Unchanged);
    QCOMPARE(r.summary.blocked(), 2);
    QCOMPARE(r.summary.conflicts, 2);

    r = f.evaluate({QStringLiteral("x.txt"), QStringLiteral("x.txt"), QStringLiteral("x.txt")});
    for (const ItemPreview &p : r.items)
        QCOMPARE(firstProblem(p).count, 2);
}

void TestPreviewEngine::existingConflict()
{
    Fixture f;
    f.disk(QStringLiteral("b.txt"));
    f.item(QStringLiteral("a.txt"));
    const PreviewResult r = f.evaluate({QStringLiteral("b.txt")});
    QCOMPARE(r.items[0].status, Status::Conflict);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::ExistsInFolder);
    QCOMPARE(firstProblem(r.items[0]).text, QStringLiteral("b.txt"));
}

void TestPreviewEngine::caseInsensitiveConflict()
{
    Fixture ins(volume(CaseSensitivity::Insensitive));
    ins.item(QStringLiteral("p.txt"));
    ins.item(QStringLiteral("q.txt"));
    PreviewResult r = ins.evaluate({QStringLiteral("A.txt"), QStringLiteral("a.txt")});
    QCOMPARE(r.items[0].status, Status::Conflict);
    QCOMPARE(r.items[1].status, Status::Conflict);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::DuplicateInBatch);

    Fixture sens;
    sens.item(QStringLiteral("p.txt"));
    sens.item(QStringLiteral("q.txt"));
    r = sens.evaluate({QStringLiteral("A.txt"), QStringLiteral("a.txt")});
    QCOMPARE(r.items[0].status, Status::Changed);
    QCOMPARE(r.items[1].status, Status::Changed);

    Fixture disk(volume(CaseSensitivity::Insensitive));
    disk.disk(QStringLiteral("B.txt"));
    disk.item(QStringLiteral("a.txt"));
    r = disk.evaluate({QStringLiteral("b.txt")});
    QCOMPARE(r.items[0].status, Status::Conflict);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::ExistsIgnoringCase);
    QCOMPARE(firstProblem(r.items[0]).text, QStringLiteral("B.txt"));
}

void TestPreviewEngine::unknownCaseIsInsensitive()
{
    Fixture f(volume(CaseSensitivity::Unknown));
    f.disk(QStringLiteral("B.txt"));
    f.item(QStringLiteral("a.txt"));
    const PreviewResult r = f.evaluate({QStringLiteral("b.txt")});
    QCOMPARE(r.items[0].status, Status::Conflict);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::ExistsIgnoringCase);
}

void TestPreviewEngine::nfdConflictMac()
{
    Fixture f(volume(CaseSensitivity::Insensitive, true));
    f.disk(cafeNfc());
    f.item(QStringLiteral("x.txt"));
    const PreviewResult r = f.evaluate({cafeNfd()});
    QCOMPARE(r.items[0].status, Status::Conflict);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::ExistsNormalization);
    QCOMPARE(firstProblem(r.items[0]).text, cafeNfc());

    // Without normalization-insensitivity the two names are different.
    Fixture g(volume(CaseSensitivity::Insensitive, false));
    g.disk(cafeNfc());
    g.item(QStringLiteral("x.txt"));
    QCOMPARE(g.evaluate({cafeNfd()}).items[0].status, Status::Changed);
}

void TestPreviewEngine::chainIsNotConflict()
{
    Fixture f;
    f.item(QStringLiteral("a"));
    f.item(QStringLiteral("b"));
    const PreviewResult r = f.evaluate({QStringLiteral("b"), QStringLiteral("c")});
    QCOMPARE(r.items[0].status, Status::Changed);
    QCOMPARE(r.items[1].status, Status::Changed);
}

void TestPreviewEngine::swapIsNotConflict()
{
    Fixture f(volume(CaseSensitivity::Insensitive));
    f.item(QStringLiteral("a"));
    f.item(QStringLiteral("b"));
    f.item(QStringLiteral("c"));
    const PreviewResult r =
            f.evaluate({QStringLiteral("B"), QStringLiteral("c"), QStringLiteral("a")});
    for (const ItemPreview &p : r.items)
        QCOMPARE(p.status, Status::Changed);
}

void TestPreviewEngine::excludedKeepsName()
{
    Fixture f;
    f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"), false);
    const PreviewResult r = f.evaluate({QStringLiteral("b.txt"), QStringLiteral("z.txt")});
    QCOMPARE(r.items[0].status, Status::Conflict);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::ExistsInFolder);
    QCOMPARE(r.items[1].status, Status::Unchanged);
    QCOMPARE(r.items[1].newName, QStringLiteral("b.txt"));
    QVERIFY(!r.items[1].included);
    QCOMPARE(r.summary.excluded, 1);
    QCOMPARE(r.summary.unchanged, 0);
}

void TestPreviewEngine::unchangedKeepsName()
{
    Fixture f;
    f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"));
    const PreviewResult r = f.evaluate({QStringLiteral("b.txt"), QStringLiteral("b.txt")});
    QCOMPARE(r.items[0].status, Status::Conflict);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::ExistsInFolder);
    QCOMPARE(r.items[1].status, Status::Unchanged);
}

void TestPreviewEngine::conflictCascade()
{
    Fixture f;
    f.disk(QStringLiteral("d"));
    f.item(QStringLiteral("b"));
    f.item(QStringLiteral("c"));
    f.item(QStringLiteral("a"));
    // c -> d is blocked by the disk; b -> c then is blocked because c keeps its name;
    // a -> b then is blocked too.
    const PreviewResult r =
            f.evaluate({QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("b")});
    QCOMPARE(r.items[1].status, Status::Conflict);
    QCOMPARE(firstProblem(r.items[1]).text, QStringLiteral("d"));
    QCOMPARE(r.items[0].status, Status::Conflict);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::ExistsInFolder);
    QCOMPARE(firstProblem(r.items[0]).text, QStringLiteral("c"));
    QCOMPARE(r.items[2].status, Status::Conflict);
    QCOMPARE(firstProblem(r.items[2]).text, QStringLiteral("b"));
}

void TestPreviewEngine::sameKeyTwiceOnDisk()
{
    // The volume probe said Unknown, but the folder holds "a" and "A" (really case-sensitive).
    // Renaming "a" frees only "a": "A" still blocks the key.
    Fixture f(volume(CaseSensitivity::Sensitive));
    f.disk(QStringLiteral("A"));
    f.item(QStringLiteral("a"));
    f.item(QStringLiteral("x"));
    f.in.folders.clear();
    f.refresh();
    f.in.folders[kFolder].volume.caseSensitivity = CaseSensitivity::Unknown;
    QList<ProposedName> names = {{QStringLiteral("b"), std::nullopt},
                                 {QStringLiteral("a"), std::nullopt}};
    const PreviewResult r = evaluateNames(f.in, names, NameValidator(OsRules::Linux, true));
    QCOMPARE(r.items[0].status, Status::Changed);
    QCOMPARE(r.items[1].status, Status::Conflict);
    QCOMPARE(firstProblem(r.items[1]).text, QStringLiteral("A"));
}

void TestPreviewEngine::invalidPortable()
{
    Fixture f;
    f.item(QStringLiteral("a-b.txt"));
    f.addRule(replaceName(QStringLiteral("-"), QStringLiteral(":")));
    PreviewResult r = f.preview();
    QCOMPARE(r.items[0].newName, QStringLiteral("a:b.txt"));
    QCOMPARE(r.items[0].status, Status::Invalid);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::ForbiddenChar);
    QCOMPARE(r.summary.errors, 1);

    f.in.chain.options.portableNames = false;
    r = f.preview();
    QCOMPARE(r.items[0].status, Status::Changed);
}

void TestPreviewEngine::invalidPath()
{
    Fixture f;
    f.item(QStringLiteral("a"));
    f.in.folders.clear();
    f.refresh();
    // A long folder path: the name is valid but the path is longer than PATH_MAX - 1.
    FolderInfo info = f.in.folders[kFolder];
    const QString longFolder = QStringLiteral("/") + QString(4090, QLatin1Char('f'));
    info.path = longFolder;
    f.in.folders.clear();
    f.in.folders.insert(longFolder, info);
    f.in.items[0].folder = longFolder;
    const PreviewResult r = evaluateNames(f.in, {{QStringLiteral("abcdefgh"), std::nullopt}},
                                          NameValidator(OsRules::Linux, false));
    QCOMPARE(r.items[0].status, Status::Invalid);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::PathTooLong);
}

void TestPreviewEngine::ruleErrorAllItems()
{
    Fixture f;
    f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"), false);
    f.addRule(insertPrefix(QStringLiteral("x")));
    f.addRule(replaceName(QStringLiteral("("), QStringLiteral("y"), ReplaceRule::Mode::Regex));
    const PreviewResult r = f.preview();
    QCOMPARE(r.items[0].status, Status::RuleError);
    QCOMPARE(r.items[0].newName, QStringLiteral("a.txt"));
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::RuleFailed);
    QCOMPARE(firstProblem(r.items[0]).ruleIndex, 1);
    QVERIFY(!firstProblem(r.items[0]).text.isEmpty());
    QVERIFY(firstProblem(r.items[0]).count >= 0);
    // An excluded item is not blocked.
    QCOMPARE(r.items[1].status, Status::Unchanged);
    QCOMPARE(r.summary.errors, 1);
}

void TestPreviewEngine::unsupportedBlocks()
{
    Fixture f;
    f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"));
    f.addRule(std::make_unique<UnsupportedRule>(
            QJsonObject{{QStringLiteral("type"), QStringLiteral("future")}}));
    const PreviewResult r = f.preview();
    for (const ItemPreview &p : r.items) {
        QCOMPARE(p.status, Status::RuleError);
        QCOMPARE(firstProblem(p).code, DiagCode::UnsupportedRule);
        QCOMPARE(firstProblem(p).text, QStringLiteral("future"));
        QCOMPARE(firstProblem(p).ruleIndex, 0);
    }
}

void TestPreviewEngine::disabledUnsupportedDoesNotBlock()
{
    Fixture f;
    f.item(QStringLiteral("a.txt"));
    auto rule = std::make_unique<UnsupportedRule>(
            QJsonObject{{QStringLiteral("type"), QStringLiteral("future")}});
    rule->enabled = false;
    f.addRule(std::move(rule));
    f.addRule(insertPrefix(QStringLiteral("x")));
    const PreviewResult r = f.preview();
    QCOMPARE(r.items[0].status, Status::Changed);
}

void TestPreviewEngine::missingItem()
{
    Fixture f;
    f.item(QStringLiteral("gone.txt"), true, false);
    f.item(QStringLiteral("A.txt"));
    // Exact match: "a.txt" is not the name on disk, even on a case-sensitive folder.
    f.in.items[1].name = QStringLiteral("a.txt");
    const PreviewResult r = f.evaluate({QStringLiteral("x.txt"), QStringLiteral("y.txt")});
    QCOMPARE(r.items[0].status, Status::Missing);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::NotOnDisk);
    QCOMPARE(r.items[1].status, Status::Missing);
    QCOMPARE(r.summary.errors, 2);
}

void TestPreviewEngine::unreadableFolderMissing()
{
    Fixture f;
    f.item(QStringLiteral("a.txt"));
    f.refresh();
    f.in.folders[kFolder].readable = false;
    PreviewResult r = evaluateNames(f.in, {{QStringLiteral("b.txt"), std::nullopt}},
                                    NameValidator(OsRules::Linux, true));
    QCOMPARE(r.items[0].status, Status::Missing);

    f.in.folders.clear();
    r = evaluateNames(f.in, {{QStringLiteral("b.txt"), std::nullopt}},
                      NameValidator(OsRules::Linux, true));
    QCOMPARE(r.items[0].status, Status::Missing);
}

void TestPreviewEngine::identityChangedMissing()
{
    Fixture f;
    const ItemId a = f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"));
    f.in.identityChanged.insert(a);
    const PreviewResult r = f.evaluate({QStringLiteral("a.txt"), QStringLiteral("c.txt")});
    QCOMPARE(r.items[0].status, Status::Missing);
    QCOMPARE(firstProblem(r.items[0]).code, DiagCode::IdentityChanged);
    QCOMPARE(r.items[1].status, Status::Changed);
}

void TestPreviewEngine::extensionWarning()
{
    Fixture f;
    f.item(QStringLiteral("photo.JPG"));
    f.item(QStringLiteral("photo.jpeg"));
    f.item(QStringLiteral("noext"));
    const QString dir = QStringLiteral("dir.v1");
    f.item(dir);
    f.in.items[3].isFolder = true;
    const PreviewResult r = f.evaluate({QStringLiteral("photo.jpg"), QStringLiteral("photo2.jpg"),
                                        QStringLiteral("noext.txt"), QStringLiteral("dir.v2")});
    QVERIFY(!hasCode(r.items[0].warnings, DiagCode::ExtensionChanged));
    QVERIFY(hasCode(r.items[1].warnings, DiagCode::ExtensionChanged));
    QVERIFY(hasCode(r.items[2].warnings, DiagCode::ExtensionChanged));
    // Folders never get the extension warning.
    QVERIFY(!hasCode(r.items[3].warnings, DiagCode::ExtensionChanged));
    QCOMPARE(r.summary.warnings, 2);
}

void TestPreviewEngine::hiddenWarning()
{
    Fixture f;
    f.item(QStringLiteral("a"));
    f.item(QStringLiteral(".b"));
    f.item(QStringLiteral(".c"));
    const PreviewResult r =
            f.evaluate({QStringLiteral(".a"), QStringLiteral("b"), QStringLiteral(".d")});
    QVERIFY(hasCode(r.items[0].warnings, DiagCode::BecomesHidden));
    QVERIFY(hasCode(r.items[1].warnings, DiagCode::BecomesVisible));
    QVERIFY(r.items[2].warnings.isEmpty());
}

void TestPreviewEngine::nameWarnings()
{
    Fixture f;
    f.item(QStringLiteral("a"));
    f.item(QStringLiteral("b"));
    f.in.chain.options.portableNames = false;
    const PreviewResult r = f.evaluate({QStringLiteral(" a"), QStringLiteral("b.")});
    QCOMPARE(r.items[0].status, Status::Changed);
    QVERIFY(hasCode(r.items[0].warnings, DiagCode::LeadingOrTrailingWhitespace));
    QVERIFY(hasCode(r.items[1].warnings, DiagCode::EndsWithSpaceOrDot));
    // Warnings are not shown for a blocked item in the count.
    Fixture g;
    g.disk(QStringLiteral(" z"));
    g.item(QStringLiteral("a"));
    const PreviewResult s = g.evaluate({QStringLiteral(" z")});
    QCOMPARE(s.items[0].status, Status::Conflict);
    QCOMPARE(s.summary.warnings, 0);
}

void TestPreviewEngine::autoResolve()
{
    Fixture f;
    f.in.chain.options.autoResolveDuplicates = true;
    f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"));
    f.item(QStringLiteral("c.txt"));
    const PreviewResult r =
            f.evaluate({QStringLiteral("x.txt"), QStringLiteral("x.txt"), QStringLiteral("x.txt")});
    QCOMPARE(r.items[0].newName, QStringLiteral("x.txt"));
    QCOMPARE(r.items[1].newName, QStringLiteral("x (2).txt"));
    QCOMPARE(r.items[2].newName, QStringLiteral("x (3).txt"));
    for (const ItemPreview &p : r.items)
        QCOMPARE(p.status, Status::Changed);
}

void TestPreviewEngine::autoResolveSkipsTaken()
{
    Fixture f;
    f.in.chain.options.autoResolveDuplicates = true;
    f.disk(QStringLiteral("x (2).txt"));
    f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"));
    f.item(QStringLiteral("c.txt"));
    // A fourth item takes "x (4).txt" itself.
    f.item(QStringLiteral("d.txt"));
    const PreviewResult r = f.evaluate({QStringLiteral("x.txt"), QStringLiteral("x.txt"),
                                        QStringLiteral("x.txt"), QStringLiteral("x (4).txt")});
    QCOMPARE(r.items[0].newName, QStringLiteral("x.txt"));
    QCOMPARE(r.items[1].newName, QStringLiteral("x (3).txt"));
    QCOMPARE(r.items[2].newName, QStringLiteral("x (5).txt"));
    QCOMPARE(r.items[3].newName, QStringLiteral("x (4).txt"));
    for (const ItemPreview &p : r.items)
        QCOMPARE(p.status, Status::Changed);
}

void TestPreviewEngine::autoResolveFirstTakenOnDisk()
{
    // AC11.9 second part.
    Fixture f;
    f.in.chain.options.autoResolveDuplicates = true;
    f.disk(QStringLiteral("x.txt"));
    f.disk(QStringLiteral("x (2).txt"));
    f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"));
    f.item(QStringLiteral("c.txt"));
    const PreviewResult r =
            f.evaluate({QStringLiteral("x.txt"), QStringLiteral("x.txt"), QStringLiteral("x.txt")});
    QCOMPARE(r.items[0].newName, QStringLiteral("x (3).txt"));
    QCOMPARE(r.items[1].newName, QStringLiteral("x (4).txt"));
    QCOMPARE(r.items[2].newName, QStringLiteral("x (5).txt"));
}

void TestPreviewEngine::autoResolveSingleItemVsDisk()
{
    Fixture f(volume(CaseSensitivity::Insensitive));
    f.in.chain.options.autoResolveDuplicates = true;
    f.disk(QStringLiteral("B.txt"));
    f.item(QStringLiteral("a.txt"));
    const PreviewResult r = f.evaluate({QStringLiteral("b.txt")});
    QCOMPARE(r.items[0].newName, QStringLiteral("b (2).txt"));
    QCOMPARE(r.items[0].status, Status::Changed);
}

void TestPreviewEngine::autoResolveNoExtension()
{
    Fixture f;
    f.in.chain.options.autoResolveDuplicates = true;
    f.item(QStringLiteral("a"));
    f.item(QStringLiteral("b"));
    f.item(QStringLiteral("c"));
    const QString dir = QStringLiteral("d");
    f.item(dir);
    f.in.items[3].isFolder = true;
    f.item(QStringLiteral("e"));
    f.in.items[4].isFolder = true;
    const PreviewResult r =
            f.evaluate({QStringLiteral("x"), QStringLiteral("x"), QStringLiteral(".x"),
                        QStringLiteral("p.v1"), QStringLiteral("p.v1")});
    QCOMPARE(r.items[1].newName, QStringLiteral("x (2)"));
    QCOMPARE(r.items[2].newName, QStringLiteral(".x"));
    // A folder has no extension (folder extensions off).
    QCOMPARE(r.items[4].newName, QStringLiteral("p.v1 (2)"));
}

void TestPreviewEngine::autoResolveTooLong()
{
    Fixture f;
    f.in.chain.options.autoResolveDuplicates = true;
    f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"));
    const QString longName = QString(251, QLatin1Char('n')) + QStringLiteral(".txt"); // 255
    const PreviewResult r = f.evaluate({longName, longName});
    QCOMPARE(r.items[0].status, Status::Changed);
    QCOMPARE(r.items[1].status, Status::Invalid);
    QCOMPARE(firstProblem(r.items[1]).code, DiagCode::NameTooLong);
    QCOMPARE(r.items[1].newName, QString(251, QLatin1Char('n')) + QStringLiteral(" (2).txt"));
}

void TestPreviewEngine::autoResolveOff()
{
    Fixture f;
    f.disk(QStringLiteral("b.txt"));
    f.item(QStringLiteral("a.txt"));
    QCOMPARE(f.evaluate({QStringLiteral("b.txt")}).items[0].status, Status::Conflict);
}

void TestPreviewEngine::numberingFollowsOrder()
{
    Fixture f;
    f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"), false);
    f.item(QStringLiteral("c.txt"));
    auto num = std::make_unique<NumberingRule>();
    num->position = NumberingRule::Position::Replace;
    f.addRule(std::move(num));
    PreviewResult r = f.preview();
    QCOMPARE(r.items[0].newName, QStringLiteral("1.txt"));
    QCOMPARE(r.items[1].newName, QStringLiteral("b.txt"));
    QCOMPARE(r.items[2].newName, QStringLiteral("2.txt"));

    f.in.items.move(2, 0);
    r = f.preview();
    QCOMPARE(r.items[0].newName, QStringLiteral("1.txt")); // c
    QCOMPARE(r.items[1].newName, QStringLiteral("2.txt")); // a
}

void TestPreviewEngine::skipBlockedCascade()
{
    // AC-B3: A is Invalid. B wants A's old name. C is fine.
    Fixture f;
    const ItemId a = f.item(QStringLiteral("a.txt"));
    const ItemId b = f.item(QStringLiteral("b.txt"));
    f.item(QStringLiteral("c.txt"));
    f.addRule(replaceName(QStringLiteral("a.txt"), QStringLiteral("a?.txt")));
    f.addRule(replaceName(QStringLiteral("b.txt"), QStringLiteral("a.txt")));
    f.addRule(replaceName(QStringLiteral("c.txt"), QStringLiteral("z.txt")));
    f.refresh();
    const PreviewResult plain = computePreview(f.in);
    QCOMPARE(plain.items[0].status, Status::Invalid);
    QCOMPARE(plain.items[1].status, Status::Conflict);

    const SkipResult s = previewSkippingBlocked(f.in);
    QCOMPARE(s.skipped, (QSet<ItemId>{a, b}));
    QCOMPARE(s.cascaded, (QSet<ItemId>{b}));
    QVERIFY(s.preview.items[0].skipped);
    QVERIFY(s.preview.items[1].skipped);
    QCOMPARE(s.preview.items[0].newName, QStringLiteral("a.txt"));
    QCOMPARE(s.preview.items[0].status, Status::Unchanged);
    QCOMPARE(s.preview.items[2].status, Status::Changed);
    QCOMPARE(s.preview.summary.blocked(), 0);
    QCOMPARE(s.preview.summary.skipped, 2);
    QCOMPARE(s.preview.summary.willChange, 1);
}

void TestPreviewEngine::skipCascadeChain()
{
    // c -> d is blocked by the disk; b -> c and a -> b are blocked because of it.
    Fixture f;
    f.disk(QStringLiteral("d"));
    const ItemId a = f.item(QStringLiteral("a"));
    const ItemId b = f.item(QStringLiteral("b"));
    const ItemId c = f.item(QStringLiteral("c"));
    f.addRule(replaceName(QStringLiteral("^c$"), QStringLiteral("d"), ReplaceRule::Mode::Regex));
    f.addRule(replaceName(QStringLiteral("^b$"), QStringLiteral("c"), ReplaceRule::Mode::Regex));
    f.addRule(replaceName(QStringLiteral("^a$"), QStringLiteral("b"), ReplaceRule::Mode::Regex));
    // The rules run in order on each name: c -> d; b -> c (rule 1 does not run again); a -> b.
    f.refresh();
    const SkipResult s = previewSkippingBlocked(f.in);
    QCOMPARE(s.skipped, (QSet<ItemId>{a, b, c}));
    QCOMPARE(s.cascaded, (QSet<ItemId>{a, b}));
}

void TestPreviewEngine::skipOwnConflictNotCascade()
{
    Fixture f;
    f.disk(QStringLiteral("d"));
    const ItemId c = f.item(QStringLiteral("c"));
    f.item(QStringLiteral("e"));
    f.addRule(replaceName(QStringLiteral("^c$"), QStringLiteral("d"), ReplaceRule::Mode::Regex));
    f.refresh();
    const SkipResult s = previewSkippingBlocked(f.in);
    QCOMPARE(s.skipped, (QSet<ItemId>{c}));
    QVERIFY(s.cascaded.isEmpty());
    QCOMPARE(s.preview.items[1].status, Status::Unchanged);
}

void TestPreviewEngine::skipKeepsNumbering()
{
    Fixture f;
    f.disk(QStringLiteral("p_001.txt"));
    const ItemId p = f.item(QStringLiteral("p.txt"));
    f.item(QStringLiteral("q.txt"));
    f.item(QStringLiteral("r.txt"));
    auto num = std::make_unique<NumberingRule>();
    num->padding = 3;
    f.addRule(std::move(num));
    f.refresh();
    const SkipResult s = previewSkippingBlocked(f.in);
    QCOMPARE(s.skipped, (QSet<ItemId>{p}));
    QCOMPARE(s.preview.items[0].newName, QStringLiteral("p.txt"));
    QCOMPARE(s.preview.items[1].newName, QStringLiteral("q_002.txt"));
    QCOMPARE(s.preview.items[2].newName, QStringLiteral("r_003.txt"));
    QVERIFY(s.cascaded.isEmpty());
}

void TestPreviewEngine::skipInputMarksSkipped()
{
    Fixture f;
    const ItemId a = f.item(QStringLiteral("a.txt"));
    f.item(QStringLiteral("b.txt"));
    f.in.skip.insert(a);
    const PreviewResult r = f.evaluate({QStringLiteral("x.txt"), QStringLiteral("a.txt")});
    QVERIFY(r.items[0].skipped);
    QVERIFY(r.items[0].included);
    QCOMPARE(r.items[0].newName, QStringLiteral("a.txt"));
    QCOMPARE(r.items[1].status, Status::Conflict);
    QCOMPARE(r.summary.skipped, 1);
    QCOMPARE(r.summary.unchanged, 0);
}

void TestPreviewEngine::summaryCounts()
{
    Fixture f;
    f.disk(QStringLiteral("taken"));
    f.item(QStringLiteral("unchanged"));
    f.item(QStringLiteral("changed"));
    f.item(QStringLiteral("Caseonly"));
    f.item(QStringLiteral("conflict"));
    f.item(QStringLiteral("invalid"));
    f.item(QStringLiteral("missing"), true, false);
    f.item(QStringLiteral("excluded"), false);
    const ItemId skipped = f.item(QStringLiteral("skipped"));
    f.item(QStringLiteral("warn"));
    f.in.skip.insert(skipped);
    const PreviewResult r = f.evaluate(
            {QStringLiteral("unchanged"), QStringLiteral("changed2"), QStringLiteral("caseonly"),
             QStringLiteral("taken"), QStringLiteral("in:valid"), QStringLiteral("m2"),
             QStringLiteral("e2"), QStringLiteral("s2"), QStringLiteral(".warn")});
    QCOMPARE(r.items[0].status, Status::Unchanged);
    QCOMPARE(r.items[1].status, Status::Changed);
    QCOMPARE(r.items[2].status, Status::CaseOnly);
    QCOMPARE(r.items[3].status, Status::Conflict);
    QCOMPARE(r.items[4].status, Status::Invalid);
    QCOMPARE(r.items[5].status, Status::Missing);
    QCOMPARE(r.items[8].status, Status::Changed);
    QCOMPARE(r.summary.willChange, 3);
    QCOMPARE(r.summary.conflicts, 1);
    QCOMPARE(r.summary.errors, 2);
    QCOMPARE(r.summary.warnings, 1);
    QCOMPARE(r.summary.excluded, 1);
    QCOMPARE(r.summary.unchanged, 1);
    QCOMPARE(r.summary.skipped, 1);
    QCOMPARE(r.summary.blocked(), 3);
}

void TestPreviewEngine::cancelStops()
{
    Fixture f;
    for (int i = 0; i < 1000; ++i)
        f.item(QStringLiteral("f%1").arg(i));
    f.addRule(insertPrefix(QStringLiteral("x")));
    f.refresh();
    std::atomic<bool> cancel{true};
    const PreviewResult r = computePreview(f.in, &cancel);
    QVERIFY(r.cancelled);
    std::atomic<bool> go{false};
    const PreviewResult ok = computePreview(f.in, &go);
    QVERIFY(!ok.cancelled);
    QCOMPARE(ok.items.size(), 1000);
}

void TestPreviewEngine::perf10k()
{
#ifndef NDEBUG
    QSKIP("performance test runs in release builds only");
#endif
    Fixture f(volume(CaseSensitivity::Insensitive));
    for (int i = 0; i < 10000; ++i)
        f.item(QStringLiteral("IMG_%1 photo.JPG").arg(i, 5, 10, QLatin1Char('0')));
    f.addRule(replaceName(QStringLiteral("IMG_"), QStringLiteral("Holiday ")));
    auto lower = std::make_unique<ChangeCaseRule>();
    lower->target = Target::Extension;
    f.addRule(std::move(lower));
    auto num = std::make_unique<NumberingRule>();
    num->position = NumberingRule::Position::Suffix;
    f.addRule(std::move(num));
    f.refresh();
    QElapsedTimer timer;
    timer.start();
    const PreviewResult r = computePreview(f.in);
    const qint64 ms = timer.elapsed();
    qInfo() << "perf10k:" << ms << "ms";
    QCOMPARE(r.summary.willChange, 10000);
    QVERIFY2(ms < 500, qPrintable(QStringLiteral("%1 ms").arg(ms)));
}

QTEST_GUILESS_MAIN(TestPreviewEngine)
#include "tst_previewengine.moc"
