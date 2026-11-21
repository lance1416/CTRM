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

#include <QApplication>
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
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
    void aboutRowGaps();
    void savePresetWidth();
    void savePresetNoteHeight_data();
    void savePresetNoteHeight();
    void skipConfirmBox();
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

void TestAppDialogs::aboutRowGaps()
{
    // The rows are space-2 apart: the dialog is as high as its rows at its width, so no free
    // height goes into a label (a centred label shows that height as a gap).
    AboutDialog dialog(nullptr);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    for (QLabel *label : dialog.findChildren<QLabel *>()) {
        const int needed = label->hasHeightForWidth() ? label->heightForWidth(label->width())
                                                      : label->sizeHint().height();
        QVERIFY2(label->height() == needed, qPrintable(QStringLiteral("%1: %2, needs %3")
                                                               .arg(label->objectName())
                                                               .arg(label->height())
                                                               .arg(needed)));
    }
}

// R6-07: the name field is field-w-max wide, and the note wraps at the field width.
void TestAppDialogs::savePresetWidth()
{
    SavePresetDialog d({QStringLiteral("Photo Trip")}, {}, QStringLiteral("My Rules"), nullptr);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    auto *form = d.findChild<QFormLayout *>();
    auto *edit = d.findChild<QLineEdit *>(QStringLiteral("presetNameEdit"));
    auto *note = d.findChild<QLabel *>(QStringLiteral("presetNameNote"));
    QVERIFY(form && edit && note);
    int row = -1;
    QFormLayout::ItemRole role;
    form->getWidgetPosition(edit, &row, &role);
    QCOMPARE(form->itemAt(row, QFormLayout::FieldRole)->geometry().width(), tokens::fieldWMax);
    QVERIFY2(d.width() >= 400, qPrintable(QString::number(d.width())));

    edit->setText(QStringLiteral("Photo Trip")); // the note shows
    QApplication::processEvents();
    d.layout()->activate();
    QVERIFY(note->isVisible());
    QCOMPARE(note->width(), tokens::fieldWMax);
    QCOMPARE(note->height(), note->heightForWidth(note->width()));
}

void TestAppDialogs::savePresetNoteHeight_data()
{
    // The width of the note text, as a part of field-w-max.
    QTest::addColumn<double>("textPart");
    QTest::newRow("oneLine") << 0.9; // QLabel's own size hint wraps this text at half the width
    QTest::newRow("twoLines") << 1.5;
}

// The note row is as high as the note text at the field width: no empty line under one line of
// text, and a long text wraps at field-w-max (audit R6-07, Windows CI 44804410991).
void TestAppDialogs::savePresetNoteHeight()
{
    QFETCH(double, textPart);
    SavePresetDialog d({QStringLiteral("Photo Trip")}, {}, QStringLiteral("Photo Trip"), nullptr);
    auto *note = d.findChild<QLabel *>(QStringLiteral("presetNameNote"));
    QVERIFY(note && !note->text().isEmpty());
    QFont font = d.font();
    int best = 0;
    for (int px = 6; px <= 40; ++px) {
        font.setPixelSize(px);
        const int width = QFontMetrics(font).horizontalAdvance(note->text());
        if (qAbs(width - textPart * tokens::fieldWMax) <
            qAbs(best - textPart * tokens::fieldWMax)) {
            best = width;
            d.setFont(font);
        }
    }
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QApplication::processEvents(); // the form rhythm follows the font change
    d.layout()->activate();
    QVERIFY(note->isVisible());
    QCOMPARE(note->width(), tokens::fieldWMax);
    QCOMPARE(note->height(), note->heightForWidth(note->width()));
}

// R6-08: the Skip and Rename question has the app icon, and its text is not narrow.
void TestAppDialogs::skipConfirmBox()
{
    SkipQuestion q;
    q.skipCount = 2;
    q.renameCount = 4;
    q.conflicts = 2;
    q.cascadeCount = 1;
    const std::unique_ptr<QMessageBox> box = makeSkipConfirmBox(nullptr, q);
    QCOMPARE(box->buttonRole(box->defaultButton()), QMessageBox::AcceptRole); // Skip and Rename
    QCOMPARE(box->buttonRole(box->escapeButton()), QMessageBox::RejectRole);
    QCOMPARE(box->icon(), QMessageBox::NoIcon);
    QVERIFY(!box->iconPixmap().isNull());
    QCOMPARE(box->iconPixmap().deviceIndependentSize().toSize(), QSize(64, 64));
    box->show();
    QVERIFY(QTest::qWaitForWindowExposed(box.get()));
    auto *info = box->findChild<QLabel *>(QStringLiteral("qt_msgbox_informativelabel"));
    QVERIFY(info);
    QVERIFY2(info->width() >= tokens::fieldWMax, qPrintable(QString::number(info->width())));
}

QTEST_MAIN(TestAppDialogs)
#include "tst_app_dialogs.moc"
