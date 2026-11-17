// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rule.h"

#include <QList>
#include <QObject>
#include <QString>

#include <utility>

class QComboBox;
class QLabel;
class QLineEdit;
class QWidget;

namespace ctrm::app {

// Makes label a dimmed caption (design-tokens 3.2, 3.3: caption, text-dim). It follows the font
// and the palette of source when they change.
void makeDimCaption(QLabel *label, QWidget *source);

// A combo box with one item for each (text, value) pair. The value is the item data.
QComboBox *makeChoiceCombo(const QList<std::pair<QString, int>> &items);

// Selects the item with this data. Does nothing when no item has it.
void selectData(QComboBox *combo, const QVariant &data);

// The "Apply to" combo box (formutils) and the rule target.
void showTarget(QComboBox *applyTo, Target target);
Target selectedTarget(const QComboBox *applyTo);

class RuleEditor;

// Knows when a user text edit is written to the rule but its ruleEdited() signal still waits for
// the 150 ms timer. setRule() emits it first, so the old rule's preview is not stale.
class PendingTextEdit : public QObject
{
public:
    explicit PendingTextEdit(RuleEditor *editor);
    void watch(QLineEdit *field);
    bool isPending() const { return m_pending; }

private:
    bool m_pending = false;
};

// Gives the field a fixed width for chars wide characters and its frame.
void setCharacterWidth(QLineEdit *field, int chars);

// The pattern font (design-tokens 3.1) at the size of the editor font.
QFont patternFont(const QFont &editorFont);

} // namespace ctrm::app
