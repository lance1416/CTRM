// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCheckBox>
#include <QLabel>
#include <QList>
#include <QObject>
#include <QPointer>

class QComboBox;
class QFormLayout;
class QLayout;
class QLayoutItem;
class QStyleOptionButton;
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

// A check box with a text that wraps at its width: the box at the native place, the text at the
// native gap after it, the first line centred on the box, as a one-line check box (R5-03). A
// long text never makes its column wider (ui-design 4.1, 8.10). Add it with makeToggleItem.
class WrappingCheckBox : public QCheckBox
{
public:
    explicit WrappingCheckBox(const QString &text, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool hitButton(const QPoint &pos) const override;

private:
    QStyleOptionButton boxOption(int width) const;
    QRect textRect(int width) const;
};

// The layout item for a check box or a radio button. The widget is the layout rect, with no
// layout margins of the style, and keeps its own height, centred in the rect (AlignTop: at the
// top). Cocoa draws the box at the left edge of the widget and centres the box and the text in
// it, so the box is on the field edge (C04) and the box and the text share one centre (R5-02).
// applyFormRhythm uses it for the check boxes and radio buttons of a form.
QLayoutItem *makeToggleItem(QWidget *w);

// A form field that wraps its text. A wrapped QLabel gives a size hint about half as wide as its
// text and so two lines high (a widget with a height-for-width layout gives the height at its
// size hint width), and QFormLayout makes a row at least as high as the size hint of its field:
// an empty line under a note of one line. This size hint is as high as the content with no width
// limit (one line for text without line breaks); QFormLayout adds the lines for the real width
// with heightForWidth. The width of the size hint does not change.
template <typename Base>
class WrappedField : public Base
{
public:
    using Base::Base;
    QSize sizeHint() const override
    {
        return {Base::sizeHint().width(), this->heightForWidth(QWIDGETSIZE_MAX)};
    }
};

// A word-wrapped note in a form.
class NoteLabel : public WrappedField<QLabel>
{
public:
    explicit NoteLabel(const QString &text = QString(), QWidget *parent = nullptr);
};

// text with a no-break space between its last two words: a wrapped hint never ends with one word
// alone on its last line (R5-08). Plain text only: a space in a rich text tag would break it.
QString joinLastWords(const QString &text);

// Makes the label baseline the same as the text baseline of firstControl (ui-design 6.2).
// It applies again when the font or the style of the label or the control changes.
void alignLabelToFirstRow(QLabel *label, QWidget *firstControl);

// The vertical rhythm, spacing and field widths of a form (ui-design 6.1 rules 2, 8, 14 and 15;
// design-tokens 4). Call it after the rows are added. It applies again on a font or style change.
// - Spacing: space-2 between rows and from label to field. The rows of a field group have one
//   pitch of control-h, with no spacing (R5-04).
// - Text fields, spin boxes, combo boxes and buttons have the visible height control-h; check
//   boxes and radio buttons 20 px, centred in their group row, in a makeToggleItem item. The
//   visible rect is the layout item rect: the style margins (the macOS focus ring) are outside
//   it, so the visible edges are on the field edge (C04).
// - A word-wrapped label fills the field column: a hint wraps at the panel edge (R5-08).
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
