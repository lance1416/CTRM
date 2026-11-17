// SPDX-License-Identifier: GPL-3.0-or-later
#include "actions.h"
#include "appcontext.h"
#include "controllers/preferencescontroller.h"
#include "dialogs/preferencesdialog.h"
#include "exec/historystore.h"
#include "exec/journal.h"
#include "fs/filesystem.h"
#include "mainwindow.h"
#include "model/item.h"
#include "settings.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUuid>
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
        , controller(window, ctx, &window)
    {}

    PreferencesDialog *open()
    {
        controller.showPreferences();
        auto *d = window.findChild<PreferencesDialog *>();
        return d;
    }

    QTemporaryDir dir;
    QSettings qsettings;
    AppContext ctx;
    MainWindow window;
    PreferencesController controller;
};

template <typename T>
T *child(QWidget *w, const char *name)
{
    return w->findChild<T *>(QLatin1StringView(name));
}

// A complete batch journal (header and state lines) that started at the given time.
void writeJournal(const QString &dir, const QDateTime &started)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject request{{"item", "1"},       {"folder", "/a"},
                              {"oldName", "x"},    {"newName", "y"},
                              {"isFolder", false}, {"identity", identityToJson(FileIdentity{})}};
    const QJsonObject header{
            {"type", "header"},
            {"format", "ctrm-journal"},
            {"formatVersion", 1},
            {"batchId", id},
            {"started", started.toString(Qt::ISODateWithMs)},
            {"ctrmVersion", "0.1.0"},
            {"os", "test"},
            {"chain", QJsonObject()},
            {"undoOf", QJsonValue()},
            {"requests", QJsonArray{request}},
            {"steps", QJsonArray()},
    };
    QByteArray data = QJsonDocument(header).toJson(QJsonDocument::Compact) + '\n';
    for (const BatchState s : {BatchState::InProgress, BatchState::Complete}) {
        data += QJsonDocument(QJsonObject{{"type", "state"},
                                          {"state", batchStateToString(s)},
                                          {"time", started.toString(Qt::ISODateWithMs)}})
                        .toJson(QJsonDocument::Compact) +
                '\n';
    }
    QFile f(dir + "/" + started.toString("yyyyMMdd'T'HHmmsszzz'Z'") + "-" + id + ".jsonl");
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        qFatal("cannot write journal");
}

} // namespace

class TestPreferences : public QObject
{
    Q_OBJECT
private slots:
    void loadsSettings();
    void changesApply();
    void cancelRestores();
    void escapeCancels();
    void typedNumberSavedOnce();
    void highlightFollowsAction();
    void retentionUsesSettings();
    void formOneFieldEdge();
    void nothingCut();
};

void TestPreferences::loadsSettings()
{
    Fixture f;
    f.ctx.settings().setFolderDropAnswer(FolderDropAnswer::RenameFolder);
    f.ctx.settings().setIncludeHidden(true);
    f.ctx.settings().setHighlightChanged(false);
    f.ctx.settings().setRetentionCount(42);
    f.ctx.settings().setRetentionDays(7);
    PreferencesDialog *d = f.open();
    QVERIFY(d);
    QVERIFY(d->isVisible());
    QCOMPARE(child<QComboBox>(d, "folderDropAnswer")->currentData().toInt(),
             int(FolderDropAnswer::RenameFolder));
    QVERIFY(child<QCheckBox>(d, "includeHidden")->isChecked());
    QVERIFY(!child<QCheckBox>(d, "highlightChanged")->isChecked());
    QCOMPARE(child<QSpinBox>(d, "retentionCount")->value(), 42);
    QCOMPARE(child<QSpinBox>(d, "retentionDays")->value(), 7);
    QCOMPARE(child<QSpinBox>(d, "retentionCount")->maximum(), 100000);
    QCOMPARE(child<QSpinBox>(d, "retentionDays")->maximum(), 3650);
    QCOMPARE(child<QSpinBox>(d, "retentionDays")->minimum(), 1);
    // A second call shows the same dialog.
    f.controller.showPreferences();
    QCOMPARE(f.window.findChildren<PreferencesDialog *>().size(), 1);
}

void TestPreferences::changesApply()
{
    for (const auto mode :
         {PreferencesDialog::ApplyMode::Immediately, PreferencesDialog::ApplyMode::OnAccept}) {
        QTemporaryDir dir;
        QSettings qs(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        Settings settings(&qs);
        PreferencesDialog d(settings, mode, nullptr);
        d.show();
        auto *combo = child<QComboBox>(&d, "folderDropAnswer");
        combo->setCurrentIndex(combo->findData(int(FolderDropAnswer::AddContents)));
        child<QSpinBox>(&d, "retentionDays")->setValue(30);
        if (mode == PreferencesDialog::ApplyMode::OnAccept) {
            QCOMPARE(settings.folderDropAnswer(), FolderDropAnswer::Ask);
            auto *box = child<QDialogButtonBox>(&d, "buttons");
            QVERIFY(box);
            box->button(QDialogButtonBox::Ok)->click();
        } else {
            QVERIFY(!child<QDialogButtonBox>(&d, "buttons"));
        }
        QCOMPARE(settings.folderDropAnswer(), FolderDropAnswer::AddContents);
        QCOMPARE(settings.retentionDays(), 30);
    }
}

void TestPreferences::cancelRestores()
{
    QTemporaryDir dir;
    QSettings qs(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    Settings settings(&qs);
    PreferencesDialog d(settings, PreferencesDialog::ApplyMode::OnAccept, nullptr);
    d.show();
    child<QCheckBox>(&d, "includeHidden")->setChecked(true);
    child<QSpinBox>(&d, "retentionCount")->setValue(9);
    child<QDialogButtonBox>(&d, "buttons")->button(QDialogButtonBox::Cancel)->click();
    QVERIFY(!settings.includeHidden());
    QCOMPARE(settings.retentionCount(), 500);
    // The controls show the saved values again, not the cancelled ones.
    QVERIFY(!child<QCheckBox>(&d, "includeHidden")->isChecked());
    QCOMPARE(child<QSpinBox>(&d, "retentionCount")->value(), 500);
}

void TestPreferences::escapeCancels()
{
    QTemporaryDir dir;
    QSettings qs(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    Settings settings(&qs);
    settings.setHighlightChanged(true);
    PreferencesDialog d(settings, PreferencesDialog::ApplyMode::OnAccept, nullptr);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    child<QCheckBox>(&d, "highlightChanged")->setChecked(false);
    QTest::keyClick(&d, Qt::Key_Escape);
    QVERIFY(!d.isVisible());
    QVERIFY(settings.highlightChanged());
    QVERIFY(child<QCheckBox>(&d, "highlightChanged")->isChecked());
}

void TestPreferences::typedNumberSavedOnce()
{
    QTemporaryDir dir;
    QSettings qs(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    Settings settings(&qs);
    PreferencesDialog d(settings, PreferencesDialog::ApplyMode::Immediately, nullptr);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    auto *spin = child<QSpinBox>(&d, "retentionCount");
    spin->setFocus();
    spin->selectAll();
    QTest::keyClicks(spin, QStringLiteral("100"));
    QCOMPARE(settings.retentionCount(), 500); // not 1, then 10
    QTest::keyClick(spin, Qt::Key_Return);
    QCOMPARE(settings.retentionCount(), 100);
}

void TestPreferences::highlightFollowsAction()
{
    Fixture f;
    QAction *action = f.window.actions().highlightChanged;
    action->setChecked(true);
    PreferencesDialog *d = f.open();
    auto *check = child<QCheckBox>(d, "highlightChanged");
    QVERIFY(check->isChecked());
    // The menu item changes while the dialog is open.
    action->setChecked(false);
    QVERIFY(!check->isChecked());
    // The dialog changes the menu item.
    check->setChecked(true);
    if (auto *box = child<QDialogButtonBox>(d, "buttons"))
        box->button(QDialogButtonBox::Ok)->click();
    QVERIFY(action->isChecked());
    QVERIFY(f.ctx.settings().highlightChanged());
}

void TestPreferences::retentionUsesSettings()
{
    Fixture f;
    const QDateTime now(QDate(2026, 11, 17), QTime(12, 0), QTimeZone::UTC);
    writeJournal(f.ctx.historyDir(), now.addDays(-10));
    writeJournal(f.ctx.historyDir(), now.addDays(-5));
    QCOMPARE(f.ctx.history().list().size(), 2);
    f.ctx.settings().setRetentionCount(1);
    f.ctx.settings().setRetentionDays(1);
    QCOMPARE(f.controller.applyRetention(now), 1);
    QCOMPARE(f.ctx.history().list().size(), 1);
    QCOMPARE(f.ctx.history().list().at(0).started, now.addDays(-5));
}

void TestPreferences::formOneFieldEdge()
{
    QTemporaryDir dir;
    QSettings qs(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    Settings settings(&qs);
    PreferencesDialog d(settings, PreferencesDialog::ApplyMode::OnAccept, nullptr);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QList<int> edges;
    for (QFormLayout *form : d.findChildren<QFormLayout *>()) {
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem *item = form->itemAt(row, QFormLayout::FieldRole);
            if (item && item->widget())
                edges.append(item->widget()->mapTo(&d, QPoint(0, 0)).x());
        }
    }
    QCOMPARE(edges.size(), 5);
    for (const int e : std::as_const(edges))
        QCOMPARE(e, edges.first());
}

void TestPreferences::nothingCut()
{
    // M30 (ui-design 6.1 r8): every control has at least its size hint, and is inside the
    // dialog.
    Fixture f;
    PreferencesDialog *d = f.open();
    QVERIFY(QTest::qWaitForWindowExposed(d));
    QApplication::processEvents();
    for (QWidget *w : d->findChildren<QWidget *>()) {
        if (!w->isVisible() || !(qobject_cast<QCheckBox *>(w) || qobject_cast<QComboBox *>(w) ||
                                 qobject_cast<QSpinBox *>(w) || qobject_cast<QLabel *>(w)))
            continue;
        const QString what = w->objectName() + u' ' + w->metaObject()->className();
        QVERIFY2(w->width() >= w->sizeHint().width(),
                 qPrintable(QStringLiteral("%1 %2 < %3")
                                    .arg(what)
                                    .arg(w->width())
                                    .arg(w->sizeHint().width())));
        // Each parent clips its children: the control must be inside each of them.
        for (QWidget *p = w; p != d; p = p->parentWidget()) {
            const QRect inParent(p->mapTo(p->parentWidget(), QPoint()), p->size());
            QVERIFY2(p->parentWidget()->rect().contains(inParent), qPrintable(what));
        }
    }
}

QTEST_MAIN(TestPreferences)
#include "tst_app_preferences.moc"
