// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace ctrm::app {

class Settings;

// The Preferences form (ui-design 16): General, Preview and History.
class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    // Immediately: each change is saved at once, no buttons (macOS). OnAccept: OK saves, Cancel
    // keeps the saved values (Windows and Linux).
    enum class ApplyMode { Immediately, OnAccept };
    static ApplyMode platformMode();

    PreferencesDialog(Settings &settings, ApplyMode mode, QWidget *parent);

    // Sets the controls from the settings.
    void load();

    // Sets only the highlight check box from the settings (the View menu item changed).
    void syncHighlight();

    void accept() override;         // OnAccept: saves, then closes
    void done(int result) override; // load(), so Cancel leaves the saved values in the controls

signals:
    // The settings were saved.
    void settingsChanged();

private:
    void save();
    void controlChanged();

    Settings &m_settings;
    ApplyMode m_mode;
    bool m_loading = false;
    QComboBox *m_folderDrop;
    QCheckBox *m_includeHidden;
    QCheckBox *m_highlight;
    QSpinBox *m_keepCount;
    QSpinBox *m_keepDays;
};

} // namespace ctrm::app
