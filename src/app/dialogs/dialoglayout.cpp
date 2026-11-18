// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs/dialoglayout.h"

#include "theme/tokens.h"

#include <QHeaderView>
#include <QTableView>
#include <QVBoxLayout>

namespace ctrm::app {

QVBoxLayout *makeDialogLayout(QWidget *dialog)
{
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(tokens::space5, tokens::space5, tokens::space5, tokens::space5);
    layout->setSpacing(tokens::space2);
    return layout;
}

namespace {

// The layout also puts its row spacing next to the spacer item: together they are the gap.
void addButtonGap(QVBoxLayout *layout)
{
    layout->addSpacing(tokens::space6 - layout->spacing());
}

} // namespace

void addButtonRow(QVBoxLayout *layout, QWidget *buttons)
{
    addButtonGap(layout);
    layout->addWidget(buttons);
}

void addButtonRow(QVBoxLayout *layout, QLayout *buttons)
{
    addButtonGap(layout);
    layout->addLayout(buttons);
}

void useRowHeight(QTableView *table)
{
    QHeaderView *rows = table->verticalHeader();
    rows->setMinimumSectionSize(qMin(rows->minimumSectionSize(), tokens::rowH));
    rows->setDefaultSectionSize(tokens::rowH);
}

} // namespace ctrm::app
