// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/ruleeditor.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QToolButton;

namespace ctrm {
class ChangeCaseRule;
}

namespace ctrm::app {

class PendingTextEdit;

// The Change case rule editor (ui-design 8.5).
class ChangeCaseEditor : public RuleEditor
{
    Q_OBJECT
public:
    explicit ChangeCaseEditor(QWidget *parent);
    QString ruleType() const override;
    void setRule(ctrm::Rule *rule, int position) override;

private:
    void updateTitleRows();

    PendingTextEdit *m_pendingText;
    ctrm::ChangeCaseRule *m_rule = nullptr;
    bool m_loading = false;
    QButtonGroup *m_cases;
    QComboBox *m_applyTo;
    QToolButton *m_more;
    QLineEdit *m_separators;
    QLineEdit *m_smallWords;
    QCheckBox *m_builtInSmallWords;
};

} // namespace ctrm::app
