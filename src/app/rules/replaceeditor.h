// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/ruleeditor.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

namespace ctrm {
class ReplaceRule;
}

namespace ctrm::app {

class PendingTextEdit;

// The Replace rule editor (ui-design 8.2).
class ReplaceEditor : public RuleEditor
{
    Q_OBJECT
public:
    explicit ReplaceEditor(QWidget *parent);
    QString ruleType() const override;
    void setRule(ctrm::Rule *rule, int position) override;

signals:
    void syntaxHelpRequested(); // the "Syntax help" link; the rule panel triggers regexHelp

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateModeRows();
    void updateError();

    PendingTextEdit *m_pendingText;
    ctrm::ReplaceRule *m_rule = nullptr;
    bool m_loading = false;
    QLineEdit *m_find;
    QLabel *m_hint;
    QLineEdit *m_replace;
    QComboBox *m_mode;
    QComboBox *m_occurrence;
    QCheckBox *m_matchCase;
    QCheckBox *m_wholeWord;
    QComboBox *m_applyTo;
};

} // namespace ctrm::app
