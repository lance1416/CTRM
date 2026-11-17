// SPDX-License-Identifier: GPL-3.0-or-later
#include "presetcontroller.h"

#include "appcontext.h"
#include "dialogs/managepresetsdialog.h"
#include "dialogs/savepresetdialog.h"
#include "mainwindow.h"
#include "rules/ruleregistry.h"
#include "widgets/rulepanel.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QToolButton>

namespace ctrm::app {

namespace {
const QString kFileFilter = QStringLiteral("CTRM presets (*.ctrm.json);;JSON files (*.json)");
}

PresetController::PresetController(MainWindow &w, AppContext &ctx, QObject *parent)
    : QObject(parent)
    , m_window(w)
    , m_ctx(ctx)
    , m_menu(w.findChild<QMenu *>(QStringLiteral("presetsMenu")))
    , m_builtInMenu(nullptr)
{
    Q_ASSERT(m_menu);
    m_builtInMenu = new QMenu(tr("Built-in"), m_menu);
    m_builtInMenu->setObjectName(QStringLiteral("builtInPresetsMenu"));

    confirmReplaceRules = [this] {
        QMessageBox box(QMessageBox::Question, tr("Load Preset"), tr("Replace the current rules?"),
                        QMessageBox::Cancel, &m_window);
        QPushButton *replace = box.addButton(tr("Replace"), QMessageBox::AcceptRole);
        box.setDefaultButton(replace);
        box.setWindowModality(Qt::WindowModal);
        box.exec();
        return box.clickedButton() == replace;
    };
    askPresetName = [this](const QString &suggested) {
        QStringList userNames;
        for (const PresetEntry &e : m_ctx.presets().userPresets())
            userNames.append(e.name);
        QStringList builtInNames;
        for (const PresetEntry &e : m_ctx.presets().builtIns())
            builtInNames.append(e.name);
        SavePresetDialog dialog(userNames, builtInNames, suggested, &m_window);
        return dialog.exec() == QDialog::Accepted ? dialog.name() : QString();
    };
    askImportPath = [this] {
        return QFileDialog::getOpenFileName(&m_window, tr("Import Preset"), QString(), kFileFilter);
    };
    askExportPath = [this](const QString &suggested) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        return QFileDialog::getSaveFileName(&m_window, tr("Export Current Rules"),
                                            dir + QLatin1Char('/') + suggested, kFileFilter);
    };
    showError = [this](const QString &text) {
        QMessageBox::warning(&m_window, tr("Presets"), text);
    };
    runManageDialog = [](ManagePresetsDialog &dialog) { dialog.exec(); };

    const Actions &a = w.actions();
    connect(a.saveAsPreset, &QAction::triggered, this, &PresetController::saveAs);
    connect(a.updatePreset, &QAction::triggered, this, &PresetController::update);
    connect(a.managePresets, &QAction::triggered, this, &PresetController::managePresets);
    connect(a.importPreset, &QAction::triggered, this, &PresetController::importPreset);
    connect(a.exportRules, &QAction::triggered, this, &PresetController::exportRules);
    connect(m_menu, &QMenu::aboutToShow, this, &PresetController::rebuildMenu);
    connect(&w.ruleModel(), &RuleListModel::chainChanged, this, &PresetController::updateTitle);

    rebuildMenu();
    updateTitle();
}

QMenu *PresetController::menu() const
{
    return m_menu;
}

void PresetController::loadPreset(const PresetEntry &entry)
{
    const PresetParseResult result = m_ctx.presets().load(entry);
    if (canReplaceWith(result, entry.name))
        apply(*result.preset, entry);
}

bool PresetController::isModified() const
{
    return m_loaded && currentJson() != m_loadedJson;
}

void PresetController::rebuildMenu()
{
    // ui-design 11.1. The shared actions stay; the preset entries are made again.
    const Actions &a = m_window.actions();
    m_menu->clear();
    m_builtInMenu->clear();
    const bool modified = isModified();

    m_menu->addAction(a.saveAsPreset);
    a.updatePreset->setText(m_loaded ? tr("Update \"%1\"").arg(m_loaded->name)
                                     : tr("Update Preset"));
    a.updatePreset->setVisible(m_loaded && !m_loaded->builtIn && modified);
    m_menu->addAction(a.updatePreset);
    m_menu->addSeparator();

    const QList<PresetEntry> user = m_ctx.presets().userPresets();
    for (const PresetEntry &entry : user) {
        const bool current = m_loaded && !m_loaded->builtIn && m_loaded->filePath == entry.filePath;
        auto *action = new QAction(
                current && modified ? entry.name + QStringLiteral(" *") : entry.name, m_menu);
        connect(action, &QAction::triggered, this, [this, entry] { loadPreset(entry); });
        m_menu->addAction(action);
    }
    if (!user.isEmpty())
        m_menu->addSeparator();

    for (const PresetEntry &entry : m_ctx.presets().builtIns()) {
        auto *action = new QAction(entry.name, m_builtInMenu);
        connect(action, &QAction::triggered, this, [this, entry] { loadPreset(entry); });
        m_builtInMenu->addAction(action);
    }
    m_menu->addMenu(m_builtInMenu);
    m_menu->addSeparator();
    m_menu->addActions({a.managePresets, a.importPreset, a.exportRules});
}

void PresetController::saveAs()
{
    const QString name = askPresetName(m_loaded ? m_loaded->name : tr("My Rules"));
    if (name.isEmpty())
        return;
    Preset preset;
    preset.name = name;
    preset.chain = m_window.ruleModel().chain();
    // A user preset with the same name is replaced (audit S09); the dialog said so.
    QString path;
    for (const PresetEntry &e : m_ctx.presets().userPresets()) {
        if (e.name.compare(name, Qt::CaseInsensitive) == 0) {
            path = e.filePath;
            break;
        }
    }
    QString error;
    const std::optional<PresetEntry> entry = m_ctx.presets().save(preset, path, &error);
    if (!entry) {
        showError(error);
        return;
    }
    setLoaded(entry);
}

void PresetController::update()
{
    if (!m_loaded || m_loaded->builtIn)
        return;
    // Keep the description and the unknown keys of the file.
    PresetParseResult old = m_ctx.presets().load(*m_loaded);
    Preset preset = old.preset ? *old.preset : Preset{};
    preset.name = m_loaded->name;
    preset.chain = m_window.ruleModel().chain();
    QString error;
    const std::optional<PresetEntry> entry =
            m_ctx.presets().save(preset, m_loaded->filePath, &error);
    if (!entry) {
        showError(error);
        return;
    }
    setLoaded(entry);
}

void PresetController::importPreset()
{
    const QString path = askImportPath();
    if (path.isEmpty())
        return;
    // Parse and ask first, so a bad or cancelled file is not copied (AC14.4).
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        showError(tr("Cannot read the file: %1").arg(file.errorString()));
        return;
    }
    const PresetParseResult result = parsePreset(file.readAll());
    if (!canReplaceWith(result, QFileInfo(path).fileName()))
        return;
    QString error;
    const std::optional<PresetEntry> entry = m_ctx.presets().importFile(path, &error);
    if (!entry) {
        showError(error);
        return;
    }
    apply(*result.preset, *entry);
}

void PresetController::exportRules()
{
    Preset preset;
    preset.name = m_loaded ? m_loaded->name : tr("Exported Rules");
    preset.chain = m_window.ruleModel().chain();
    const QString path = askExportPath(preset.name + QStringLiteral(".ctrm.json"));
    if (path.isEmpty())
        return;
    QString error;
    if (!m_ctx.presets().exportTo(preset, path, &error))
        showError(error);
}

void PresetController::managePresets()
{
    ManagePresetsDialog dialog(m_ctx.presets(), &m_window);
    connect(&dialog, &ManagePresetsDialog::presetRenamed, this,
            [this](const PresetEntry &from, const PresetEntry &to) {
                if (m_loaded && !m_loaded->builtIn && m_loaded->filePath == from.filePath) {
                    m_loaded = to;
                    updateTitle();
                }
            });
    connect(&dialog, &ManagePresetsDialog::presetRemoved, this, [this](const PresetEntry &e) {
        // The rules stay; they are no longer a saved preset.
        if (m_loaded && !m_loaded->builtIn && m_loaded->filePath == e.filePath)
            setLoaded(std::nullopt);
    });
    runManageDialog(dialog);
}

bool PresetController::canReplaceWith(const PresetParseResult &result, const QString &name)
{
    if (!result.preset) {
        showError(result.error);
        return false;
    }
    for (const QString &warning : result.warnings)
        qWarning("Preset \"%s\": %s", qUtf8Printable(name), qUtf8Printable(warning));
    // Ask only when the current rules are not empty and are not a saved preset (ui-design 11.1).
    const bool unsaved = m_window.ruleModel().rowCount() > 0 && (!m_loaded || isModified());
    return !unsaved || confirmReplaceRules();
}

void PresetController::apply(const Preset &preset, const PresetEntry &entry)
{
    m_window.ruleModel().setChain(preset.chain);
    setLoaded(entry);
}

QJsonObject PresetController::currentJson() const
{
    const RuleChain &chain = m_window.ruleModel().chain();
    return QJsonObject{{QStringLiteral("rules"), chainRulesToJson(chain)},
                       {QStringLiteral("options"), chainOptionsToJson(chain.options)}};
}

void PresetController::setLoaded(const std::optional<PresetEntry> &entry)
{
    m_loaded = entry;
    m_loadedJson = entry ? currentJson() : QJsonObject();
    updateTitle();
}

void PresetController::updateTitle()
{
    m_window.setPresetTitle(m_loaded ? m_loaded->name : QString(), isModified());
}

} // namespace ctrm::app
