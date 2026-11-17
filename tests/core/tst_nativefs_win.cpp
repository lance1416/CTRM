// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtGlobal>
#include <QtTest>

#if defined(Q_OS_WIN)

#include "fs/filesystem.h"
#include "temptree.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTimeZone>

#include <qt_windows.h>

using namespace ctrm;

namespace {

LPCWSTR wpath(const QString &path)
{
    return reinterpret_cast<LPCWSTR>(path.utf16());
}

QString nativeLong(const QString &path)
{
    return QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(path);
}

std::optional<DirEntry> findEntry(const QList<DirEntry> &entries, const QString &name)
{
    for (const DirEntry &e : entries) {
        if (e.name == name)
            return e;
    }
    return std::nullopt;
}

} // namespace

class TestNativeFsWin : public QObject
{
    Q_OBJECT
private slots:
    void renameNoReplaceRefusesExisting();
    void renameNoReplaceRefusesExistingFolder();
    void renameMovesName();
    void caseOnlyRenameDirect();
    void identityStrongNtfs();
    void identityWeak();
    void identityMissing();
    void volumeInfoNtfs();
    void listFolderEntries();
    void listFolderMissing();
    void listFolderMarksLoneSurrogate();
    void existsLstat();
    void renamesInLongPathFolder();
    void hiddenAttribute();
    void timestampsAndAttributesKept();
};

void TestNativeFsWin::renameNoReplaceRefusesExisting()
{
    TempTree t;
    const QString a = t.file(QStringLiteral("a.txt"), "A");
    const QString b = t.file(QStringLiteral("b.txt"), "B");
    const auto before = t.checksums();
    auto fs = createNativeFileSystem();
    const RenameResult r = fs->renameNoReplace(a, b);
    QVERIFY(!r.ok);
    QVERIFY(r.errorCode == ERROR_ALREADY_EXISTS || r.errorCode == ERROR_FILE_EXISTS);
    QVERIFY(!r.errorText.isEmpty());
    QVERIFY(r.fallback.isEmpty());
    QCOMPARE(t.checksums(), before);
}

void TestNativeFsWin::renameNoReplaceRefusesExistingFolder()
{
    TempTree t;
    const QString d = t.mkdir(QStringLiteral("d"));
    t.file(QStringLiteral("d/x.txt"), "X");
    const QString e = t.mkdir(QStringLiteral("e"));
    t.file(QStringLiteral("e/y.txt"), "Y");
    const auto before = t.checksums();
    auto fs = createNativeFileSystem();
    QVERIFY(!fs->renameNoReplace(d, e).ok);
    QVERIFY(!fs->renameNoReplace(t.root() + QStringLiteral("/d/x.txt"), e).ok);
    QCOMPARE(t.checksums(), before);
}

void TestNativeFsWin::renameMovesName()
{
    TempTree t;
    const QString a = t.file(QStringLiteral("a.txt"), "A");
    const QString c = t.root() + QStringLiteral("/c.txt");
    auto fs = createNativeFileSystem();
    const RenameResult r = fs->renameNoReplace(a, c);
    QVERIFY2(r.ok, qPrintable(r.errorText));
    QVERIFY(!QFileInfo::exists(a));
    QCOMPARE(TempTree::sha256(c), TempTree::sha256(t.file(QStringLiteral("ref"), "A")));

    const QString d = t.mkdir(QStringLiteral("d"));
    t.file(QStringLiteral("d/child.txt"), "C");
    const RenameResult rd = fs->renameNoReplace(d, t.root() + QStringLiteral("/e"));
    QVERIFY2(rd.ok, qPrintable(rd.errorText));
    QVERIFY(QFileInfo::exists(t.root() + QStringLiteral("/e/child.txt")));
}

void TestNativeFsWin::caseOnlyRenameDirect()
{
    // Records what MoveFileExW does with a case-only rename. The planner still uses two steps.
    TempTree t;
    const QString a = t.file(QStringLiteral("a.txt"), "A");
    auto fs = createNativeFileSystem();
    const RenameResult r = fs->renameNoReplace(a, t.root() + QStringLiteral("/A.txt"));
    const auto entries = fs->listFolder(t.root(), nullptr);
    QVERIFY(entries);
    QStringList names;
    for (const DirEntry &e : *entries)
        names << e.name;
    qInfo() << "case-only MoveFileExW ok:" << r.ok << "error:" << r.errorCode << "names:" << names;
    // No data loss in any case: exactly one entry with the content "A".
    QCOMPARE(entries->size(), 1);
    QCOMPARE(TempTree::sha256(t.root() + QLatin1Char('/') + names.first()),
             TempTree::sha256(t.file(QStringLiteral("ref/x"), "A")));
}

void TestNativeFsWin::identityStrongNtfs()
{
    TempTree t;
    auto fs = createNativeFileSystem();
    const VolumeInfo v = fs->volumeInfo(t.root());
    if (v.fsName.compare(QStringLiteral("NTFS"), Qt::CaseInsensitive) != 0 &&
        v.fsName.compare(QStringLiteral("ReFS"), Qt::CaseInsensitive) != 0)
        QSKIP("temporary folder is not on NTFS or ReFS");
    QCOMPARE(v.identityType, IdentityType::Strong);

    const QString a = t.file(QStringLiteral("a.txt"), "A");
    const QString b = t.file(QStringLiteral("b.txt"), "B");
    const auto idA = fs->identity(a, IdentityType::Strong);
    const auto idB = fs->identity(b, IdentityType::Strong);
    QVERIFY(idA && idB);
    QCOMPARE(idA->type, IdentityType::Strong);
    QVERIFY(idA->idLow != 0 || idA->idHigh != 0);
    QVERIFY(!sameFile(*idA, *idB));

    const QString c = t.root() + QStringLiteral("/c.txt");
    QVERIFY(fs->renameNoReplace(a, c).ok);
    const auto idC = fs->identity(c, IdentityType::Strong);
    QVERIFY(idC);
    QCOMPARE(*idC, *idA);
    QVERIFY(sameFile(*idA, *idC));
}

void TestNativeFsWin::identityWeak()
{
    TempTree t;
    auto fs = createNativeFileSystem();
    const QString a = t.file(QStringLiteral("a.txt"), "12345");
    const auto id = fs->identity(a, IdentityType::Weak);
    QVERIFY(id);
    QCOMPARE(id->type, IdentityType::Weak);
    QCOMPARE(id->size, qint64(5));
    const qint64 expectedMs = QFileInfo(a).lastModified().toMSecsSinceEpoch();
    QCOMPARE(id->mtimeNs / 1000000, expectedMs);

    const auto folderId = fs->identity(t.mkdir(QStringLiteral("d")), IdentityType::Weak);
    QVERIFY(folderId);
    QCOMPARE(folderId->size, qint64(-1));
    QCOMPARE(folderId->mtimeNs, qint64(0)); // a folder: type only
}

void TestNativeFsWin::identityMissing()
{
    TempTree t;
    auto fs = createNativeFileSystem();
    QVERIFY(!fs->identity(t.root() + QStringLiteral("/none"), IdentityType::Strong));
    QVERIFY(!fs->identity(t.root() + QStringLiteral("/none"), IdentityType::Weak));
}

void TestNativeFsWin::volumeInfoNtfs()
{
    TempTree t;
    auto fs = createNativeFileSystem();
    const VolumeInfo v = fs->volumeInfo(t.root());
    qInfo() << "temp volume:" << v.fsName;
    QVERIFY(!v.fsName.isEmpty());
    QVERIFY(!v.normalizationInsensitive);
    if (v.fsName.compare(QStringLiteral("NTFS"), Qt::CaseInsensitive) == 0) {
        // A normal NTFS folder is case-insensitive.
        QCOMPARE(v.caseSensitivity, CaseSensitivity::Insensitive);
        QCOMPARE(v.identityType, IdentityType::Strong);
    }
    // A drive root also works (no trailing separator in the input).
    const QString drive = t.root().left(2);
    QVERIFY(drive.endsWith(QLatin1Char(':')));
    QCOMPARE(fs->volumeInfo(drive).fsName, v.fsName);
}

void TestNativeFsWin::listFolderEntries()
{
    TempTree t;
    t.file(QStringLiteral("a.txt"), "A");
    t.mkdir(QStringLiteral("sub"));
    auto fs = createNativeFileSystem();
    QString error;
    const auto entries = fs->listFolder(t.root(), &error);
    QVERIFY2(entries, qPrintable(error));
    QCOMPARE(entries->size(), 2);
    const auto file = findEntry(*entries, QStringLiteral("a.txt"));
    const auto sub = findEntry(*entries, QStringLiteral("sub"));
    QVERIFY(file && sub);
    QVERIFY(!file->isFolder);
    QVERIFY(sub->isFolder);
    QVERIFY(!file->isSymlink);
    QVERIFY(file->nameSupported);

    const auto empty = fs->listFolder(t.root() + QStringLiteral("/sub"), &error);
    QVERIFY(empty);
    QVERIFY(empty->isEmpty());
}

void TestNativeFsWin::listFolderMissing()
{
    TempTree t;
    auto fs = createNativeFileSystem();
    QString error;
    QVERIFY(!fs->listFolder(t.root() + QStringLiteral("/none"), &error));
    QVERIFY(!error.isEmpty());
}

void TestNativeFsWin::listFolderMarksLoneSurrogate()
{
    // NTFS permits a name with an unpaired surrogate. It does not round-trip through UTF-8
    // (journal, presets), so the entry is not supported.
    TempTree t;
    const QString name = QStringLiteral("a") + QChar(0xD800) + QStringLiteral(".txt");
    const QString path = nativeLong(t.root() + QLatin1Char('/') + name);
    HANDLE h = CreateFileW(wpath(path), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        QSKIP("the file system refuses an unpaired surrogate");
    CloseHandle(h);
    auto fs = createNativeFileSystem();
    const auto entries = fs->listFolder(t.root(), nullptr);
    QVERIFY(entries);
    const auto e = findEntry(*entries, name);
    QVERIFY(e);
    QVERIFY(!e->nameSupported);
    DeleteFileW(wpath(path));
}

void TestNativeFsWin::existsLstat()
{
    TempTree t;
    auto fs = createNativeFileSystem();
    QVERIFY(fs->exists(t.file(QStringLiteral("a.txt"), "A")));
    QVERIFY(fs->exists(t.mkdir(QStringLiteral("d"))));
    QVERIFY(!fs->exists(t.root() + QStringLiteral("/none")));
    QVERIFY(!fs->exists(t.root() + QStringLiteral("/none/deeper")));
}

void TestNativeFsWin::renamesInLongPathFolder()
{
    TempTree t;
    QString rel;
    while (t.root().size() + rel.size() < 300)
        rel += QStringLiteral("/") + QString(40, QLatin1Char('d'));
    rel.remove(0, 1);
    const QString folder = t.root() + QLatin1Char('/') + rel;
    // Qt adds the long path prefix itself; make the folder through Win32 to be independent.
    QString built = t.root();
    for (const QString &part : rel.split(QLatin1Char('/'))) {
        built += QLatin1Char('/') + part;
        QVERIFY(CreateDirectoryW(wpath(nativeLong(built)), nullptr));
    }
    QVERIFY(folder.size() > 260);
    const QString from = folder + QStringLiteral("/a.txt");
    HANDLE h = CreateFileW(wpath(nativeLong(from)), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(h != INVALID_HANDLE_VALUE);
    CloseHandle(h);

    auto fs = createNativeFileSystem();
    QVERIFY(fs->exists(from));
    QVERIFY(fs->identity(from, IdentityType::Weak));
    const auto entries = fs->listFolder(folder, nullptr);
    QVERIFY(entries);
    QCOMPARE(entries->size(), 1);
    const RenameResult r = fs->renameNoReplace(from, folder + QStringLiteral("/b.txt"));
    QVERIFY2(r.ok, qPrintable(r.errorText));
    QVERIFY(!fs->exists(from));
    QVERIFY(fs->exists(folder + QStringLiteral("/b.txt")));
}

void TestNativeFsWin::hiddenAttribute()
{
    TempTree t;
    const QString hidden = t.file(QStringLiteral("h.txt"), "H");
    t.file(QStringLiteral("v.txt"), "V");
    QVERIFY(SetFileAttributesW(wpath(QDir::toNativeSeparators(hidden)), FILE_ATTRIBUTE_HIDDEN));
    auto fs = createNativeFileSystem();
    const auto entries = fs->listFolder(t.root(), nullptr);
    QVERIFY(entries);
    const auto h = findEntry(*entries, QStringLiteral("h.txt"));
    const auto v = findEntry(*entries, QStringLiteral("v.txt"));
    QVERIFY(h && v);
    QVERIFY(h->hiddenAttribute);
    QVERIFY(!v->hiddenAttribute);
}

void TestNativeFsWin::timestampsAndAttributesKept()
{
    TempTree t;
    const QString a = t.file(QStringLiteral("a.txt"), "A");
    {
        QFile f(a);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime(QDate(2020, 1, 2), QTime(3, 4, 5), QTimeZone::UTC),
                              QFileDevice::FileModificationTime));
    }
    const QString b = t.root() + QStringLiteral("/b.txt");
    // Clear the read-only attribute at the end, also when a check fails, so that the
    // temporary folder can be removed.
    const auto clearAttributes = qScopeGuard([&] {
        SetFileAttributesW(wpath(QDir::toNativeSeparators(a)), FILE_ATTRIBUTE_NORMAL);
        SetFileAttributesW(wpath(QDir::toNativeSeparators(b)), FILE_ATTRIBUTE_NORMAL);
    });
    const DWORD attrs = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE;
    QVERIFY(SetFileAttributesW(wpath(QDir::toNativeSeparators(a)), attrs));
    const QDateTime mtime = QFileInfo(a).lastModified();
    const auto before = createNativeFileSystem()->identity(a, IdentityType::Weak);
    QVERIFY(before);

    auto fs = createNativeFileSystem();
    const RenameResult r = fs->renameNoReplace(a, b);
    QVERIFY2(r.ok, qPrintable(r.errorText));
    QCOMPARE(QFileInfo(b).lastModified(), mtime);
    QCOMPARE(GetFileAttributesW(wpath(QDir::toNativeSeparators(b))), attrs);
    const auto after = fs->identity(b, IdentityType::Weak);
    QVERIFY(after);
    QCOMPARE(after->mtimeNs, before->mtimeNs);
    QCOMPARE(after->size, before->size);
}

#else

class TestNativeFsWin : public QObject
{
    Q_OBJECT
private slots:
    void windowsOnly() { QSKIP("Windows only"); }
};

#endif

QTEST_GUILESS_MAIN(TestNativeFsWin)
#include "tst_nativefs_win.moc"
