// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/ruleeditor.h"

#include <QList>

#include <utility>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace ctrm {
class RemoveRule;
}

namespace ctrm::app {

class PendingTextEdit;

// The Remove rule editor (ui-design 8.4). The rows of the three modes are in one form.
class RemoveEditor : public RuleEditor
{
    Q_OBJECT
public:
    explicit RemoveEditor(QWidget *parent);
    QString ruleType() const override;
    void setRule(ctrm::Rule *rule, int position) override;

private:
    void updateModeRows();
    void writeClasses();
    // A slot for a control: a user change runs write(rule, value), then emits ruleEdited().
    template <typename Write>
    auto edit(Write write);

    PendingTextEdit *m_pendingText;
    ctrm::RemoveRule *m_rule = nullptr;
    bool m_loading = false;
    QComboBox *m_mode;
    QSpinBox *m_from;
    QCheckBox *m_rangeFromEnd;
    QSpinBox *m_count;
    QWidget *m_classesGroup;
    QList<std::pair<QCheckBox *, quint32>> m_classes;
    QLineEdit *m_customText;
    QLineEdit *m_open;
    QLineEdit *m_close;
    QComboBox *m_occurrence;
    QCheckBox *m_keepDelimiters;
    QComboBox *m_applyTo;
};

} // namespace ctrm::app
