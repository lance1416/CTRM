// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "presets/presetstore.h"

#include <QJsonObject>
#include <QObject>

#include <functional>
#include <optional>

class QMenu;

namespace ctrm::app {

class AppContext;
class MainWindow;
class ManagePresetsDialog;

// The Presets menu and Manage Presets (ui-design 11). The menu is the Presets menu of the menu
// bar; the toolbar Presets button and the "or Presets" button of the rule panel show it too.
class PresetController : public QObject
{
    Q_OBJECT
public:
    PresetController(MainWindow &w, AppContext &ctx, QObject *parent);

    std::function<bool()> confirmReplaceRules; // "Replace the current rules?" [Cancel] [Replace]
    std::function<QString(const QString &suggested)>
            askPresetName; // SavePresetDialog; empty = cancel
    std::function<QString()> askImportPath;
    std::function<QString(const QString &suggested)> askExportPath;
    // An error for the user (a bad preset file, a failed save). The default is
    // QMessageBox::warning.
    std::function<void(const QString &text)> showError;
    // Shows Manage Presets. The default is QDialog::exec().
    std::function<void(ManagePresetsDialog &dialog)> runManageDialog;

    QMenu *menu() const; // toolbar Presets button and Presets menu bar menu
    void loadPreset(const ctrm::PresetEntry &entry);
    bool isModified() const; // current chain JSON != loaded preset JSON

private:
    void rebuildMenu();
    void saveAs();
    void update();
    void importPreset();
    void exportRules();
    void managePresets();
    // Shows the error of a bad preset, logs its warnings and asks before the current rules go.
    bool canReplaceWith(const ctrm::PresetParseResult &result, const QString &name);
    void apply(const ctrm::Preset &preset, const ctrm::PresetEntry &entry);
    QJsonObject currentJson() const;
    void setLoaded(const std::optional<ctrm::PresetEntry> &entry);
    void updateTitle();

    MainWindow &m_window;
    AppContext &m_ctx;
    QMenu *m_menu;
    QMenu *m_builtInMenu;
    std::optional<ctrm::PresetEntry> m_loaded;
    QJsonObject m_loadedJson;
};

} // namespace ctrm::app
