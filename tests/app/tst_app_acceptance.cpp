// SPDX-License-Identifier: GPL-3.0-or-later
//
// PRD section 10 acceptance tests that need the full app. The other criteria have their tests in
// the task files:
//   AC1.3        tests/core/tst_itemsource.cpp folderChildrenHidden;
//                tests/app/tst_app_itemadder.cpp hiddenNotAdded
//   AC1.5        tests/core/tst_nativefs.cpp renameSymlinkNotTarget;
//                tests/core/tst_safety.cpp symlinkRenamedNotTarget
//   AC1.6        tests/core/tst_itemsource.cpp folderChildrenAppleDouble; exFAT: test-volumes.sh
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
//   AC-B1, AC-B2 tests/app/tst_app_batch.cpp renameNoConfirm, skipAsksWithCount
//   AC-B3 core   tests/core/tst_previewengine.cpp skipBlockedCascade, skipKeepsNumbering;
//                UI: tests/app/tst_app_batch.cpp skipShowsCascade and this file
//   AC4.1-AC4.7  tests/core/tst_replacerule.cpp, tst_casenumbering.cpp, tst_insertremove.cpp,
//                tst_extcleanup.cpp (edgeInputs and named tests)
//   AC11.1-11.9  tests/core/tst_previewengine.cpp, tst_namevalidator.cpp
//   AC11.10      tests/core/tst_journal.cpp retentionKeepsUnsafe, retentionDeletesGroups
//   AC12.1-12.10 tests/core/tst_safety.cpp, tst_executor.cpp, tst_planner.cpp, tst_nativefs.cpp
//   AC12.11      tests/core/tst_batchservice.cpp failureKeep, keepRestoresTempNamedItems
//   AC12.12      tests/core/tst_batchservice.cpp prepareDetectsIdentityChange;
//                tests/app/tst_app_mainwindow.cpp refreshDetectsMissing
//   AC12.13      tests/core/tst_executor.cpp rollBackSkipsReplacedItem
//   AC12.8 UI    tests/app/tst_app_recovery.cpp rollBackChoice, quitDuringBatchAskedAtNextStart
//   AC13.1-13.13 tests/core/tst_undo.cpp, tst_journal.cpp, tst_batchservice.cpp
//   AC13.2 UI    tests/app/tst_app_history.cpp undoAfterRestart
//   AC13.6 UI    tests/app/tst_app_history.cpp quickUndoNoPreview, quickUndoOpensPreview
//   AC13.7 UI    tests/app/tst_app_history.cpp partlyUndoneUi
//   AC13.8 UI    tests/app/tst_app_history.cpp undoOfLabel
//   AC13.9 UI    tests/app/tst_app_history.cpp quickUndoWeakOpensPreview
//   AC13.10 UI   tests/app/tst_app_recovery.cpp markCompleteChoice
//   AC13.12 UI   tests/app/tst_app_history.cpp damagedRow, clearKeepsDamaged
//   AC14.1-14.4  tests/core/tst_presets.cpp; UI: tests/app/tst_app_presets.cpp
//                saveThenLoadIdentical, unsupportedRuleBlocks, newerVersionError
//   AC12.4 case-sensitive, AC13.9 and AC13.13 exFAT: scripts/test-volumes.sh (macOS images)
//   AC-G1        tests/core/tst_safety.cpp noReplaceEverywhere
//   AC-G3        this file acG3_keyboardOnly (all steps); the visible focus is manual
//   Manual       tests/manual/v0.1-checklist.md (AC-G2, AC-G3 focus, AC13.5 readable)

#include "appcontext.h"
#include "controllers/batchcontroller.h"
#include "controllers/historycontroller.h"
#include "controllers/itemadder.h"
#include "dialogs/historywindow.h"
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
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtTest>

#include <memory>
#include <optional>

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
    // Returns true when the target accepts both events.
    [[nodiscard]] bool drop(const QStringList &paths)
    {
        QMimeData mime;
        QList<QUrl> urls;
        for (const QString &p : paths)
            urls.append(QUrl::fromLocalFile(p));
        mime.setUrls(urls);
        QWidget *target = window.findChild<EmptyState *>();
        if (!target || !target->isVisible())
            target = window.tableView()->viewport();
        // Qt 6.8 has only the QPoint constructor; Qt 6.12 adds QPointF and deprecates QPoint.
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        const QPointF pos(target->rect().center());
#else
        const QPoint pos = target->rect().center();
#endif
        QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(target, &enter);
        if (!enter.isAccepted())
            return false;
        QDropEvent dropEvent(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(target, &dropEvent);
        return dropEvent.isAccepted();
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
    void acB3_skipCascadeKeepsNumbering();
    void acG3_keyboardOnly();
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
    QVERIFY(f.drop(paths));
    // All items show in the list, with their preview.
    QTRY_VERIFY_WITH_TIMEOUT(f.window.previewIsCurrent(), 30'000);
    const qint64 ms = clock.elapsed();
    QCOMPARE(f.window.itemModel().rowCount(), 1000);
    QCOMPARE(f.window.currentPreview().items.size(), 1000);
    QCOMPARE(f.window.tableView()->model()->rowCount(), 1000);
    qInfo("AC1.1: 1000 items added and previewed in %lld ms", ms);
#ifdef NDEBUG
    // Timing in a Debug build proves nothing (same rule as AC2.1). The perf job checks it.
    QVERIFY2(ms < 2000, qPrintable(QStringLiteral("%1 ms").arg(ms)));
#endif
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
    QVERIFY(f.drop({folder, loose}));
    QCOMPARE(f.questions.size(), 1);
    QCOMPARE(f.questions[0].folders, QStringList{QStringLiteral("Trip")});
    QCOMPARE(f.oldNames(), (QStringList{QStringLiteral("Trip"), QStringLiteral("loose.txt")}));
    QVERIFY(f.window.itemModel().items().first().isFolder);

    f.window.itemModel().clear();
    f.reply.choice = FolderDropReply::Choice::AddContents;
    QVERIFY(f.drop({folder}));
    QCOMPARE(f.questions.size(), 2);
    QCOMPARE(f.oldNames(), (QStringList{QStringLiteral("a.jpg"), QStringLiteral("b.jpg"),
                                        QStringLiteral("sub")}));
}

void TestAcceptance::ac1_4_duplicateOnce()
{
    Fixture f;
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    const QString file = f.tree.file(QStringLiteral("a.txt"), "x");
    QVERIFY(f.drop({file}));
    QVERIFY(f.drop({file}));
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
    QVERIFY(f.drop({folder})); // Add Its Contents
    f.reply.choice = FolderDropReply::Choice::RenameFolder;
    QVERIFY(f.drop({one, two, photo, dir, inside}));
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

void TestAcceptance::acB3_skipCascadeKeepsNumbering()
{
    Fixture f;
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    BatchController batch(f.window, f.ctx, nullptr);
    std::optional<SkipQuestion> question;
    batch.confirmSkip = [&](const SkipQuestion &q) {
        question = q;
        return true;
    };
    batch.askFailure = [](const FailureInfo &) { return FailureChoice::RollBack; };
    batch.showReport = [](const FinishResult &) {};

    QStringList paths;
    for (const char *name : {"1", "b", "c", "d"})
        paths.append(f.tree.file(QLatin1StringView(name), name));
    QVERIFY(f.drop(paths));

    // Number all items, then "1_1" becomes "" (A is invalid) and "b_2" becomes "1" (B wants the
    // old name of A).
    auto numbering = std::make_unique<NumberingRule>();
    auto invalid = std::make_unique<ReplaceRule>();
    invalid->find = QStringLiteral("^1_1$");
    invalid->mode = ReplaceRule::Mode::Regex;
    auto wantsOld = std::make_unique<ReplaceRule>();
    wantsOld->find = QStringLiteral("^b_2$");
    wantsOld->mode = ReplaceRule::Mode::Regex;
    wantsOld->replace = QStringLiteral("1");
    RuleListModel &rules = f.window.ruleModel();
    rules.insertRule(0, std::move(numbering));
    rules.insertRule(1, std::move(invalid));
    rules.insertRule(2, std::move(wantsOld));
    QTRY_VERIFY(f.window.previewIsCurrent());
    QCOMPARE(f.oldNames(), (QStringList{QStringLiteral("1"), QStringLiteral("b"),
                                        QStringLiteral("c"), QStringLiteral("d")}));
    const PreviewResult before = f.window.currentPreview();
    QCOMPARE(before.items[2].newName, QStringLiteral("c_3"));
    QCOMPARE(before.items[3].newName, QStringLiteral("d_4"));

    QSignalSpy finished(&batch, &BatchController::batchFinished);
    f.window.actions().skipAndRename->trigger();
    QVERIFY(finished.wait(30'000) || finished.count() > 0);

    // The question names B as skipped only because of another item.
    QVERIFY(question.has_value());
    QCOMPARE(question->skipCount, 2);
    QCOMPARE(question->invalid, 1);
    QCOMPARE(question->cascadeCount, 1);
    QCOMPARE(question->renameCount, 2);

    // A and B keep their names. The other items keep the numbers of the first preview.
    const QStringList onDisk = QDir(f.tree.root()).entryList(QDir::Files, QDir::Name);
    QCOMPARE(onDisk, (QStringList{QStringLiteral("1"), QStringLiteral("b"), QStringLiteral("c_3"),
                                  QStringLiteral("d_4")}));
}

void TestAcceptance::acG3_keyboardOnly()
{
    // The OS file dialog (Ctrl+O) is not part of this test: the files are added directly. All
    // other steps use only key presses on the window.
    Fixture f;
    BatchController batch(f.window, f.ctx, nullptr);
    HistoryController history(f.window, f.ctx, batch, nullptr);
    batch.askFailure = [](const FailureInfo &) { return FailureChoice::RollBack; };
    batch.showReport = [](const FinishResult &) {};
    int undoPreviews = 0;
    history.showUndoPreview = [&](const UndoPreview &) {
        ++undoPreviews;
        return true;
    };
    f.window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    const QString a = f.tree.file(QStringLiteral("a.txt"), "a");
    const QString b = f.tree.file(QStringLiteral("b.txt"), "b");
    f.window.itemAdder().addPaths({a, b});
    const auto reactivate = [&] {
        if (QApplication::activeWindow() == &f.window)
            return true;
        f.window.activateWindow();
        return QTest::qWaitForWindowActive(&f.window);
    };
    const auto key = [&](Qt::Key k, Qt::KeyboardModifiers m = Qt::ControlModifier) {
        QWidget *target = QApplication::focusWidget();
        QTest::keyClick(target ? target : &f.window, k, m);
    };

    // Add an Insert rule from the Add Rule menu (Ctrl+Shift+N, arrow keys, Return).
    QString menuError;
    QTimer::singleShot(0, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) {
            menuError = QStringLiteral("no Add Rule menu");
            return;
        }
        for (int i = 0; i < 30; ++i) {
            QTest::keyClick(menu, Qt::Key_Down);
            if (menu->activeAction() &&
                menu->activeAction()->objectName() == QStringLiteral("addRule_insert"))
                break;
        }
        if (!menu->activeAction() ||
            menu->activeAction()->objectName() != QStringLiteral("addRule_insert")) {
            menuError = QStringLiteral("no Insert item in the menu");
            menu->close();
            return;
        }
        QTest::keyClick(menu, Qt::Key_Return);
    });
    key(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY2(f.window.ruleModel().rowCount() == 1 || !menuError.isEmpty(),
                 qPrintable(menuError));
    QVERIFY2(menuError.isEmpty(), qPrintable(menuError));

    // The focus is in the first field of the rule editor: type the text.
    // The offscreen platform does not make the window active again when the menu closes.
    QVERIFY(reactivate());
    QTRY_VERIFY(qobject_cast<QLineEdit *>(QApplication::focusWidget()));
    auto *field = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    QTest::keyClicks(field, QStringLiteral("x_"));
    QTRY_COMPARE(f.window.currentPreview().items.value(0).newName, QStringLiteral("x_a.txt"));
    QTRY_VERIFY(f.window.previewIsCurrent());

    // Rename (Ctrl+Return).
    QSignalSpy finished(&batch, &BatchController::batchFinished);
    key(Qt::Key_Return);
    QVERIFY(finished.wait(30'000) || finished.count() > 0);
    QTRY_VERIFY(QFile::exists(f.tree.root() + QStringLiteral("/x_a.txt")));
    QVERIFY(QFile::exists(f.tree.root() + QStringLiteral("/x_b.txt")));

    // The batch makes the rules read-only, so the text field loses the focus, and no control gets
    // it back after the batch. Without a focus widget the offscreen platform also makes the
    // window inactive: make it active again. The window shortcuts still work.
    QVERIFY(reactivate());
    // Go to the items (Ctrl+2), then Quick Undo (Ctrl+Z). The items are unchanged.
    key(Qt::Key_2);
    QTRY_COMPARE(QApplication::focusWidget(), f.window.tableView());
    QTRY_VERIFY(f.window.actions().undoRename->isEnabled());
    finished.clear();
    key(Qt::Key_Z);
    QVERIFY(finished.wait(30'000) || finished.count() > 0);
    // With a weak identity (exFAT, FAT), Quick Undo opens the undo preview (AC13.9).
    const bool weak =
            f.ctx.fileSystem().volumeInfo(f.tree.root()).identityType == IdentityType::Weak;
    QCOMPARE(undoPreviews, weak ? 1 : 0);
    QTRY_VERIFY(QFile::exists(a));
    QVERIFY(QFile::exists(b));
    QVERIFY(reactivate());

    // Open History (Ctrl+Y on macOS, Ctrl+H on other systems).
#ifdef Q_OS_MACOS
    key(Qt::Key_Y);
#else
    key(Qt::Key_H);
#endif
    auto *historyWindow = f.window.findChild<HistoryWindow *>();
    QVERIFY(historyWindow);
    QTRY_VERIFY(historyWindow->isVisible());
    QCOMPARE(f.ctx.history().list().size(), 2);
    historyWindow->close();
}

QTEST_MAIN(TestAcceptance)
#include "tst_app_acceptance.moc"
