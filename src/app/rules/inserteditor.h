// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/ruleeditor.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace ctrm {
class InsertRule;
}

namespace ctrm::app {

class PendingTextEdit;

// The Insert rule editor (ui-design 8.3).
class InsertEditor : public RuleEditor
{
    Q_OBJECT
public:
    explicit InsertEditor(QWidget *parent);
    QString ruleType() const override;
    void setRule(ctrm::Rule *rule, int position) override;

private:
    void writePosition();
    void updateEnabled();

    PendingTextEdit *m_pendingText;
    ctrm::InsertRule *m_rule = nullptr;
    bool m_loading = false;
    QLineEdit *m_text;
    QRadioButton *m_prefix;
    QRadioButton *m_suffix;
    QRadioButton *m_atIndex;
    QSpinBox *m_index;
    QCheckBox *m_fromEnd;
    QComboBox *m_applyTo;
};

} // namespace ctrm::app
