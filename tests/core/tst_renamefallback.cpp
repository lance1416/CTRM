// SPDX-License-Identifier: GPL-3.0-or-later
//
// The POSIX rename fallback, called directly. It runs when the file system does not support the
// exclusive rename (exFAT and FAT on macOS, some Linux file systems).

#include "fs/filesystem.h"
#include "temptree.h"

#include <QFile>
#include <QtTest>

#ifndef Q_OS_WIN
#include "fs/nativefilesystem_posix.h"

#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace ctrm;

class TestRenameFallback : public QObject
{
    Q_OBJECT
private slots:
#ifdef Q_OS_WIN
    void posixOnly();
#else
    void fileMovesName();
    void folderMovesName();
    void existingFileTargetRefused();
    void existingFolderTargetRefused();
    void symlinkStaysSymlink();
    void sourceMissing();
#endif
};

#ifdef Q_OS_WIN

void TestRenameFallback::posixOnly()
{
    QSKIP("POSIX only");
}

#else

namespace {

QByteArray enc(const QString &path)
{
    return QFile::encodeName(path);
}

QByteArray contentOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<missing>");
}

} // namespace

void TestRenameFallback::fileMovesName()
{
    TempTree tree;
    const QString from = tree.file(QStringLiteral("a.txt"), "a");
    const QString to = tree.root() + QStringLiteral("/b.txt");
    const RenameResult r = posixRenameFallback(enc(from), enc(to));
    QVERIFY2(r.ok, qPrintable(r.errorText));
    QVERIFY(!QFile::exists(from));
    QCOMPARE(contentOf(to), QByteArray("a"));
    // Hard links work on the temporary folder (APFS, ext4). exFAT, where link() fails and the
    // file goes through lstat-rename, is covered by scripts/test-volumes.sh.
    QCOMPARE(r.fallback, QStringLiteral("link-unlink"));
}

void TestRenameFallback::folderMovesName()
{
    TempTree tree;
    const QString from = tree.mkdir(QStringLiteral("dir"));
    tree.file(QStringLiteral("dir/inside.txt"), "i");
    const QString to = tree.root() + QStringLiteral("/dir2");
    const RenameResult r = posixRenameFallback(enc(from), enc(to));
    QVERIFY2(r.ok, qPrintable(r.errorText));
    QCOMPARE(r.fallback, QStringLiteral("lstat-rename"));
    QVERIFY(!QFile::exists(from));
    QCOMPARE(contentOf(to + QStringLiteral("/inside.txt")), QByteArray("i"));
}

void TestRenameFallback::existingFileTargetRefused()
{
    TempTree tree;
    const QString from = tree.file(QStringLiteral("a.txt"), "a");
    const QString to = tree.file(QStringLiteral("b.txt"), "b");
    const RenameResult r = posixRenameFallback(enc(from), enc(to));
    QVERIFY(!r.ok);
    QCOMPARE(r.errorCode, EEXIST);
    QVERIFY(!r.fallback.isEmpty());
    QCOMPARE(contentOf(from), QByteArray("a"));
    QCOMPARE(contentOf(to), QByteArray("b"));
}

void TestRenameFallback::existingFolderTargetRefused()
{
    TempTree tree;
    const QString from = tree.mkdir(QStringLiteral("dir"));
    tree.file(QStringLiteral("dir/inside.txt"), "i");
    const QString to = tree.mkdir(QStringLiteral("other"));
    const RenameResult r = posixRenameFallback(enc(from), enc(to));
    QVERIFY(!r.ok);
    QCOMPARE(r.errorCode, EEXIST);
    QCOMPARE(r.fallback, QStringLiteral("lstat-rename"));
    QCOMPARE(contentOf(from + QStringLiteral("/inside.txt")), QByteArray("i"));
    QVERIFY(QFileInfo(to).isDir());
}

void TestRenameFallback::symlinkStaysSymlink()
{
    TempTree tree;
    const QString target = tree.file(QStringLiteral("target.txt"), "t");
    const QString link = tree.root() + QStringLiteral("/link");
    QVERIFY(::symlink("target.txt", enc(link).constData()) == 0);
    const QString to = tree.root() + QStringLiteral("/link2");
    const RenameResult r = posixRenameFallback(enc(link), enc(to));
    QVERIFY2(r.ok, qPrintable(r.errorText));
    struct stat st;
    QVERIFY(::lstat(enc(link).constData(), &st) != 0);
    QVERIFY(::lstat(enc(to).constData(), &st) == 0);
    QVERIFY(S_ISLNK(st.st_mode));
    QCOMPARE(QFile::symLinkTarget(to), target);
    QCOMPARE(contentOf(target), QByteArray("t"));
}

void TestRenameFallback::sourceMissing()
{
    TempTree tree;
    const RenameResult r = posixRenameFallback(enc(tree.root() + QStringLiteral("/none")),
                                               enc(tree.root() + QStringLiteral("/b")));
    QVERIFY(!r.ok);
    QCOMPARE(r.errorCode, ENOENT);
}

#endif

QTEST_GUILESS_MAIN(TestRenameFallback)
#include "tst_renamefallback.moc"
