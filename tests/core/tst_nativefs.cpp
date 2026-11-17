// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/filesystem.h"
#include "fs/itemsource.h"
#include "temptree.h"

#include <QFile>
#include <QtTest>

#include <cerrno>

#ifndef Q_OS_WIN
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace ctrm;

class TestNativeFs : public QObject
{
    Q_OBJECT
private slots:
#ifdef Q_OS_WIN
    void posixOnly();
#else
    void renameNoReplaceRefusesExisting();
    void renameMovesName();
    void renameFolder();
    void renameOtherFolderRefused();
    void renameSourceMissing();
    void renameSymlinkNotTarget();
    void identityStrongOnTmp();
    void identityWeakFolderSize();
    void listFolderHidden();
    void listFolderMissing();
    void addFolderSkipsNonUtf8Names();
    void canonicalPathResolvesLinkedParent();
    void caseSensitivityDetected();
    void timestampsAndModeKept();
#endif
};

#ifdef Q_OS_WIN

void TestNativeFs::posixOnly()
{
    QSKIP("POSIX only");
}

#else

namespace {

QByteArray enc(const QString &path)
{
    return QFile::encodeName(path);
}

struct stat lstatOf(const QString &path)
{
    struct stat st = {};
    lstat(enc(path).constData(), &st);
    return st;
}

} // namespace

void TestNativeFs::renameNoReplaceRefusesExisting()
{
    TempTree t;
    const QString a = t.file("a.txt", "A");
    const QString b = t.file("b.txt", "B");
    const auto before = t.checksums();
    auto fs = createNativeFileSystem();
    const RenameResult r = fs->renameNoReplace(a, b);
    QVERIFY(!r.ok);
    QVERIFY(r.errorCode != 0);
    QVERIFY(!r.errorText.isEmpty());
    QCOMPARE(t.checksums(), before);
}

void TestNativeFs::renameMovesName()
{
    TempTree t;
    const QString a = t.file("a.txt", "A");
    auto fs = createNativeFileSystem();
    const RenameResult r = fs->renameNoReplace(a, t.root() + "/c.txt");
    QVERIFY2(r.ok, qPrintable(r.errorText));
    QVERIFY(!fs->exists(a));
    QFile c(t.root() + "/c.txt");
    QVERIFY(c.open(QIODevice::ReadOnly));
    QCOMPARE(c.readAll(), QByteArray("A"));
}

void TestNativeFs::renameFolder()
{
    TempTree t;
    t.file("d/x.txt", "X");
    auto fs = createNativeFileSystem();
    const RenameResult r = fs->renameNoReplace(t.root() + "/d", t.root() + "/e");
    QVERIFY2(r.ok, qPrintable(r.errorText));
    QVERIFY(!fs->exists(t.root() + "/d"));
    QCOMPARE(TempTree::sha256(t.root() + "/e/x.txt"), TempTree::sha256(t.file("y.txt", "X")));

    // A folder never replaces an existing folder.
    t.mkdir("f");
    QVERIFY(!fs->renameNoReplace(t.root() + "/e", t.root() + "/f").ok);
    QVERIFY(fs->exists(t.root() + "/e/x.txt"));
}

void TestNativeFs::renameOtherFolderRefused()
{
    TempTree t;
    const QString a = t.file("a.txt", "A");
    t.mkdir("sub");
    auto fs = createNativeFileSystem();
    QVERIFY(!fs->renameNoReplace(a, t.root() + "/sub/a.txt").ok);
    QVERIFY(fs->exists(a));
}

void TestNativeFs::renameSourceMissing()
{
    TempTree t;
    auto fs = createNativeFileSystem();
    const RenameResult r = fs->renameNoReplace(t.root() + "/none", t.root() + "/other");
    QVERIFY(!r.ok);
    QCOMPARE(r.errorCode, ENOENT);
}

void TestNativeFs::renameSymlinkNotTarget()
{
    TempTree t;
    const QString target = t.file("t.txt", "T");
    const QString l = t.root() + "/l";
    QCOMPARE(symlink("t.txt", enc(l).constData()), 0);
    auto fs = createNativeFileSystem();
    const RenameResult r = fs->renameNoReplace(l, t.root() + "/m");
    QVERIFY2(r.ok, qPrintable(r.errorText));
    QVERIFY(fs->exists(target));
    QCOMPARE(TempTree::sha256(target), TempTree::sha256(t.file("cmp.txt", "T")));
    QVERIFY(S_ISLNK(lstatOf(t.root() + "/m").st_mode));
    QCOMPARE(QFile::symLinkTarget(t.root() + "/m"), target);

    // A dangling link exists (lstat semantics) and is listed as a link, not a folder.
    QCOMPARE(symlink("nowhere", enc(t.root() + "/dangling").constData()), 0);
    QVERIFY(fs->exists(t.root() + "/dangling"));
    QString error;
    const auto entries = fs->listFolder(t.root(), &error);
    QVERIFY2(entries, qPrintable(error));
    bool found = false;
    for (const DirEntry &e : *entries) {
        if (e.name == u"dangling") {
            found = true;
            QVERIFY(e.isSymlink);
            QVERIFY(!e.isFolder);
        }
    }
    QVERIFY(found);
}

void TestNativeFs::identityStrongOnTmp()
{
    TempTree t;
    const QString a = t.file("a.txt", "A");
    auto fs = createNativeFileSystem();
    const VolumeInfo v = fs->volumeInfo(t.root());
    QVERIFY(!v.fsName.isEmpty());
#ifdef Q_OS_LINUX
    if (v.fsName != u"ext4" && v.fsName != u"btrfs" && v.fsName != u"xfs")
        QSKIP(qPrintable(QStringLiteral("temporary folder is on %1").arg(v.fsName)));
#elif defined(Q_OS_MACOS)
    // exFAT, FAT and some network volumes have no stable file IDs (weak identity).
    if (v.fsName != u"apfs" && v.fsName != u"hfs")
        QSKIP(qPrintable(QStringLiteral("temporary folder is on %1, which has no strong file "
                                        "identity")
                                 .arg(v.fsName)));
#endif
    QCOMPARE(v.identityType, IdentityType::Strong);
    const auto before = fs->identity(a, IdentityType::Strong);
    QVERIFY(before);
    QCOMPARE(before->type, IdentityType::Strong);
    QVERIFY(before->idLow != 0);
    QVERIFY(fs->renameNoReplace(a, t.root() + "/b.txt").ok);
    const auto after = fs->identity(t.root() + "/b.txt", IdentityType::Strong);
    QVERIFY(after);
    QVERIFY(sameFile(*before, *after));
    const auto other = fs->identity(t.file("c.txt", "A"), IdentityType::Strong);
    QVERIFY(!sameFile(*before, *other));
    QVERIFY(!fs->identity(a, IdentityType::Strong));
}

void TestNativeFs::identityWeakFolderSize()
{
    TempTree t;
    const QString d = t.mkdir("d");
    const QString f = t.file("f.txt", "12345");
    auto fs = createNativeFileSystem();
    const auto folder = fs->identity(d, IdentityType::Weak);
    QVERIFY(folder);
    QCOMPARE(folder->type, IdentityType::Weak);
    QCOMPARE(folder->size, qint64(-1));
    QCOMPARE(folder->mtimeNs, qint64(0)); // a folder: type only
    const auto file = fs->identity(f, IdentityType::Weak);
    QVERIFY(file);
    QCOMPARE(file->size, qint64(5));
    QVERIFY(file->mtimeNs > 0);
}

void TestNativeFs::listFolderHidden()
{
    TempTree t;
    t.file(".h", "");
    const QString flagged = t.file("flagged.txt", "");
    t.mkdir("sub");
#ifdef Q_OS_MACOS
    QCOMPARE(chflags(enc(flagged).constData(), UF_HIDDEN), 0);
    // Some volumes (exFAT, FAT) report the hidden flag for each name with a leading dot. The OS
    // gives this flag, not CTRM, so check the OS first.
    if (lstatOf(t.root() + "/.h").st_flags & UF_HIDDEN)
        QSKIP("the volume marks each name with a leading dot as hidden (for example exFAT or "
              "FAT)");
    if (!(lstatOf(flagged).st_flags & UF_HIDDEN))
        QSKIP("the volume does not keep the hidden flag");
#endif
    auto fs = createNativeFileSystem();
    QString error;
    const auto entries = fs->listFolder(t.root(), &error);
    QVERIFY2(entries, qPrintable(error));
    QStringList names;
    for (const DirEntry &e : *entries) {
        names.append(e.name);
        QVERIFY(e.nameSupported);
        if (e.name == u"sub")
            QVERIFY(e.isFolder);
        if (e.name == u".h")
            QVERIFY(!e.hiddenAttribute);
#ifdef Q_OS_MACOS
        if (e.name == u"flagged.txt")
            QVERIFY(e.hiddenAttribute);
#endif
    }
    names.sort();
    QCOMPARE(names, QStringList({".h", "flagged.txt", "sub"}));
#ifdef Q_OS_MACOS
    QCOMPARE(folderChildren(*fs, t.root(), false).items.size(), 1);
#endif
}

void TestNativeFs::listFolderMissing()
{
    TempTree t;
    auto fs = createNativeFileSystem();
    QString error;
    QVERIFY(!fs->listFolder(t.root() + "/none", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!fs->exists(t.root() + "/none"));
}

void TestNativeFs::addFolderSkipsNonUtf8Names()
{
#ifndef Q_OS_LINUX
    QSKIP("Linux only: other systems do not store names that are not valid UTF-8");
#else
    TempTree t;
    t.file("ok.txt", "");
    const QByteArray bad = enc(t.root()) + "/\xff.txt";
    const int fd = ::open(bad.constData(), O_CREAT | O_WRONLY, 0644);
    if (fd < 0)
        QSKIP("The file system refuses names that are not valid UTF-8");
    ::close(fd);
    auto fs = createNativeFileSystem();
    const FolderChildren c = folderChildren(*fs, t.root(), true);
    QCOMPARE(c.items.size(), 1);
    QCOMPARE(c.items[0].name, QStringLiteral("ok.txt"));
    QCOMPARE(c.unsupportedNames.size(), 1);
#endif
}

void TestNativeFs::canonicalPathResolvesLinkedParent()
{
    TempTree t;
    const QString dir = t.mkdir("dir");
    t.file("dir/a", "A");
    const QString dirL = t.root() + "/dirL";
    QCOMPARE(symlink(enc(dir).constData(), enc(dirL).constData()), 0);
    QCOMPARE(canonicalItemPath(dirL + "/a"), canonicalItemPath(dir + "/a"));
    QCOMPARE(canonicalItemPath(dir + "/a"), dir + "/a");
}

void TestNativeFs::caseSensitivityDetected()
{
    TempTree t;
    t.file("Probe.txt", "");
    auto fs = createNativeFileSystem();
    const VolumeInfo v = fs->volumeInfo(t.root());
#ifdef Q_OS_MACOS
    if (v.fsName != u"apfs")
        QSKIP("temporary folder is not on APFS");
    // The default APFS volume is case-insensitive. A case-sensitive APFS volume is possible.
    if (v.caseSensitivity == CaseSensitivity::Sensitive)
        QSKIP("temporary folder is on a case-sensitive APFS volume");
    QCOMPARE(v.caseSensitivity, CaseSensitivity::Insensitive);
    QVERIFY(v.normalizationInsensitive);
#else
    if (v.fsName != u"ext4" && v.fsName != u"btrfs" && v.fsName != u"xfs" && v.fsName != u"tmpfs")
        QSKIP(qPrintable(QStringLiteral("temporary folder is on %1").arg(v.fsName)));
    QCOMPARE(v.caseSensitivity, CaseSensitivity::Sensitive);
    QVERIFY(!v.normalizationInsensitive);
#endif
    // Agrees with the real behaviour: does the other case find the same file?
    const bool found = fs->exists(t.root() + "/PROBE.TXT");
    QCOMPARE(found, v.caseSensitivity == CaseSensitivity::Insensitive);
}

void TestNativeFs::timestampsAndModeKept()
{
    TempTree t;
    const QString a = t.file("a.txt", "A");
    QCOMPARE(chmod(enc(a).constData(), 0640), 0);
    // exFAT and FAT have no file mode: chmod has no effect there.
    const bool modeKept = (lstatOf(a).st_mode & 0777) == 0640;
    const struct timespec times[2] = {{1000000000, 123456789}, {1100000000, 987654321}};
    QCOMPARE(utimensat(AT_FDCWD, enc(a).constData(), times, 0), 0);
    const struct stat before = lstatOf(a);
    auto fs = createNativeFileSystem();
    const QString b = t.root() + "/b.txt";
    QVERIFY(fs->renameNoReplace(a, b).ok);
    const struct stat after = lstatOf(b);
    QCOMPARE(after.st_mode, before.st_mode);
#ifdef Q_OS_MACOS
    QCOMPARE(after.st_mtimespec.tv_sec, before.st_mtimespec.tv_sec);
    QCOMPARE(after.st_mtimespec.tv_nsec, before.st_mtimespec.tv_nsec);
#else
    QCOMPARE(after.st_mtim.tv_sec, before.st_mtim.tv_sec);
    QCOMPARE(after.st_mtim.tv_nsec, before.st_mtim.tv_nsec);
#endif
    QCOMPARE(TempTree::sha256(b), TempTree::sha256(t.file("c.txt", "A")));
    if (!modeKept)
        QSKIP("the volume does not keep the file mode (for example exFAT or FAT)");
    QCOMPARE(after.st_mode & 0777, mode_t(0640));
}

#endif

QTEST_GUILESS_MAIN(TestNativeFs)
#include "tst_nativefs.moc"
