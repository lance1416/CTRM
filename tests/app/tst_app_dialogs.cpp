// SPDX-License-Identifier: GPL-3.0-or-later
// The common geometry of the dialogs (design-tokens 4.2, 4.3; craft audit O07, C11).
#include "controllers/batchcontroller.h"
#include "dialogs/aboutdialog.h"
#include "dialogs/failuredialog.h"
#include "dialogs/folderdropdialog.h"
#include "dialogs/historywindow.h"
#include "dialogs/managepresetsdialog.h"
#include "dialogs/preferencesdialog.h"
#include "dialogs/recoverydialog.h"
#include "dialogs/reportdialog.h"
#include "dialogs/reportproblemdialog.h"
#include "dialogs/savepresetdialog.h"
#include "dialogs/undopreviewdialog.h"
#include "exec/batchservice.h"
#include "exec/historystore.h"
#include "exec/undo.h"
#include "presets/presetstore.h"
#include "settings.h"
#include "theme/tokens.h"

#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QSettings>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <memory>

using namespace ctrm;
using namespace ctrm::app;

namespace {

using Maker = std::function<std::unique_ptr<QWidget>()>;

struct Stores
{
    QTemporaryDir dir;
    QSettings qsettings{dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat};
    Settings settings{&qsettings};
    HistoryStore history{dir.filePath(QStringLiteral("history"))};
    PresetStore presets{dir.filePath(QStringLiteral("presets"))};
};

Stores &stores()
{
    static Stores s;
    return s;
}

// The geometry of the visible part of a layout item: a nested layout or a widget.
QRect itemRect(QLayoutItem *item)
{
    return item->geometry();
}

bool isContent(QLayoutItem *item)
{
    return !item->spacerItem() && !item->isEmpty();
}

} // namespace

class TestAppDialogs : public QObject
{
    Q_OBJECT
private slots:
    void insets_data();
    void insets();
    void tableRowHeight_data();
    void tableRowHeight();
    void preferencesSections();
};

void TestAppDialogs::insets_data()
{
    QTest::addColumn<Maker>("make");
    const auto add = [](const char *name, Maker make) { QTest::newRow(name) << std::move(make); };
    add("about", [] { return std::make_unique<AboutDialog>(nullptr); });
    add("failure", [] {
        FailureInfo info;
        info.done = 1;
        info.total = 2;
        return std::make_unique<FailureDialog>(info, nullptr);
    });
    add("folderDrop", [] {
        return std::make_unique<FolderDropDialog>(FolderDropQuestion{{QStringLiteral("Trip")}},
                                                  nullptr);
    });
    add("history", [] { return std::make_unique<HistoryWindow>(stores().history, nullptr); });
    add("managePresets",
        [] { return std::make_unique<ManagePresetsDialog>(stores().presets, nullptr); });
    add("preferences", [] {
        return std::make_unique<PreferencesDialog>(stores().settings,
                                                   PreferencesDialog::ApplyMode::OnAccept, nullptr);
    });
    add("recovery", [] {
        RecoveryInfo info;
        info.batchId = QStringLiteral("b1");
        info.started = QDateTime::currentDateTime();
        info.done = 1;
        info.total = 2;
        info.doneItems = 1;
        info.folders = {QStringLiteral("/tmp")};
        return std::make_unique<RecoveryDialog>(info, nullptr);
    });
    add("report", [] {
        return std::make_unique<ReportDialog>(FinishResult{}, QDateTime::currentDateTime(),
                                              nullptr);
    });
    add("reportProblem",
        [] { return std::make_unique<ReportProblemDialog>(QStringLiteral("CTRM"), nullptr); });
    add("savePreset", [] {
        return std::make_unique<SavePresetDialog>(QStringList{}, QStringList{},
                                                  QStringLiteral("My Rules"), nullptr);
    });
    add("undoPreview", [] { return std::make_unique<UndoPreviewDialog>(UndoPreview{}, nullptr); });
}

void TestAppDialogs::insets()
{
    // Dialog inset space-5 on each side; space-6 before the button row (the last row).
    QFETCH(Maker, make);
    std::unique_ptr<QWidget> dialog = make();
    dialog->show();
    QVERIFY(QTest::qWaitForWindowExposed(dialog.get()));
    QLayout *layout = dialog->layout();
    QVERIFY(layout);
    const int s5 = tokens::space5;
    QCOMPARE(layout->contentsMargins(), QMargins(s5, s5, s5, s5));

    QList<QLayoutItem *> content;
    for (int i = 0; i < layout->count(); ++i) {
        if (isContent(layout->itemAt(i)))
            content.append(layout->itemAt(i));
    }
    QVERIFY(content.size() >= 2);
    const QRect buttons = itemRect(content.constLast());
    const QRect above = itemRect(content.at(content.size() - 2));
    QCOMPARE(buttons.top() - above.bottom() - 1, tokens::space6);
}

void TestAppDialogs::tableRowHeight_data()
{
    insets_data();
}

void TestAppDialogs::tableRowHeight()
{
    // C11: all tables of the app have one row height, row-h.
    QFETCH(Maker, make);
    std::unique_ptr<QWidget> dialog = make();
    for (QTableView *table : dialog->findChildren<QTableView *>())
        QCOMPARE(table->verticalHeader()->defaultSectionSize(), tokens::rowH);
}

void TestAppDialogs::preferencesSections()
{
    // C12: one spin box width; under each section title its rows, space-2 apart; space-4
    // between the sections.
    PreferencesDialog dialog(stores().settings, PreferencesDialog::ApplyMode::Immediately, nullptr);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    auto *count = dialog.findChild<QSpinBox *>(QStringLiteral("retentionCount"));
    auto *days = dialog.findChild<QSpinBox *>(QStringLiteral("retentionDays"));
    QVERIFY(count && days);
    QCOMPARE(count->width(), days->width());
    QVERIFY(count->width() >= count->sizeHint().width());

    QLayout *layout = dialog.layout();
    QList<QLayoutItem *> items;
    for (int i = 0; i < layout->count(); ++i) {
        if (isContent(layout->itemAt(i)))
            items.append(layout->itemAt(i));
    }
    QCOMPARE(items.size(), 6); // 3 x (title, form)
    for (int section = 0; section < 3; ++section) {
        QLayoutItem *title = items.at(2 * section);
        QLayoutItem *form = items.at(2 * section + 1);
        QVERIFY(qobject_cast<QLabel *>(title->widget()));
        QVERIFY(qobject_cast<QFormLayout *>(form->layout()));
        QCOMPARE(qobject_cast<QFormLayout *>(form->layout())->verticalSpacing(), tokens::space2);
        QCOMPARE(form->geometry().top() - title->geometry().bottom() - 1, tokens::space2);
        if (section > 0) {
            const QRect above = items.at(2 * section - 1)->geometry();
            QCOMPARE(title->geometry().top() - above.bottom() - 1, tokens::space4);
        }
    }
}

QTEST_MAIN(TestAppDialogs)
#include "tst_app_dialogs.moc"
