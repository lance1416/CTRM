// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/ruleeditor.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace ctrm {
class NumberingRule;
}

namespace ctrm::app {

class PendingTextEdit;

// The Numbering rule editor (ui-design 8.6).
class NumberingEditor : public RuleEditor
{
    Q_OBJECT
public:
    explicit NumberingEditor(QWidget *parent);
    QString ruleType() const override;
    void setRule(ctrm::Rule *rule, int position) override;
    // The number of items that get a number. The example uses it for Auto padding.
    void setNumberedCount(int n);

private:
    void updateExample();

    PendingTextEdit *m_pendingText;
    ctrm::NumberingRule *m_rule = nullptr;
    bool m_loading = false;
    int m_numberedCount = 1;
    QSpinBox *m_start;
    QSpinBox *m_step;
    QComboBox *m_padding;
    QLabel *m_example;
    QButtonGroup *m_positions;
    QSpinBox *m_index;
    QCheckBox *m_fromEnd;
    QLineEdit *m_separator;
    QComboBox *m_scope;
    QComboBox *m_applyTo;
    QLabel *m_orderNote;
};

} // namespace ctrm::app
