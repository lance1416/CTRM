// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QStringList>

class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

namespace ctrm::app {

// Save Preset (ui-design 11.3): "Preset name:" with Cancel and Save. Save is off while the name is
// empty. For the name of a user preset, a dimmed note shows and Save becomes Replace. A built-in
// name cannot be used: a note says so and Save is off.
class SavePresetDialog : public QDialog
{
    Q_OBJECT
public:
    SavePresetDialog(const QStringList &userNames, const QStringList &builtInNames,
                     const QString &suggested, QWidget *parent);

    QString name() const; // trimmed

private:
    void updateState();

    QStringList m_userNames;
    QStringList m_builtInNames;
    QLineEdit *m_edit;
    QFormLayout *m_form;
    QLabel *m_note;
    QPushButton *m_save;
};

} // namespace ctrm::app
