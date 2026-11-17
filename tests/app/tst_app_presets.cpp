// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"
#include "controllers/itemadder.h"
#include "controllers/presetcontroller.h"
#include "dialogs/managepresetsdialog.h"
#include "fs/filesystem.h"
#include "mainwindow.h"
#include "models/rulelistmodel.h"
#include "presets/presetstore.h"
#include "rules/changecaserule.h"
#include "rules/numberingrule.h"
#include "rules/replacerule.h"
#include "rules/ruleregistry.h"
#include "temptree.h"
#include "widgets/actionbar.h"
#include "widgets/rulepanel.h"

#include <QAction>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

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
        , presets(window, ctx, &window)
    {
        window.itemAdder().askFolderDrop = [](const FolderDropQuestion &) {
            return FolderDropReply{FolderDropReply::Choice::AddContents, false, false};
        };
        presets.confirmReplaceRules = [this] {
            ++confirmCalls;
            return confirmAnswer;
        };
        presets.askPresetName = [this](const QString &) { return nextName; };
        presets.showError = [this](const QString &text) { errors.append(text); };
    }

    QMenu *menu()
    {
        QMenu *m = presets.menu();
        emit m->aboutToShow();
        return m;
    }

    QStringList menuTexts()
    {
        QStringList texts;
        for (QAction *a : menu()->actions()) {
            if (a->isVisible())
                texts.append(a->isSeparator() ? QStringLiteral("---") : a->text());
        }
        return texts;
    }

    QAction *menuAction(const QString &text)
    {
        for (QAction *a : menu()->actions()) {
            if (a->text() == text)
                return a;
        }
        return nullptr;
    }

    QJsonObject chainJson()
    {
        const RuleChain &chain = window.ruleModel().chain();
        return QJsonObject{{"rules", chainRulesToJson(chain)},
                           {"options", chainOptionsToJson(chain.options)}};
    }

    void saveAs(const QString &name)
    {
        nextName = name;
        window.actions().saveAsPreset->trigger();
    }

    QString writeFile(const QString &name, const QByteArray &bytes)
    {
        return tree.file(name, bytes);
    }

    QTemporaryDir dir;
    TempTree tree;
    QSettings qsettings;
    AppContext ctx;
    MainWindow window;
    PresetController presets;
    int confirmCalls = 0;
    bool confirmAnswer = true;
    QString nextName;
    QStringList errors;
};

RuleChain threeRules()
{
    RuleChain chain;
    auto replace = std::make_unique<ReplaceRule>();
    replace->find = QStringLiteral("IMG_");
    chain.insert(0, std::move(replace));
    auto caseRule = std::make_unique<ChangeCaseRule>();
    caseRule->caseMode = ChangeCaseRule::Case::Title;
    chain.insert(1, std::move(caseRule));
    auto numbering = std::make_unique<NumberingRule>();
    numbering->padding = 3;
    chain.insert(2, std::move(numbering));
    chain.options.autoResolveDuplicates = true;
    chain.options.portableNames = false;
    return chain;
}

QByteArray presetBytes(int formatVersion, const QString &type)
{
    const QString text = QStringLiteral(R"({"format": "ctrm-preset", "formatVersion": %1, )") +
                         QStringLiteral(R"("name": "Odd", "rules": [{"type": "%2"}]})");
    return text.arg(formatVersion).arg(type).toUtf8();
}

} // namespace

class TestAppPresets : public QObject
{
    Q_OBJECT
private slots:
    void menuLayout();
    void presetsButtonHasMenu();
    void saveThenLoadIdentical();
    void builtInsInMenu();
    void loadAsksWhenUnsaved();
    void newerVersionError();
    void unsupportedRuleBlocks();
    void titleShowsModified();
    void updateEntryOnlyWhenChanged();
    void exportCurrentRules();
    void builtInReadOnly();
    void manageRenameDeleteDuplicate();
    void controllerFollowsManage();
};

void TestAppPresets::menuLayout()
{
    Fixture f;
    QCOMPARE(f.menuTexts(),
             (QStringList{"Save Current Rules as Preset...", "---", "Built-in", "---",
                          "Manage Presets...", "Import Preset...", "Export Current Rules..."}));
    // Rebuilds keep the shared actions and the Built-in submenu.
    f.menu();
    f.menu();
    QCOMPARE(f.menuTexts().size(), 7);
    QAction *builtIn = f.menuAction(QStringLiteral("Built-in"));
    QVERIFY(builtIn && builtIn->menu() && builtIn->menu()->parent() == f.presets.menu());
    QAction *save = f.window.actions().saveAsPreset;
    QCOMPARE(save->shortcut(), QKeySequence(QStringLiteral("Ctrl+S")));
    QVERIFY(f.presets.menu()->actions().contains(save));
    QCOMPARE(f.window.findChildren<QAction *>(QStringLiteral("saveAsPreset")).size(), 1);
}

void TestAppPresets::presetsButtonHasMenu()
{
    Fixture f;
    QCOMPARE(f.window.rulePanel()->presetsButton()->menu(), f.presets.menu());
}

void TestAppPresets::saveThenLoadIdentical()
{
    Fixture f;
    f.window.ruleModel().setChain(threeRules());
    const QJsonObject saved = f.chainJson();
    f.saveAs(QStringLiteral("Photo Trip"));
    QCOMPARE(f.errors, QStringList());
    QCOMPARE(f.ctx.presets().userPresets().size(), 1);
    QCOMPARE(f.ctx.presets().userPresets().first().name, QStringLiteral("Photo Trip"));
    QVERIFY(!f.presets.isModified());

    f.window.ruleModel().setChain(RuleChain{});
    QAction *load = f.menuAction(QStringLiteral("Photo Trip *")); // loaded, changed
    QVERIFY(load);
    load->trigger();
    QCOMPARE(f.chainJson(), saved);
    QCOMPARE(f.confirmCalls, 0); // the rules were empty
    QVERIFY(!f.presets.isModified());
}

void TestAppPresets::builtInsInMenu()
{
    Fixture f;
    auto *builtIn = f.menuAction(QStringLiteral("Built-in"));
    QVERIFY(builtIn && builtIn->menu());
    QStringList names;
    for (QAction *a : builtIn->menu()->actions())
        names.append(a->text());
    QCOMPARE(names.size(), 4);
    for (const char *name : {"Lower-case extensions", "Spaces to underscores",
                             "Photo numbering (IMG_0001)", "Remove [...] tags"})
        QVERIFY2(names.contains(QLatin1StringView(name)), name);
    // A second rebuild does not add entries.
    f.menu();
    QCOMPARE(f.menuAction(QStringLiteral("Built-in"))->menu()->actions().size(), 4);

    builtIn->menu()->actions().first()->trigger();
    QVERIFY(f.window.ruleModel().rowCount() > 0);
    QVERIFY(f.window.windowTitle().contains(builtIn->menu()->actions().first()->text()));
}

void TestAppPresets::loadAsksWhenUnsaved()
{
    Fixture f;
    f.window.ruleModel().setChain(threeRules());
    const QJsonObject before = f.chainJson();
    const PresetEntry entry = f.ctx.presets().builtIns().first();
    f.confirmAnswer = false;
    f.presets.loadPreset(entry);
    QCOMPARE(f.confirmCalls, 1);
    QCOMPARE(f.chainJson(), before);

    f.confirmAnswer = true;
    f.presets.loadPreset(entry);
    QCOMPARE(f.confirmCalls, 2);
    QVERIFY(f.chainJson() != before);

    // The rules are equal to the loaded preset: no question.
    f.presets.loadPreset(f.ctx.presets().builtIns().at(1));
    QCOMPARE(f.confirmCalls, 2);

    // The loaded preset is changed: the question comes again.
    f.window.ruleModel().setEnabled(0, false);
    f.confirmAnswer = false;
    const QJsonObject changed = f.chainJson();
    f.presets.loadPreset(f.ctx.presets().builtIns().at(2));
    QCOMPARE(f.confirmCalls, 3);
    QCOMPARE(f.chainJson(), changed);

    // A cancelled import does not copy the file.
    const QString path = f.writeFile(QStringLiteral("ok.ctrm.json"), presetBytes(1, "replace"));
    f.presets.askImportPath = [path] { return path; };
    f.window.actions().importPreset->trigger();
    QCOMPARE(f.confirmCalls, 4);
    QCOMPARE(f.ctx.presets().userPresets().size(), 0);
    QCOMPARE(f.chainJson(), changed);
}

void TestAppPresets::newerVersionError()
{
    Fixture f;
    f.window.ruleModel().setChain(threeRules());
    const QJsonObject before = f.chainJson();
    const QString path = f.writeFile(QStringLiteral("new.ctrm.json"), presetBytes(99, "replace"));
    f.presets.askImportPath = [path] { return path; };
    f.window.actions().importPreset->trigger();
    QCOMPARE(f.errors.size(), 1);
    QVERIFY2(f.errors.first().contains(QStringLiteral("newer CTRM")), qPrintable(f.errors.first()));
    QCOMPARE(f.chainJson(), before);
    QCOMPARE(f.ctx.presets().userPresets().size(), 0);
    QCOMPARE(f.confirmCalls, 0);
}

void TestAppPresets::unsupportedRuleBlocks()
{
    Fixture f;
    f.window.show();
    f.window.itemAdder().addPaths({f.tree.file(QStringLiteral("a.txt"), "x")});
    const QString path = f.writeFile(QStringLiteral("odd.ctrm.json"), presetBytes(1, "warp"));
    f.presets.askImportPath = [path] { return path; };
    f.window.actions().importPreset->trigger();
    QVERIFY2(f.errors.isEmpty(), qPrintable(f.errors.join(QLatin1Char('\n'))));
    QCOMPARE(f.window.ruleModel().rowCount(), 1);
    QCOMPARE(f.window.ruleModel().index(0).data(RuleListModel::SummaryRole).toString(),
             QStringLiteral("Unsupported rule (warp)"));
    QVERIFY(!f.window.actionBar()->renameButton()->isEnabled());
    QVERIFY(f.window.findChild<QLabel *>(QStringLiteral("renameReason"))
                    ->text()
                    .contains(QStringLiteral("block")));
}

void TestAppPresets::titleShowsModified()
{
    Fixture f;
    f.window.ruleModel().setChain(threeRules());
    f.saveAs(QStringLiteral("Photo Trip"));
    QVERIFY(f.window.windowTitle().contains(QStringLiteral("Photo Trip")));
    QVERIFY(!f.window.isWindowModified());

    f.window.rulePanel()->ruleList()->setCurrentIndex(f.window.ruleModel().index(2));
    f.window.rulePanel()
            ->currentEditor()
            ->findChild<QSpinBox *>(QStringLiteral("start"))
            ->setValue(4);
    QVERIFY(f.window.windowTitle().contains(QStringLiteral("Photo Trip")));
    QVERIFY(f.window.isWindowModified());
    QVERIFY(f.presets.isModified());
    QVERIFY(f.menuAction(QStringLiteral("Photo Trip *")));

    f.window.rulePanel()
            ->currentEditor()
            ->findChild<QSpinBox *>(QStringLiteral("start"))
            ->setValue(1);
    QVERIFY(!f.window.isWindowModified());
}

void TestAppPresets::updateEntryOnlyWhenChanged()
{
    Fixture f;
    const QString update = QStringLiteral("Update \"Photo Trip\"");
    QVERIFY(!f.menuTexts().contains(update));
    f.window.ruleModel().setChain(threeRules());
    f.saveAs(QStringLiteral("Photo Trip"));
    QVERIFY(!f.menuTexts().contains(update));

    f.window.ruleModel().setEnabled(0, false);
    QCOMPARE(f.menuTexts().at(1), update);
    f.menuAction(update)->trigger();
    QVERIFY(!f.presets.isModified());
    QVERIFY(!f.menuTexts().contains(update));
    const PresetParseResult saved = f.ctx.presets().load(f.ctx.presets().userPresets().first());
    QVERIFY(saved.preset);
    QVERIFY(!saved.preset->chain.at(0)->enabled);

    // A built-in preset is read-only: no Update entry.
    f.presets.loadPreset(f.ctx.presets().builtIns().first());
    f.window.ruleModel().setEnabled(0, false);
    QVERIFY(f.presets.isModified());
    QVERIFY(!f.menuTexts().join(QLatin1Char('|')).contains(QStringLiteral("Update")));
}

void TestAppPresets::exportCurrentRules()
{
    Fixture f;
    f.window.ruleModel().setChain(threeRules());
    const QString path = f.tree.root() + QStringLiteral("/out.ctrm.json");
    f.presets.askExportPath = [path](const QString &) { return path; };
    f.window.actions().exportRules->trigger();
    QCOMPARE(f.errors, QStringList());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const PresetParseResult parsed = parsePreset(file.readAll());
    QVERIFY(parsed.preset);
    QCOMPARE(chainRulesToJson(parsed.preset->chain), chainRulesToJson(threeRules()));
}

void TestAppPresets::builtInReadOnly()
{
    Fixture f;
    ManagePresetsDialog dialog(f.ctx.presets(), nullptr);
    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("presetList"));
    QVERIFY(list);
    QCOMPARE(list->count(), 4);
    QVERIFY(list->item(0)->text().endsWith(QStringLiteral(" (B)")));
    list->setCurrentRow(0);
    QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("renameButton"))->isEnabled());
    QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("deleteButton"))->isEnabled());
    QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("showFileButton"))->isEnabled());
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("duplicateButton"))->isEnabled());
    auto *rules = dialog.findChild<QLabel *>(QStringLiteral("ruleSummaries"));
    QVERIFY(rules && !rules->text().isEmpty());
    QVERIFY(rules->text().startsWith(QStringLiteral("1 ")));
    auto *close = dialog.findChild<QPushButton *>(QStringLiteral("closeButton"));
    QVERIFY(close && close->isDefault());
}

void TestAppPresets::manageRenameDeleteDuplicate()
{
    Fixture f;
    f.window.ruleModel().setChain(threeRules());
    f.saveAs(QStringLiteral("Photo Trip"));
    ManagePresetsDialog dialog(f.ctx.presets(), nullptr);
    QSignalSpy renamed(&dialog, &ManagePresetsDialog::presetRenamed);
    QSignalSpy removed(&dialog, &ManagePresetsDialog::presetRemoved);
    dialog.askName = [](const QString &current) { return current + QStringLiteral(" 2"); };
    dialog.confirmDelete = [](const QString &) { return true; };
    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("presetList"));
    QCOMPARE(list->count(), 5);
    QCOMPARE(list->item(0)->text(), QStringLiteral("Photo Trip"));
    list->setCurrentRow(0);
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("renameButton"))->isEnabled());

    dialog.findChild<QPushButton *>(QStringLiteral("renameButton"))->click();
    QCOMPARE(renamed.count(), 1);
    QCOMPARE(f.ctx.presets().userPresets().first().name, QStringLiteral("Photo Trip 2"));
    QCOMPARE(list->currentItem()->text(), QStringLiteral("Photo Trip 2"));

    list->setCurrentRow(list->count() - 1); // a built-in
    dialog.findChild<QPushButton *>(QStringLiteral("duplicateButton"))->click();
    QCOMPARE(f.ctx.presets().userPresets().size(), 2);
    QCOMPARE(list->count(), 6);

    const QString name = QStringLiteral("Photo Trip 2");
    list->setCurrentItem(list->findItems(name, Qt::MatchExactly).first());
    dialog.findChild<QPushButton *>(QStringLiteral("deleteButton"))->click();
    QCOMPARE(removed.count(), 1);
    QCOMPARE(f.ctx.presets().userPresets().size(), 1);
    QVERIFY(list->findItems(name, Qt::MatchExactly).isEmpty());
    QVERIFY(f.ctx.presets().userPresets().first().name != name);
}

void TestAppPresets::controllerFollowsManage()
{
    Fixture f;
    f.window.ruleModel().setChain(threeRules());
    f.saveAs(QStringLiteral("Photo Trip"));
    const auto selectByText = [](QListWidget *list, const QString &text) {
        const QList<QListWidgetItem *> items = list->findItems(text, Qt::MatchExactly);
        QVERIFY(items.size() == 1);
        list->setCurrentItem(items.first());
    };

    f.presets.runManageDialog = [&](ManagePresetsDialog &dialog) {
        dialog.askName = [](const QString &) { return QStringLiteral("Beach"); };
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("presetList"));
        selectByText(list, QStringLiteral("Photo Trip"));
        dialog.findChild<QPushButton *>(QStringLiteral("renameButton"))->click();
    };
    f.window.actions().managePresets->trigger();
    QCOMPARE(f.window.windowTitle(), QStringLiteral("CTRM - Beach[*]"));
    QVERIFY(!f.presets.isModified());
    f.window.ruleModel().setEnabled(0, false);
    QVERIFY(f.menuTexts().contains(QStringLiteral("Update \"Beach\"")));

    f.presets.runManageDialog = [&](ManagePresetsDialog &dialog) {
        dialog.confirmDelete = [](const QString &) { return true; };
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("presetList"));
        selectByText(list, QStringLiteral("Beach"));
        dialog.findChild<QPushButton *>(QStringLiteral("deleteButton"))->click();
    };
    f.window.actions().managePresets->trigger();
    QCOMPARE(f.ctx.presets().userPresets().size(), 0);
    QCOMPARE(f.window.windowTitle(), QStringLiteral("CTRM"));
    QVERIFY(!f.menuTexts().join(QLatin1Char('|')).contains(QStringLiteral("Update")));
    QCOMPARE(f.window.ruleModel().rowCount(), 3); // the rules stay
}

QTEST_MAIN(TestAppPresets)
#include "tst_app_presets.moc"
