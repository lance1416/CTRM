// SPDX-License-Identifier: GPL-3.0-or-later
#include "fakefilesystem.h"
#include "fs/filesystem.h"
#include "fs/folderinfo.h"
#include "fs/itemsource.h"
#include "fsblock.h"
#include "temptree.h"

#include <QDir>
#include <QFile>
#include <QtTest>

using namespace ctrm;

namespace {

VolumeInfo volume(CaseSensitivity cs, bool nfd = false, IdentityType id = IdentityType::Strong)
{
    return VolumeInfo{cs, nfd, id, QStringLiteral("fakefs")};
}

QStringList itemNames(const QList<Item> &items)
{
    QStringList names;
    for (const Item &i : items)
        names.append(i.name);
    return names;
}

} // namespace

class TestItemSource : public QObject
{
    Q_OBJECT
private slots:
    void nameKeyInsensitive();
    void nameKeyUnknownIsInsensitive();
    void nameKeyNfd();
    void naturalOrder();
    void folderChildrenDirectOnly();
    void folderChildrenHidden();
    void folderChildrenAppleDouble();
    void folderChildrenNatural();
    void folderChildrenSymlink();
    void folderChildrenUnsupportedNames();
    void makeItemSetsIdentity();
    void makeItemErrors();
    void makeItemUsesOnDiskSpelling();
    void canonicalItemPathResolvesParentOnly();
    void fakeRenameNoReplace();
    void fakeCaseOnlyOnInsensitive();
    void fakeCaseOnlyOnSensitive();
    void fakeLookupFollowsVolumeCase();
    void fakeRenameKeepsIdentity();
    void fakeRenameFolderMovesContents();
    void fakeRenameOtherFolderFails();
    void fakeRenameSourceMissing();
    void fakeFailRenameAndHook();
    void readFolderInfoNames();
    void fakeTopFoldersDifferOnlyInCase();
};

void TestItemSource::nameKeyInsensitive()
{
    const VolumeInfo ins = volume(CaseSensitivity::Insensitive);
    const VolumeInfo sen = volume(CaseSensitivity::Sensitive);
    QCOMPARE(nameKey("A.TXT", ins), nameKey("a.txt", ins));
    QVERIFY(nameKey("A.TXT", sen) != nameKey("a.txt", sen));
    QCOMPARE(nameKey(QStringLiteral("Ärger"), ins), nameKey(QStringLiteral("äRGER"), ins));
}

void TestItemSource::nameKeyUnknownIsInsensitive()
{
    const VolumeInfo unk = volume(CaseSensitivity::Unknown);
    QCOMPARE(nameKey("Photo.JPG", unk), nameKey("photo.jpg", unk));
}

void TestItemSource::nameKeyNfd()
{
    const QString nfc = QStringLiteral("Café");
    const QString nfd = QStringLiteral("Café");
    QCOMPARE(nameKey(nfc, volume(CaseSensitivity::Sensitive, true)),
             nameKey(nfd, volume(CaseSensitivity::Sensitive, true)));
    QCOMPARE(nameKey(nfc, volume(CaseSensitivity::Insensitive, true)),
             nameKey(QStringLiteral("CAFÉ"), volume(CaseSensitivity::Insensitive, true)));
    // Without normalization insensitivity, the forms are different names.
    QVERIFY(nameKey(nfc, volume(CaseSensitivity::Sensitive)) !=
            nameKey(nfd, volume(CaseSensitivity::Sensitive)));
}

void TestItemSource::naturalOrder()
{
    QVERIFY(naturalLess("file2", "file10"));
    QVERIFY(!naturalLess("file10", "file2"));
    QVERIFY(naturalLess("a", "B"));
    QVERIFY(!naturalLess("B", "a"));
    QVERIFY(naturalLess("file2", "file02") != naturalLess("file02", "file2")); // total order
    QVERIFY(!naturalLess("same", "same"));
    QVERIFY(naturalLess("x9y", "x10"));
    QVERIFY(naturalLess("img", "img1"));
}

void TestItemSource::folderChildrenDirectOnly()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a.txt");
    fs.addFolder("/f/sub");
    fs.addFile("/f/sub/b.txt");

    const FolderChildren c = folderChildren(fs, "/f", false);
    QCOMPARE(itemNames(c.items), QStringList({"a.txt", "sub"}));
    QCOMPARE(c.items[0].folder, QStringLiteral("/f"));
    QVERIFY(!c.items[0].isFolder);
    QVERIFY(c.items[1].isFolder);
    QCOMPARE(c.items[0].id, ItemId(0));
    QVERIFY(c.unsupportedNames.isEmpty());
}

void TestItemSource::folderChildrenHidden()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/.h");
    fs.addFile("/f/attr.txt");
    fs.setHiddenAttribute("/f/attr.txt", true);
    fs.addFile("/f/v.txt");

    QCOMPARE(itemNames(folderChildren(fs, "/f", false).items), QStringList({"v.txt"}));
    QCOMPARE(itemNames(folderChildren(fs, "/f", true).items),
             QStringList({".h", "attr.txt", "v.txt"}));
}

void TestItemSource::folderChildrenAppleDouble()
{
    // macOS keeps extended attributes of "X" in "._X" on some volumes (exFAT, FAT, SMB) and moves
    // "._X" with "X". On macOS, "._X" is never an item when "X" is in the same folder.
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a.txt");
    fs.addFile("/f/._a.txt");
    fs.addFile("/f/.h");
    fs.addFile("/f/._.h");
    fs.addFile("/f/._gone.txt"); // no "gone.txt": not a sidecar of an item
    fs.addFile("/f/._A.TXT");    // not the exact name of "a.txt"

    QStringList hidden = itemNames(folderChildren(fs, "/f", true).items);
    hidden.sort();
#ifdef Q_OS_MACOS
    QCOMPARE(hidden, QStringList({"._A.TXT", "._gone.txt", ".h", "a.txt"}));
#else
    QCOMPARE(hidden, QStringList({"._.h", "._A.TXT", "._a.txt", "._gone.txt", ".h", "a.txt"}));
#endif
    QCOMPARE(itemNames(folderChildren(fs, "/f", false).items), QStringList({"a.txt"}));
    // The conflict check still sees "._a.txt" as a name on disk.
    QVERIFY(readFolderInfo(fs, "/f").names.contains(QStringLiteral("._a.txt")));
}

void TestItemSource::folderChildrenNatural()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/file10");
    fs.addFile("/f/file2");
    fs.addFile("/f/File1");
    QCOMPARE(itemNames(folderChildren(fs, "/f", false).items),
             QStringList({"File1", "file2", "file10"}));
}

void TestItemSource::folderChildrenSymlink()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addSymlink("/f/link");
    const FolderChildren c = folderChildren(fs, "/f", false);
    QCOMPARE(c.items.size(), 1);
    QCOMPARE(c.items[0].name, QStringLiteral("link"));
    QVERIFY(!c.items[0].isFolder);
}

void TestItemSource::folderChildrenUnsupportedNames()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/ok.txt");
    fs.addFile("/f/bad.txt");
    fs.setNameSupported("/f/bad.txt", false);
    const FolderChildren c = folderChildren(fs, "/f", true);
    QCOMPARE(itemNames(c.items), QStringList({"ok.txt"}));
    QCOMPARE(c.unsupportedNames, QStringList({"bad.txt"}));

    QString error;
    QVERIFY(!makeItem(fs, "/f/bad.txt", &error));
    QVERIFY(!error.isEmpty());
}

void TestItemSource::makeItemSetsIdentity()
{
    FakeFileSystem fs;
    fs.addFolder("/s", volume(CaseSensitivity::Sensitive, false, IdentityType::Strong));
    fs.addFile("/s/a.txt", "abc", 7);
    fs.addFolder("/w", volume(CaseSensitivity::Insensitive, false, IdentityType::Weak));
    fs.addFile("/w/b.txt", "abcd", 9);

    QString error;
    const auto strong = makeItem(fs, "/s/a.txt", &error);
    QVERIFY2(strong, qPrintable(error));
    QCOMPARE(strong->identity.type, fs.volumeInfo("/s").identityType);
    QCOMPARE(strong->identity, *fs.identity("/s/a.txt", IdentityType::Strong));
    QCOMPARE(strong->folder, QStringLiteral("/s"));
    QCOMPARE(strong->name, QStringLiteral("a.txt"));
    QCOMPARE(strong->id, ItemId(0));
    QVERIFY(strong->included);

    const auto weak = makeItem(fs, "/w/b.txt", &error);
    QVERIFY2(weak, qPrintable(error));
    QCOMPARE(weak->identity.type, IdentityType::Weak);
    QCOMPARE(weak->identity.size, qint64(4));
    QCOMPARE(weak->identity.mtimeNs, qint64(9));

    fs.addFolder("/s/sub");
    const auto folder = makeItem(fs, "/s/sub", &error);
    QVERIFY(folder);
    QVERIFY(folder->isFolder);
}

void TestItemSource::makeItemErrors()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    QString error;
    QVERIFY(!makeItem(fs, "/f/missing.txt", &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!makeItem(fs, "/nofolder/a.txt", &error));
    QVERIFY(!error.isEmpty());
}

void TestItemSource::makeItemUsesOnDiskSpelling()
{
    FakeFileSystem fs;
    fs.addFolder("/f", volume(CaseSensitivity::Insensitive));
    fs.addFile("/f/Photo.JPG");
    QString error;
    const auto item = makeItem(fs, "/f/photo.jpg", &error);
    QVERIFY2(item, qPrintable(error));
    QCOMPARE(item->name, QStringLiteral("Photo.JPG"));
}

void TestItemSource::canonicalItemPathResolvesParentOnly()
{
    TempTree t;
    t.file("real/a.txt", "A");
    const QString link = t.root() + "/linked";
    // A junction on Windows: QFileInfo::canonicalFilePath does not resolve it.
    QVERIFY(makeFolderLink(t.root() + "/real", link));
    QCOMPARE(canonicalItemPath(link + "/a.txt"), t.root() + "/real/a.txt");
    QCOMPARE(canonicalItemPath(t.root() + "/real/../real/a.txt"), t.root() + "/real/a.txt");
    // The item link itself is not resolved.
    QCOMPARE(canonicalItemPath(link), t.root() + "/linked");
    // Parent that does not exist: cleaned path.
    QCOMPARE(canonicalItemPath("/no/such/../dir/x.txt"), QStringLiteral("/no/dir/x.txt"));
}

void TestItemSource::fakeRenameNoReplace()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a.txt", "A");
    fs.addFile("/f/b.txt", "B");
    const RenameResult r = fs.renameNoReplace("/f/a.txt", "/f/b.txt");
    QVERIFY(!r.ok);
    QCOMPARE(r.errorCode, ErrorCode::TargetExists);
    QCOMPARE(fs.content("/f/a.txt"), QByteArray("A"));
    QCOMPARE(fs.content("/f/b.txt"), QByteArray("B"));
    QCOMPARE(fs.names("/f"), QStringList({"a.txt", "b.txt"}));

    QVERIFY(fs.renameNoReplace("/f/a.txt", "/f/c.txt").ok);
    QCOMPARE(fs.names("/f"), QStringList({"b.txt", "c.txt"}));
    QCOMPARE(fs.content("/f/c.txt"), QByteArray("A"));
    QCOMPARE(fs.renameCount(), 2);
}

void TestItemSource::fakeCaseOnlyOnInsensitive()
{
    FakeFileSystem fs;
    fs.addFolder("/f", volume(CaseSensitivity::Insensitive));
    fs.addFile("/f/a.txt");
    const RenameResult r = fs.renameNoReplace("/f/a.txt", "/f/A.txt");
    QVERIFY(!r.ok);
    QCOMPARE(r.errorCode, ErrorCode::TargetExists);
    QCOMPARE(fs.names("/f"), QStringList({"a.txt"}));
    // Two steps work.
    QVERIFY(fs.renameNoReplace("/f/a.txt", "/f/.tmp").ok);
    QVERIFY(fs.renameNoReplace("/f/.tmp", "/f/A.txt").ok);
    QCOMPARE(fs.names("/f"), QStringList({"A.txt"}));

    // Unknown is treated as Insensitive.
    fs.addFolder("/u", volume(CaseSensitivity::Unknown));
    fs.addFile("/u/x");
    QCOMPARE(fs.renameNoReplace("/u/x", "/u/X").errorCode, ErrorCode::TargetExists);

    // Normalization-insensitive volume: NFC and NFD forms are one name.
    fs.addFolder("/n", volume(CaseSensitivity::Sensitive, true));
    fs.addFile(QStringLiteral("/n/Café"));
    QCOMPARE(fs.renameNoReplace(QStringLiteral("/n/Café"), QStringLiteral("/n/Café")).errorCode,
             ErrorCode::TargetExists);
}

void TestItemSource::fakeCaseOnlyOnSensitive()
{
    FakeFileSystem fs;
    fs.addFolder("/f", volume(CaseSensitivity::Sensitive));
    fs.addFile("/f/a.txt");
    QVERIFY(fs.renameNoReplace("/f/a.txt", "/f/A.txt").ok);
    QCOMPARE(fs.names("/f"), QStringList({"A.txt"}));
    // Both spellings can exist side by side.
    fs.addFile("/f/a.txt");
    QCOMPARE(fs.names("/f"), QStringList({"A.txt", "a.txt"}));
}

void TestItemSource::fakeLookupFollowsVolumeCase()
{
    FakeFileSystem fs;
    fs.addFolder("/i", volume(CaseSensitivity::Insensitive));
    fs.addFile("/i/a.txt");
    fs.addFolder("/s", volume(CaseSensitivity::Sensitive));
    fs.addFile("/s/a.txt");
    QVERIFY(fs.exists("/i/A.TXT"));
    QVERIFY(fs.identity("/i/A.TXT", IdentityType::Strong));
    QVERIFY(!fs.exists("/s/A.TXT"));
    QVERIFY(!fs.identity("/s/A.TXT", IdentityType::Strong));
    QVERIFY(fs.exists("/s/a.txt"));
    // The folder name itself follows the case rule of the parent volume.
    fs.addFolder("/i/Sub", volume(CaseSensitivity::Insensitive));
    fs.addFile("/i/Sub/b");
    QVERIFY(fs.exists("/i/sub/B"));
    QVERIFY(fs.listFolder("/i/SUB", nullptr));
}

void TestItemSource::fakeRenameKeepsIdentity()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a.txt", "xyz", 42);
    fs.addFile("/f/b.txt", "xyz", 42);
    const auto strongBefore = fs.identity("/f/a.txt", IdentityType::Strong);
    const auto weakBefore = fs.identity("/f/a.txt", IdentityType::Weak);
    QVERIFY(strongBefore && weakBefore);
    QVERIFY(!sameFile(*strongBefore, *fs.identity("/f/b.txt", IdentityType::Strong)));
    QVERIFY(fs.renameNoReplace("/f/a.txt", "/f/c.txt").ok);
    QCOMPARE(*fs.identity("/f/c.txt", IdentityType::Strong), *strongBefore);
    QCOMPARE(*fs.identity("/f/c.txt", IdentityType::Weak), *weakBefore);
    QVERIFY(!fs.exists("/f/a.txt"));
}

void TestItemSource::fakeRenameFolderMovesContents()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFolder("/f/old");
    fs.addFile("/f/old/x.txt", "X");
    fs.addFolder("/f/old/deep");
    fs.addFile("/f/old/deep/y.txt", "Y");
    QVERIFY(fs.renameNoReplace("/f/old", "/f/new").ok);
    QCOMPARE(fs.names("/f"), QStringList({"new"}));
    QCOMPARE(fs.content("/f/new/x.txt"), QByteArray("X"));
    QCOMPARE(fs.content("/f/new/deep/y.txt"), QByteArray("Y"));
    QVERIFY(!fs.listFolder("/f/old", nullptr));
    QVERIFY(!fs.exists("/f/old/x.txt"));
}

void TestItemSource::fakeRenameOtherFolderFails()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFolder("/g");
    fs.addFile("/f/a.txt");
    const RenameResult r = fs.renameNoReplace("/f/a.txt", "/g/a.txt");
    QVERIFY(!r.ok);
    QVERIFY(r.errorCode != 0);
    QCOMPARE(fs.names("/f"), QStringList({"a.txt"}));
    QVERIFY(fs.names("/g").isEmpty());
}

void TestItemSource::fakeRenameSourceMissing()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    const RenameResult r = fs.renameNoReplace("/f/a.txt", "/f/b.txt");
    QVERIFY(!r.ok);
    QCOMPARE(r.errorCode, ErrorCode::SourceMissing);
}

void TestItemSource::fakeFailRenameAndHook()
{
    FakeFileSystem fs;
    fs.addFolder("/f");
    fs.addFile("/f/a.txt");
    QStringList calls;
    fs.onBeforeRename(
            [&calls](const QString &from, const QString &to) { calls.append(from + ">" + to); });
    fs.failRename("/f/a.txt", 13, "Permission denied");
    const RenameResult r = fs.renameNoReplace("/f/a.txt", "/f/b.txt");
    QVERIFY(!r.ok);
    QCOMPARE(r.errorCode, 13);
    QCOMPARE(r.errorText, QStringLiteral("Permission denied"));
    QCOMPARE(fs.names("/f"), QStringList({"a.txt"}));
    // The failure is used once.
    QVERIFY(fs.renameNoReplace("/f/a.txt", "/f/b.txt").ok);
    QCOMPARE(calls, QStringList({"/f/a.txt>/f/b.txt", "/f/a.txt>/f/b.txt"}));
    QCOMPARE(fs.renameCount(), 2);

    // The hook can change the file system before the rename (source removed).
    fs.onBeforeRename([&fs](const QString &from, const QString &) { fs.remove(from); });
    QCOMPARE(fs.renameNoReplace("/f/b.txt", "/f/c.txt").errorCode, ErrorCode::SourceMissing);
}

void TestItemSource::readFolderInfoNames()
{
    FakeFileSystem fs;
    const VolumeInfo v = volume(CaseSensitivity::Insensitive, true, IdentityType::Weak);
    fs.addFolder("/f", v);
    fs.addFile("/f/B.txt");
    fs.addFile("/f/a.txt");
    fs.addFolder("/f/sub");
    const FolderInfo info = readFolderInfo(fs, "/f");
    QVERIFY(info.readable);
    QCOMPARE(info.path, QStringLiteral("/f"));
    QCOMPARE(info.volume.caseSensitivity, CaseSensitivity::Insensitive);
    QVERIFY(info.volume.normalizationInsensitive);
    QStringList names = info.names;
    names.sort();
    QCOMPARE(names, QStringList({"B.txt", "a.txt", "sub"}));

    const FolderInfo missing = readFolderInfo(fs, "/nope");
    QVERIFY(!missing.readable);
    QVERIFY(missing.names.isEmpty());
}

void TestItemSource::fakeTopFoldersDifferOnlyInCase()
{
    FakeFileSystem fs;
    fs.addFolder("/a", volume(CaseSensitivity::Insensitive));
    fs.addFolder("/A", volume(CaseSensitivity::Sensitive));
    QCOMPARE(fs.names("/"), QStringList({"A", "a"}));
    QCOMPARE(fs.volumeInfo("/a").caseSensitivity, CaseSensitivity::Insensitive);
    QCOMPARE(fs.volumeInfo("/A").caseSensitivity, CaseSensitivity::Sensitive);
}

QTEST_GUILESS_MAIN(TestItemSource)
#include "tst_itemsource.moc"
