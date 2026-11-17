// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "presets/presetstore.h"

#include <QDialog>

#include <functional>

class QLabel;
class QListWidget;
class QPushButton;

namespace ctrm::app {

// Manage Presets (ui-design 11.2, audit S08): the "My Presets" group, then the "Built-in"
// group, each under a dimmed header. Built-ins are read-only: only Duplicate works for them.
class ManagePresetsDialog : public QDialog
{
    Q_OBJECT
public:
    ManagePresetsDialog(ctrm::PresetStore &store, QWidget *parent);

    // The new name for Rename; empty = cancel. The default is a QInputDialog.
    std::function<QString(const QString &current)> askName;
    // "Delete the preset ...?" The default is a QMessageBox.
    std::function<bool(const QString &name)> confirmDelete;
    // An error of the store. The default is QMessageBox::warning.
    std::function<void(const QString &text)> showError;

signals:
    void presetRenamed(const ctrm::PresetEntry &from, const ctrm::PresetEntry &to);
    void presetRemoved(const ctrm::PresetEntry &entry);

private:
    void reload(const QString &selectPath);
    void updateSelection();
    std::optional<ctrm::PresetEntry> selected() const;
    void rename();
    void duplicate();
    void remove();
    void showFile();

    ctrm::PresetStore &m_store;
    QList<ctrm::PresetEntry> m_entries;
    QListWidget *m_list;
    QLabel *m_rules;
    QPushButton *m_rename;
    QPushButton *m_duplicate;
    QPushButton *m_delete;
    QPushButton *m_showFile;
};

} // namespace ctrm::app
