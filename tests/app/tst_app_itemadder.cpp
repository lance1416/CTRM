// SPDX-License-Identifier: GPL-3.0-or-later
#include "controllers/itemadder.h"
#include "dialogs/folderdropdialog.h"
#include "fs/filesystem.h"
#include "fsblock.h"
#include "models/itemtablemodel.h"
#include "settings.h"
#include "temptree.h"

#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <optional>

using namespace ctrm;
using namespace ctrm::app;

namespace {

// One ItemAdder with a real file system, a new model and temporary settings.
struct Fixture
{
    Fixture()
        : fs(createNativeFileSystem())
        , qsettings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat)
        , settings(&qsettings)
        , adder(*fs, model, settings, nullptr)
    {
        adder.askFolderDrop = [this](const FolderDropQuestion &q) {
            questions.append(q);
            return reply;
        };
    }

    QStringList names() const
    {
        QStringList result;
        for (const Item &item : model.items())
            result.append(item.name);
        return result;
    }

    QTemporaryDir dir;
    std::unique_ptr<FileSystem> fs;
    ItemTableModel model;
    QSettings qsettings;
    Settings settings;
    ItemAdder adder;
    FolderDropReply reply{FolderDropReply::Choice::AddContents, false, false};
    QList<FolderDropQuestion> questions;
};

} // namespace

class TestItemAdder : public QObject
{
    Q_OBJECT
private slots:
    void addFilesKeepsOrder();
    void thousandFilesFast();
    void folderAskRenameItself();
    void folderAskAddContents();
    void hiddenNotAdded();
    void droppedHiddenFileAdded();
    void droppedAppleDoubleNotAdded();
    void duplicateMessage();
    void dontAskAgainSaved();
    void cancelAddsNothingFromFolders();
    void dialogTextsAndReply();
    void symlinkToFolderIsOneItem();
    void bundleIsOneItem();
    void trailingSlashAndRepeats();
    void unreadableFolderMessage();
};

void TestItemAdder::addFilesKeepsOrder()
{
    TempTree t;
    const QString c = t.file(QStringLiteral("c.txt"), "c");
    const QString a = t.file(QStringLiteral("a.txt"), "a");
    const QString b = t.file(QStringLiteral("b.txt"), "b");
    Fixture f;
    f.adder.addPaths({c, a, b});
    QCOMPARE(f.names(), (QStringList{"c.txt", "a.txt", "b.txt"}));
    QVERIFY(f.questions.isEmpty());
}

void TestItemAdder::thousandFilesFast()
{
    TempTree t;
    QStringList paths;
    for (int i = 0; i < 1000; ++i)
        paths.append(t.file(QStringLiteral("f%1.txt").arg(i), "x"));
    Fixture f;
    QElapsedTimer clock;
    clock.start();
    f.adder.addPaths(paths);
    const qint64 ms = clock.elapsed();
    QCOMPARE(f.model.rowCount(), 1000);
    QVERIFY2(ms < 2000, qPrintable(QStringLiteral("%1 ms").arg(ms)));
}

void TestItemAdder::folderAskRenameItself()
{
    TempTree t;
    const QString folder = t.mkdir(QStringLiteral("photos"));
    t.file(QStringLiteral("photos/1.jpg"), "1");
    Fixture f;
    f.reply.choice = FolderDropReply::Choice::RenameFolder;
    f.adder.addPaths({folder});
    QCOMPARE(f.questions.size(), 1);
    QCOMPARE(f.questions[0].folders, QStringList{"photos"});
    QCOMPARE(f.names(), QStringList{"photos"});
    QVERIFY(f.model.items()[0].isFolder);
    QCOMPARE(f.settings.folderDropAnswer(), FolderDropAnswer::Ask);
}

void TestItemAdder::folderAskAddContents()
{
    TempTree t;
    const QString one = t.mkdir(QStringLiteral("one"));
    const QString two = t.mkdir(QStringLiteral("two"));
    t.file(QStringLiteral("one/f10.txt"), "x");
    t.file(QStringLiteral("one/f2.txt"), "x");
    t.file(QStringLiteral("one/sub/deep.txt"), "x");
    t.file(QStringLiteral("two/z.txt"), "x");
    Fixture f;
    f.adder.addPaths({one, two});
    QCOMPARE(f.questions.size(), 1);
    QCOMPARE(f.questions[0].folders, (QStringList{"one", "two"}));
    QCOMPARE(f.names(), (QStringList{"f2.txt", "f10.txt", "sub", "z.txt"}));
}

void TestItemAdder::hiddenNotAdded()
{
    TempTree t;
    const QString folder = t.mkdir(QStringLiteral("d"));
    t.file(QStringLiteral("d/.h"), "x");
    t.file(QStringLiteral("d/v"), "x");
    {
        Fixture f;
        f.adder.addPaths({folder});
        QCOMPARE(f.names(), QStringList{"v"});
    }
    {
        Fixture f;
        f.reply.includeHidden = true;
        f.adder.addPaths({folder});
        QCOMPARE(f.names(), (QStringList{".h", "v"}));
    }
    {
        Fixture f;
        f.settings.setIncludeHidden(true);
        f.adder.addPaths({folder});
        QCOMPARE(f.names(), (QStringList{".h", "v"}));
    }
}

void TestItemAdder::droppedHiddenFileAdded()
{
    TempTree t;
    const QString hidden = t.file(QStringLiteral(".h"), "x");
    Fixture f;
    f.adder.addPaths({hidden});
    QCOMPARE(f.names(), QStringList{".h"});
}

void TestItemAdder::droppedAppleDoubleNotAdded()
{
    // On macOS, "._X" next to "X" is never an item, also when the user drops or picks it
    // (PRD 6.6). "._gone.txt" has no main file, so it is a normal hidden file.
    TempTree t;
    t.file(QStringLiteral("a.txt"), "a");
    t.file(QStringLiteral("b.txt"), "b");
    const QString sideA = t.file(QStringLiteral("._a.txt"), "x");
    const QString sideB = t.file(QStringLiteral("._b.txt"), "x");
    const QString orphan = t.file(QStringLiteral("._gone.txt"), "x");
    {
        Fixture f;
        QSignalSpy spy(&f.adder, &ItemAdder::statusMessage);
        f.adder.addPaths({sideA, orphan});
#ifdef Q_OS_MACOS
        QCOMPARE(f.names(), QStringList{"._gone.txt"});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(),
                 QStringLiteral("1 item was not added. It is a macOS data file. macOS renames it "
                                "together with its main file."));
#else
        QCOMPARE(f.names(), (QStringList{"._a.txt", "._gone.txt"}));
        QCOMPARE(spy.count(), 0);
#endif
    }
#ifdef Q_OS_MACOS
    {
        Fixture f;
        QSignalSpy spy(&f.adder, &ItemAdder::statusMessage);
        f.adder.addPaths({sideA, sideB});
        QVERIFY(f.names().isEmpty());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(),
                 QStringLiteral("2 items were not added. They are macOS data files. macOS "
                                "renames them together with their main files."));
    }
#endif
}

void TestItemAdder::duplicateMessage()
{
    TempTree t;
    const QString a = t.file(QStringLiteral("a.txt"), "a");
    Fixture f;
    QSignalSpy spy(&f.adder, &ItemAdder::statusMessage);
    f.adder.addPaths({a});
    QCOMPARE(spy.count(), 0);
    f.adder.addPaths({a});
    QCOMPARE(f.model.rowCount(), 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("1 item was already in the list."));
}

void TestItemAdder::dontAskAgainSaved()
{
    TempTree t;
    const QString folder = t.mkdir(QStringLiteral("d"));
    t.file(QStringLiteral("d/x"), "x");
    Fixture f;
    f.reply.dontAskAgain = true;
    f.adder.addPaths({folder});
    QCOMPARE(f.settings.folderDropAnswer(), FolderDropAnswer::AddContents);
    QCOMPARE(f.names(), QStringList{"x"});
    f.model.clear();
    f.adder.addPaths({folder});
    QCOMPARE(f.questions.size(), 1);
    QCOMPARE(f.names(), QStringList{"x"});
}

void TestItemAdder::cancelAddsNothingFromFolders()
{
    TempTree t;
    const QString folder = t.mkdir(QStringLiteral("d"));
    t.file(QStringLiteral("d/inside"), "x");
    const QString file = t.file(QStringLiteral("loose.txt"), "x");
    Fixture f;
    f.reply.choice = FolderDropReply::Choice::Cancel;
    f.reply.dontAskAgain = true;
    f.adder.addPaths({folder, file});
    QCOMPARE(f.questions.size(), 1);
    QCOMPARE(f.names(), QStringList{"loose.txt"});
    QCOMPARE(f.settings.folderDropAnswer(), FolderDropAnswer::Ask);
}

namespace {

QPushButton *buttonWithText(QWidget *w, const QString &text)
{
    for (QPushButton *b : w->findChildren<QPushButton *>()) {
        if (b->text() == text)
            return b;
    }
    return nullptr;
}

bool hasLabel(QWidget *w, const QString &text)
{
    for (QLabel *l : w->findChildren<QLabel *>()) {
        if (l->text() == text)
            return true;
    }
    return false;
}

} // namespace

void TestItemAdder::dialogTextsAndReply()
{
    {
        FolderDropDialog d(FolderDropQuestion{{QStringLiteral("photos")}}, nullptr);
        QVERIFY(hasLabel(&d, QStringLiteral("You added the folder \"photos\".")));
        QVERIFY(hasLabel(&d, QStringLiteral("What do you want to rename?")));
        QPushButton *contents = buttonWithText(&d, QStringLiteral("Add Its Contents"));
        QVERIFY(contents);
        QVERIFY(contents->isDefault());
        QVERIFY(buttonWithText(&d, QStringLiteral("Rename the Folder Itself")));
        d.findChildren<QCheckBox *>().at(0)->setChecked(true); // Include hidden items
        d.open();
        contents->click();
        const FolderDropReply r = d.reply();
        QCOMPARE(r.choice, FolderDropReply::Choice::AddContents);
        QVERIFY(r.includeHidden);
        QVERIFY(!r.dontAskAgain);
    }
    {
        FolderDropDialog d(FolderDropQuestion{{QStringLiteral("a"), QStringLiteral("b")}}, nullptr);
        QVERIFY(hasLabel(&d, QStringLiteral("You added 2 folders.")));
        d.findChildren<QCheckBox *>().at(1)->setChecked(true); // Do not ask again
        d.open();
        buttonWithText(&d, QStringLiteral("Rename the Folder Itself"))->click();
        QCOMPARE(d.reply().choice, FolderDropReply::Choice::RenameFolder);
        QVERIFY(d.reply().dontAskAgain);
    }
    {
        FolderDropDialog d(FolderDropQuestion{{QStringLiteral("a")}}, nullptr);
        d.open();
        d.reject();
        QCOMPARE(d.reply().choice, FolderDropReply::Choice::Cancel);
    }
}

void TestItemAdder::symlinkToFolderIsOneItem()
{
    TempTree t;
    const QString target = t.mkdir(QStringLiteral("target"));
    t.file(QStringLiteral("target/inside"), "x");
    const QString link = t.root() + QStringLiteral("/link");
    QVERIFY(QFile::link(target, link));
    Fixture f;
    f.adder.addPaths({link});
    QVERIFY(f.questions.isEmpty());
    QCOMPARE(f.names(), QStringList{"link"});
    QVERIFY(!f.model.items()[0].isFolder);
}

void TestItemAdder::bundleIsOneItem()
{
#ifndef Q_OS_MACOS
    QSKIP("bundles exist on macOS only");
#else
    TempTree t;
    const QString bundle = t.mkdir(QStringLiteral("Tool.app"));
    t.file(QStringLiteral("Tool.app/Contents/Info.plist"), "<plist/>");
    if (!QFileInfo(bundle).isBundle())
        QSKIP("the system does not see the folder as a bundle");
    const QString file = t.file(QStringLiteral("a.txt"), "x");
    Fixture f;
    f.reply.choice = FolderDropReply::Choice::Cancel;
    f.adder.addPaths({bundle, file});
    QVERIFY(f.questions.isEmpty());
    QCOMPARE(f.names(), (QStringList{"Tool.app", "a.txt"}));
#endif
}

void TestItemAdder::trailingSlashAndRepeats()
{
    TempTree t;
    const QString folder = t.mkdir(QStringLiteral("d"));
    const QString a = t.file(QStringLiteral("a.txt"), "x");
    const QString b = t.file(QStringLiteral("b.txt"), "x");
    Fixture f;
    f.reply.choice = FolderDropReply::Choice::RenameFolder;
    QSignalSpy spy(&f.adder, &ItemAdder::statusMessage);
    f.adder.addPaths({folder + QLatin1Char('/'), a, a});
    QCOMPARE(f.names(), (QStringList{"d", "a.txt"}));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("1 item was already in the list."));
    f.adder.addPaths({a, b, folder, t.root() + QStringLiteral("/missing")});
    QCOMPARE(f.names(), (QStringList{"d", "a.txt", "b.txt"}));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toString(),
             QStringLiteral("2 items were already in the list. 1 item was not added. It cannot "
                            "be found or read."));
}

void TestItemAdder::unreadableFolderMessage()
{
    TempTree t;
    const QString folder = t.mkdir(QStringLiteral("locked"));
    t.file(QStringLiteral("locked/x"), "x");
    std::optional<UnlistableFolder> lock(std::in_place, folder);
    QVERIFY(lock->isActive());
    if (QDir(folder).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).contains("x"))
        QSKIP("the folder is readable without permission (root)");
    Fixture f;
    QSignalSpy spy(&f.adder, &ItemAdder::statusMessage);
    f.adder.addPaths({folder});
    lock.reset();
    QCOMPARE(f.model.rowCount(), 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(),
             QStringLiteral("1 item was not added. It cannot be found or read."));
}

QTEST_MAIN(TestItemAdder)
#include "tst_app_itemadder.moc"
