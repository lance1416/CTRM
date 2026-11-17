// SPDX-License-Identifier: GPL-3.0-or-later
//
// PRD section 10 acceptance tests that need the full app. The other criteria have their tests in
// the task files:
//   AC1.3        tests/core/tst_itemsource.cpp folderChildrenHidden;
//                tests/app/tst_app_itemadder.cpp hiddenNotAdded
//   AC1.5        tests/core/tst_nativefs.cpp renameSymlinkNotTarget;
//                tests/core/tst_safety.cpp symlinkRenamedNotTarget
//   AC2.1 core   tests/core/tst_previewengine.cpp perf10k;
//                tests/app/tst_app_preview.cpp uiNotBlocked
//   AC2.2        tests/app/tst_app_mainwindow.cpp blockingReasonTooltip;
//                tests/app/tst_app_itemmodel.cpp statusTexts
//   AC2.3        tests/app/tst_app_mainwindow.cpp filterChangedOnly;
//                tests/app/tst_app_itemmodel.cpp filterChanged
//   AC2.4 core   tests/core/tst_safety.cpp previewEqualsDisk
//   AC3.1, AC3.2 tests/core/tst_rulechain.cpp orderMatters, disabledRuleHasNoEffect
//   AC3.3        tests/app/tst_app_mainwindow.cpp moveRowUpdatesNumbering;
//                tests/core/tst_previewengine.cpp numberingFollowsOrder
//   AC3.4        tests/app/tst_app_mainwindow.cpp sortByNewNameStable;
//                tests/app/tst_app_tableview.cpp headerClickSortsOnce
//   AC-B3 core   tests/core/tst_previewengine.cpp skipBlockedCascade, skipKeepsNumbering
//   AC4.1-AC4.7  tests/core/tst_replacerule.cpp, tst_casenumbering.cpp, tst_insertremove.cpp,
//                tst_extcleanup.cpp (edgeInputs and named tests)
//   AC11.1-11.9  tests/core/tst_previewengine.cpp, tst_namevalidator.cpp
//   AC11.10      tests/core/tst_journal.cpp retentionKeepsUnsafe, retentionDeletesGroups
//   AC12.1-12.10 tests/core/tst_safety.cpp, tst_executor.cpp, tst_planner.cpp, tst_nativefs.cpp
//   AC12.11      tests/core/tst_batchservice.cpp failureKeep, keepRestoresTempNamedItems
//   AC12.12      tests/core/tst_batchservice.cpp prepareDetectsIdentityChange;
//                tests/app/tst_app_mainwindow.cpp refreshDetectsMissing
//   AC12.13      tests/core/tst_executor.cpp rollBackSkipsReplacedItem
//   AC13.1-13.13 tests/core/tst_undo.cpp, tst_journal.cpp, tst_batchservice.cpp
//   AC14.1-14.4  tests/core/tst_presets.cpp; UI: tests/app/tst_app_presets.cpp
//                saveThenLoadIdentical, unsupportedRuleBlocks, newerVersionError
//   AC12.4 case-sensitive, AC13.9 exFAT: scripts/test-volumes.sh (macOS disk images)
//   AC-G1        tests/core/tst_safety.cpp noReplaceEverywhere
//   Manual       tests/manual/v0.1-checklist.md (AC-G2, AC-G3, AC13.5 readable)

#include "appcontext.h"
#include "controllers/itemadder.h"
#include "exec/batchservice.h"
#include "fs/filesystem.h"
#include "mainwindow.h"
#include "models/itemtablemodel.h"
#include "models/rulelistmodel.h"
#include "rules/changecaserule.h"
#include "rules/insertrule.h"
#include "rules/numberingrule.h"
#include "rules/replacerule.h"
#include "temptree.h"
#include "widgets/emptystate.h"
#include "widgets/itemtableview.h"

#include <QDir>
#include <QElapsedTimer>
#include <QMimeData>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtTest>

#include <memory>

using namespace ctrm;
using namespace ctrm::app;

namespace {

struct Fixture
{
    Fixture()
        : qsettings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat)
        , ctx(createNativeFileSystem(), dir.filePath(QStringLiteral("history")),
              dir.filePath(QStringLiteral("presets")), &qsettings)
        , window(ctx)
    {
        window.itemAdder().askFolderDrop = [this](const FolderDropQuestion &q) {
            questions.append(q);
            return reply;
        };
        window.resize(900, 600);
        window.show();
    }

    // Drops the paths on the visible drop target (the empty state or the table), as the OS does.
    void drop(const QStringList &paths)
    {
        QMimeData mime;
        QList<QUrl> urls;
        for (const QString &p : paths)
            urls.append(QUrl::fromLocalFile(p));
        mime.setUrls(urls);
        QWidget *target = window.findChild<EmptyState *>();
        if (!target || !target->isVisible())
            target = window.tableView()->viewport();
        const QPointF pos(target->rect().center());
        QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(target, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent dropEvent(pos, Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(target, &dropEvent);
    }

    QStringList oldNames()
    {
        QStringList result;
        for (const Item &item : window.itemModel().items())
            result.append(item.name);
        return result;
    }

    QTemporaryDir dir;
    TempTree tree;
    QSettings qsettings;
    AppContext ctx;
    MainWindow window;
    FolderDropReply reply{FolderDropReply::Choice::AddContents, false, false};
    QList<FolderDropQuestion> questions;
};

} // namespace

class TestAcceptance : public QObject
{
    Q_OBJECT
private slots:
    void ac1_1_drop1000Under2s();
    void ac1_2_folderQuestionBothChoices();
    void ac1_4_duplicateOnce();
    void ac2_1_preview10kUnder500msNoFreeze();
    void ac2_4_previewEqualsDisk();
};

void TestAcceptance::ac1_1_drop1000Under2s()
{
    Fixture f;
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QStringList paths;
    for (int i = 0; i < 1000; ++i)
        paths.append(f.tree.file(QStringLiteral("photo %1.jpg").arg(i), "x"));
    QElapsedTimer clock;
    clock.start();
    f.drop(paths);
    // All items show in the list, with their preview.
    QTRY_VERIFY_WITH_TIMEOUT(f.window.previewIsCurrent(), 2000);
    const qint64 ms = clock.elapsed();
    QCOMPARE(f.window.itemModel().rowCount(), 1000);
    QCOMPARE(f.window.currentPreview().items.size(), 1000);
    QCOMPARE(f.window.tableView()->model()->rowCount(), 1000);
    QVERIFY2(ms < 2000, qPrintable(QStringLiteral("%1 ms").arg(ms)));
}

void TestAcceptance::ac1_2_folderQuestionBothChoices()
{
    Fixture f;
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    const QString folder = f.tree.mkdir(QStringLiteral("Trip"));
    f.tree.file(QStringLiteral("Trip/b.jpg"), "x");
    f.tree.file(QStringLiteral("Trip/a.jpg"), "x");
    f.tree.file(QStringLiteral("Trip/sub/deep.jpg"), "x");
    const QString loose = f.tree.file(QStringLiteral("loose.txt"), "x");

    f.reply.choice = FolderDropReply::Choice::RenameFolder;
    f.drop({folder, loose});
    QCOMPARE(f.questions.size(), 1);
    QCOMPARE(f.questions[0].folders, QStringList{QStringLiteral("Trip")});
    QCOMPARE(f.oldNames(), (QStringList{QStringLiteral("Trip"), QStringLiteral("loose.txt")}));
    QVERIFY(f.window.itemModel().items().first().isFolder);

    f.window.itemModel().clear();
    f.reply.choice = FolderDropReply::Choice::AddContents;
    f.drop({folder});
    QCOMPARE(f.questions.size(), 2);
    QCOMPARE(f.oldNames(), (QStringList{QStringLiteral("a.jpg"), QStringLiteral("b.jpg"),
                                        QStringLiteral("sub")}));
}

void TestAcceptance::ac1_4_duplicateOnce()
{
    Fixture f;
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    const QString file = f.tree.file(QStringLiteral("a.txt"), "x");
    f.drop({file});
    f.drop({file});
    QCOMPARE(f.oldNames(), QStringList{QStringLiteral("a.txt")});
    QCOMPARE(f.window.statusBar()->currentMessage(),
             QStringLiteral("1 item was already in the list."));
}

void TestAcceptance::ac2_1_preview10kUnder500msNoFreeze()
{
#ifndef NDEBUG
    QSKIP("timing test runs in release builds only");
#endif
    Fixture f;
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QStringList paths;
    for (int i = 0; i < 10000; ++i)
        paths.append(f.tree.file(QStringLiteral("f%1.txt").arg(i), QByteArray()));
    f.window.itemAdder().addPaths(paths);
    QTRY_VERIFY_WITH_TIMEOUT(f.window.previewIsCurrent(), 10000);

    // A rule change: measure the time to the new preview and the longest event loop gap.
    QElapsedTimer clock;
    qint64 last = 0;
    qint64 maxGap = 0;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
        const qint64 now = clock.elapsed();
        maxGap = std::max(maxGap, now - last);
        last = now;
    });
    auto rule = std::make_unique<InsertRule>();
    rule->text = QStringLiteral("x_");
    clock.start();
    timer.start();
    f.window.ruleModel().insertRule(0, std::move(rule));
    QTRY_VERIFY_WITH_TIMEOUT(f.window.previewIsCurrent(), 10000);
    const qint64 ms = clock.elapsed();
    timer.stop();
    QCOMPARE(f.window.currentPreview().items.first().newName, QStringLiteral("x_f0.txt"));
    qInfo("10,000 items: preview %lld ms, longest event loop gap %lld ms", ms, maxGap);
    QVERIFY2(ms < 500, qPrintable(QStringLiteral("preview %1 ms").arg(ms)));
    QVERIFY2(maxGap <= 100, qPrintable(QStringLiteral("gap %1 ms").arg(maxGap)));
}

void TestAcceptance::ac2_4_previewEqualsDisk()
{
    Fixture f;
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    // Different cases in one batch: plain changes, a swap, letter case only and a folder with
    // an item inside it.
    const QString folder = f.tree.mkdir(QStringLiteral("p"));
    for (int i = 1; i <= 50; ++i)
        f.tree.file(QStringLiteral("p/file %1.txt").arg(i), QByteArray::number(i));
    const QString one = f.tree.file(QStringLiteral("one.txt"), "1");
    const QString two = f.tree.file(QStringLiteral("two.txt"), "2");
    const QString photo = f.tree.file(QStringLiteral("photo.JPG"), "j");
    const QString dir = f.tree.mkdir(QStringLiteral("album"));
    const QString inside = f.tree.file(QStringLiteral("album/Inside.txt"), "i");
    f.drop({folder}); // Add Its Contents
    f.reply.choice = FolderDropReply::Choice::RenameFolder;
    f.drop({one, two, photo, dir, inside});
    QCOMPARE(f.window.itemModel().rowCount(), 55);

    // Swap one.txt and two.txt; lower-case the rest; number the "file" items.
    auto swapA = std::make_unique<ReplaceRule>();
    swapA->find = QStringLiteral("^(one|two)$"); // the rules change the stem
    swapA->mode = ReplaceRule::Mode::Regex;
    swapA->replace = QStringLiteral("tmp_$1");
    auto swapB = std::make_unique<ReplaceRule>();
    swapB->find = QStringLiteral("^tmp_one$");
    swapB->mode = ReplaceRule::Mode::Regex;
    swapB->replace = QStringLiteral("two");
    auto swapC = std::make_unique<ReplaceRule>();
    swapC->find = QStringLiteral("^tmp_two$");
    swapC->mode = ReplaceRule::Mode::Regex;
    swapC->replace = QStringLiteral("one");
    auto lower = std::make_unique<ChangeCaseRule>();
    lower->caseMode = ChangeCaseRule::Case::Lower;
    lower->target = Target::Name;
    auto files = std::make_unique<ReplaceRule>();
    files->find = QStringLiteral("file ");
    files->replace = QStringLiteral("doc-");
    auto album = std::make_unique<ReplaceRule>();
    album->find = QStringLiteral("album");
    album->replace = QStringLiteral("Album 2026");
    RuleListModel &rules = f.window.ruleModel();
    rules.insertRule(0, std::move(swapA));
    rules.insertRule(1, std::move(swapB));
    rules.insertRule(2, std::move(swapC));
    rules.insertRule(3, std::move(lower));
    rules.insertRule(4, std::move(files));
    rules.insertRule(5, std::move(album));
    QTRY_VERIFY(f.window.previewIsCurrent());

    // The names that the table shows.
    const QList<Item> items = f.window.itemModel().items();
    QHash<QString, QString> shown; // old path -> shown new name
    const QAbstractItemModel &model = f.window.itemModel();
    for (int row = 0; row < items.size(); ++row) {
        shown.insert(items[row].path(),
                     model.index(row, ItemTableModel::NewName).data().toString());
    }
    QCOMPARE(shown.value(one), QStringLiteral("two.txt"));
    QCOMPARE(shown.value(two), QStringLiteral("one.txt"));
    QCOMPARE(shown.value(photo), QStringLiteral("photo.jpg"));
    QCOMPARE(shown.value(dir), QStringLiteral("Album 2026"));
    QCOMPARE(f.window.currentPreview().summary.willChange, 55);

    // Rename with the input and the preview of the window.
    const PreviewInput input = f.window.currentPreviewInput();
    BatchService &service = f.ctx.batchService();
    const PrepareResult prepared = service.prepare(input, f.window.currentPreview());
    QCOMPARE(prepared.kind, PrepareResult::Kind::Ready);
    QString error;
    auto session = service.start(*prepared.plan, input.chain, {}, &error);
    QVERIFY2(session, qPrintable(error));
    std::atomic<bool> cancel{false};
    QVERIFY(session->run(cancel).finished);
    QCOMPARE(session->complete().state, BatchState::Complete);

    // Each item is on disk with the shown name (the contents prove which item it is).
    const auto contentOf = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<missing>");
    };
    const QString root = f.tree.root();
    for (int i = 1; i <= 50; ++i) {
        const QString name = shown.value(folder + QStringLiteral("/file %1.txt").arg(i));
        QCOMPARE(name, QStringLiteral("doc-%1.txt").arg(i));
        QCOMPARE(contentOf(folder + u'/' + name), QByteArray::number(i));
    }
    QCOMPARE(contentOf(root + QStringLiteral("/two.txt")), QByteArray("1"));
    QCOMPARE(contentOf(root + QStringLiteral("/one.txt")), QByteArray("2"));
    QVERIFY(QDir(root).entryList(QDir::Files).contains(QStringLiteral("photo.jpg")));
    QVERIFY(!QDir(root).entryList(QDir::Files).contains(QStringLiteral("photo.JPG")));
    QCOMPARE(contentOf(root + QStringLiteral("/Album 2026/inside.txt")), QByteArray("i"));
}

QTEST_MAIN(TestAcceptance)
#include "tst_app_acceptance.moc"
