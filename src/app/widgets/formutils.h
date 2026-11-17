// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>

class QComboBox;
class QFormLayout;
class QLabel;
class QToolButton;
class QWidget;

namespace ctrm::app {

// A form of ui-design 6.1: labels top-aligned (horizontal part from the style), text fields grow.
// When parent is not nullptr, the form is its layout.
QFormLayout *makeForm(QWidget *parent);

// A radio or check box group as one field: a grid, margins 0, spacing from the style. The
// sub-fields of the inline fields in controls start on one line; the group takes their widgets
// and deletes the inline field widgets.
QWidget *makeFieldGroup(const QList<QWidget *> &controls);

// A control with its sub-fields on one line: QHBoxLayout, margins 0.
QWidget *makeInlineField(QWidget *first, const QList<QWidget *> &more);

// Makes the label baseline the same as the text baseline of firstControl (ui-design 6.2).
// It applies again when the font or the style of the label or the control changes.
void alignLabelToFirstRow(QLabel *label, QWidget *firstControl);

// Gives the label columns of all forms the width of the widest label (ui-design 6.1 rule 9).
class LabelColumnSync : public QObject
{
public:
    explicit LabelColumnSync(QObject *parent = nullptr);
    void addForm(QFormLayout *form);
    void sync();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QList<QPointer<QFormLayout>> m_forms;
    bool m_syncPending = false;
};

// A "More options" button in the field column. It shows and hides the rows it holds.
class MoreOptions : public QObject
{
public:
    // Adds the button as a new row of form, with an empty label. The rows are closed.
    MoreOptions(QFormLayout *form, const QString &text, QWidget *parent);
    // Holds the row by its field widget, so rows that are inserted later do not change it.
    void addRow(int row);
    QToolButton *button() const;
    void setOpen(bool open);

private:
    QPointer<QFormLayout> m_form;
    QToolButton *m_button;
    QList<QPointer<QWidget>> m_rows;
};

// The "Apply to" combo box. Item data: "stem", "extension", "name".
QComboBox *makeApplyToCombo(QWidget *parent);

} // namespace ctrm::app
