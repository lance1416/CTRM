// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QFormLayout;
class QLabel;
class QLineEdit;

namespace ctrm {
class Rule;
}

namespace ctrm::app {

// The base of all rule editor pages: a title ("Rule 2: Replace", Heading style) above a form.
class RuleEditor : public QWidget
{
    Q_OBJECT
public:
    explicit RuleEditor(QWidget *parent);
    virtual QString ruleType() const = 0;
    virtual void setRule(ctrm::Rule *rule, int position) = 0; // position is 1-based for the title
    QFormLayout *form() const;
    // Shows text in an error row under the field of row, in the field column. Empty text hides
    // the error row. The error row is not shown while the row of the field is hidden.
    void showFieldError(int row, const QString &text);

signals:
    void ruleEdited(); // after 150 ms for text fields, at once for others

protected:
    void setTitle(int position, const QString &displayName);
    // User edits in field restart the 150 ms timer before ruleEdited(). setText() from code
    // does not start it.
    void watchTextField(QLineEdit *field);
    // Stops a pending text edit without ruleEdited(). Call it at the start of setRule().
    void cancelPendingEdit();
    // Emits ruleEdited() now. A pending text change is part of this signal.
    void emitEdited();
    void changeEvent(QEvent *event) override;
    bool event(QEvent *event) override;

private:
    // Combo boxes alone on their form row get the widest size hint of them (ui-design 6.1
    // rule 8, M25).
    void matchComboWidths();

    void updateFonts();

    QLabel *m_title;
    QFormLayout *m_form;
    QTimer m_textTimer;
    QHash<QWidget *, QPointer<QWidget>> m_errorRows; // field -> error row
};

} // namespace ctrm::app
