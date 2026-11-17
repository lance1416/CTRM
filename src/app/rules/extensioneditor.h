// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/ruleeditor.h"

class QButtonGroup;
class QLabel;
class QLineEdit;

namespace ctrm {
class ExtensionRule;
}

namespace ctrm::app {

class PendingTextEdit;

// The Extension rule editor (ui-design 8.7). It has no "Apply to" row: the target is always the
// extension.
class ExtensionEditor : public RuleEditor
{
    Q_OBJECT
public:
    explicit ExtensionEditor(QWidget *parent);
    QString ruleType() const override;
    void setRule(ctrm::Rule *rule, int position) override;
    // The "Treat folder names as name + extension" chain option. On shows a note.
    void setFolderExtensions(bool on);

private:
    void updateEnabled();

    PendingTextEdit *m_pendingText;
    ctrm::ExtensionRule *m_rule = nullptr;
    bool m_loading = false;
    QButtonGroup *m_actions;
    QLineEdit *m_setValue;
    QLineEdit *m_addValue;
    QLineEdit *m_onlyIfMatches;
    QLabel *m_onlyForHint;
    QLabel *m_folderNote;
};

} // namespace ctrm::app
