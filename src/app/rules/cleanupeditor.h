// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/ruleeditor.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

namespace ctrm {
class CleanUpRule;
}

namespace ctrm::app {

class PendingTextEdit;

// The Clean up rule editor (ui-design 8.8).
class CleanUpEditor : public RuleEditor
{
    Q_OBJECT
public:
    explicit CleanUpEditor(QWidget *parent);
    QString ruleType() const override;
    void setRule(ctrm::Rule *rule, int position) override;
    // The "Use portable names" chain option. It sets the note under "Replace invalid characters".
    void setPortableNames(bool on);

private:
    PendingTextEdit *m_pendingText;
    ctrm::CleanUpRule *m_rule = nullptr;
    bool m_loading = false;
    QCheckBox *m_trim;
    QCheckBox *m_collapseSpaces;
    QComboBox *m_spacesTo;
    QCheckBox *m_replaceInvalid;
    QLineEdit *m_replacement;
    QLabel *m_portableNote;
    QComboBox *m_applyTo;
};

} // namespace ctrm::app
