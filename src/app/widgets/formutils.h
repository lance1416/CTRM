// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLayout;
class QToolButton;
class QWidget;

namespace ctrm::app {

// A form of ui-design 6.1: labels top-aligned (horizontal part from the style), text fields grow.
// When parent is not nullptr, the form is its layout.
QFormLayout *makeForm(QWidget *parent);

// A radio or check box group as one field: a grid, margins 0, spacing from the style. The
// sub-fields of the inline fields in controls start on one line; the group takes their widgets
// and deletes the inline field widgets. It is a layout, not a widget: the visible edge of its
// controls is the field edge also on a style that draws a control inside its widget (macOS).
// Add it to a form with QFormLayout::addRow; the form becomes the parent of its widgets.
QLayout *makeFieldGroup(const QList<QWidget *> &controls);

// A control with its sub-fields on one line: QHBoxLayout, margins 0.
QWidget *makeInlineField(QWidget *first, const QList<QWidget *> &more);

// A check box with a label that wraps: an empty QCheckBox and a word-wrapped QLabel with text,
// on one line (QHBoxLayout, margins 0). The first text line is centred on the box. A click on the
// label changes the check box; the check box has text as its accessible name. A long label then
// never makes its column wider (ui-design 4.1, 8.10).
QWidget *makeWrappingCheckBox(QCheckBox *check, const QString &text);

// Makes the label baseline the same as the text baseline of firstControl (ui-design 6.2).
// It applies again when the font or the style of the label or the control changes.
void alignLabelToFirstRow(QLabel *label, QWidget *firstControl);

// The vertical rhythm, spacing and field widths of a form (ui-design 6.1 rules 2, 8, 14 and 15;
// design-tokens 4). Call it after the rows are added. It applies again on a font or style change.
// - Spacing: space-2 between rows and from label to field; space-1 between group rows.
// - Text fields, spin boxes, combo boxes and buttons have the visible height control-h; check
//   boxes and radio buttons 20 px. The visible rect is the layout item rect: the style margins
//   (the macOS focus ring) are outside it, so the visible edges are on the field edge (C04).
//   On macOS, check boxes and radio buttons move 2 pt right: Cocoa draws their box 2 pt to the
//   left of the layout rect.
// - Labels have the visible height of the first row of their field, text centred: the label
//   baseline is the baseline of the first control (6.2).
// - A text field or a combo box alone on its row fills the field column up to field-w-max. A
//   field with a fixed width keeps it.
void applyFormRhythm(QFormLayout *form);

// The visible height of a form control (control-h or the 20 px of a check box or radio button).
// 0 for other widgets.
int formControlHeight(const QWidget *w);

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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QFormLayout> m_form;
    QToolButton *m_button;
    QList<QPointer<QWidget>> m_rows;
};

// The "Apply to" combo box. Item data: "stem", "extension", "name".
QComboBox *makeApplyToCombo(QWidget *parent);

} // namespace ctrm::app
