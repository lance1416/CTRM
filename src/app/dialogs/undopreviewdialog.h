// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/undo.h"

#include <QDialog>

namespace ctrm::app {

// The undo preview (ui-design 10.2): the check result of each item before an undo.
// Accepted: run the undo for the items that pass.
class UndoPreviewDialog : public QDialog
{
    Q_OBJECT
public:
    UndoPreviewDialog(const ctrm::UndoPreview &preview, QWidget *parent);

    // "%n will go back · %n conflict(s) · %n missing"
    static QString countText(const ctrm::UndoPreview &preview);
    // The Message column. Missing has its own text: the undo preview cannot refresh or remove.
    static QString messageText(const ctrm::ItemPreview &preview);
};

} // namespace ctrm::app
