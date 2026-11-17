// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs/undopreviewdialog.h"

#include "models/historymodel.h"
#include "text/messages.h"
#include "text/plural.h"
#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/typography.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

namespace ctrm::app {

namespace {

bool passes(Status s)
{
    return s == Status::Changed || s == Status::CaseOnly;
}

std::optional<StateIcon> iconFor(Status s)
{
    switch (s) {
    case Status::Changed:
    case Status::CaseOnly:
        return StateIcon::Change;
    case Status::Conflict:
        return StateIcon::Conflict;
    case Status::Invalid:
    case Status::Missing:
    case Status::RuleError:
        return StateIcon::Error;
    case Status::Unchanged:
        break;
    }
    return std::nullopt;
}

} // namespace

QString UndoPreviewDialog::countText(const UndoPreview &preview)
{
    int conflicts = 0;
    int missing = 0;
    int other = 0;
    for (const UndoItem &item : preview.items) {
        const Status s = item.preview.status;
        if (s == Status::Conflict)
            ++conflicts;
        else if (s == Status::Missing)
            ++missing;
        else if (!passes(s))
            ++other;
    }
    QStringList parts{tr("%1 will go back").arg(preview.passCount)};
    if (conflicts > 0)
        parts.append(numerus(tr("%n conflict(s)", nullptr, conflicts), conflicts));
    if (missing > 0)
        parts.append(tr("%1 missing").arg(missing));
    if (other > 0)
        parts.append(numerus(tr("%n other problem(s)", nullptr, other), other));
    return parts.join(QStringLiteral(" · "));
}

UndoPreviewDialog::UndoPreviewDialog(const UndoPreview &preview, QWidget *parent)
    : QDialog(parent)
{
    setWindowModality(Qt::WindowModal);
    const int count = int(preview.items.size());
    setWindowTitle(numerus(tr("Undo Rename of %n Item(s)", nullptr, count), count) +
                   QStringLiteral(" - ") +
                   HistoryModel::whenText(preview.originalStarted, QDateTime::currentDateTime()));

    auto *counts = new QLabel(countText(preview), this);
    counts->setObjectName(QStringLiteral("undoCounts"));
    counts->setFont(typeStyle(TypeToken::LabelStrong, font()));

    // Weak identity check (spec 10a.4): a warning, it does not block.
    QStringList weakLines;
    QSet<QString> weakFolders;
    for (const auto &[folder, fsName] : preview.weakFolders) {
        weakFolders.insert(folder);
        weakLines.append(tr("Weak identity check. %1 is %2. CTRM can compare only the name, size, "
                            "and date of each item.")
                                 .arg(HistoryModel::displayPath(folder), fsName));
    }
    auto *weak = new QLabel(weakLines.join(u'\n'), this);
    weak->setObjectName(QStringLiteral("weakBar"));
    weak->setWordWrap(true);
    weak->setTextFormat(Qt::PlainText);
    weak->setVisible(!weakLines.isEmpty());
    const CtrmColors colors = CtrmColors::fromPalette(palette());
    auto *weakIcon = new QLabel(this);
    const int iconSize = fontMetrics().height();
    weakIcon->setPixmap(stateIcon(StateIcon::Warning, colors).pixmap(iconSize, iconSize));
    weakIcon->setVisible(!weakLines.isEmpty());

    auto *model = new QStandardItemModel(0, 4, this);
    model->setHorizontalHeaderLabels(
            {tr("State"), tr("Current name"), tr("Name after undo"), tr("Message")});
    for (const UndoItem &item : preview.items) {
        auto *state = new QStandardItem;
        if (const auto icon = iconFor(item.preview.status))
            state->setIcon(stateIcon(*icon, colors));
        const QString message = statusText(item.preview, false);
        state->setToolTip(message);
        state->setData(message, Qt::AccessibleTextRole);
        auto *current = new QStandardItem(item.currentName);
        current->setToolTip(
                HistoryModel::displayPath(item.currentFolder + u'/' + item.currentName));
        auto *after = new QStandardItem(item.originalName);
        auto *text = new QStandardItem(message);
        if (weakFolders.contains(item.currentFolder)) {
            const QString tip = tr("Weak identity check.");
            current->setToolTip(current->toolTip() + u'\n' + tip);
            text->setToolTip(message + u'\n' + tip);
        }
        model->appendRow({state, current, after, text});
    }
    auto *table = new QTableView(this);
    table->setObjectName(QStringLiteral("undoTable"));
    table->setModel(model);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setTextElideMode(Qt::ElideMiddle);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->resizeColumnToContents(1);
    table->resizeColumnToContents(2);

    const int blocked = count - preview.passCount;
    auto *footer = new QLabel(this);
    footer->setObjectName(QStringLiteral("undoFooter"));
    footer->setText(blocked == 1
                            ? tr("1 item cannot go back. It keeps its name.")
                            : tr("%1 items cannot go back. They keep their names.").arg(blocked));
    footer->setVisible(blocked > 0);

    auto *reason = new QLabel(tr("No item can go back."), this);
    reason->setObjectName(QStringLiteral("undoReason"));
    reason->setVisible(preview.passCount == 0);

    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(QDialogButtonBox::Cancel);
    QPushButton *run = buttons->addButton(
            numerus(tr("Undo %n Item(s)", nullptr, preview.passCount), preview.passCount),
            QDialogButtonBox::AcceptRole);
    run->setObjectName(QStringLiteral("undoRun"));
    run->setDefault(true);
    run->setEnabled(preview.passCount > 0);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *weakRow = new QHBoxLayout;
    weakRow->addWidget(weakIcon, 0, Qt::AlignTop);
    weakRow->addWidget(weak, 1);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(reason);
    bottom->addStretch(1);
    bottom->addWidget(buttons);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(counts);
    layout->addLayout(weakRow);
    layout->addWidget(table, 1);
    layout->addWidget(footer);
    layout->addLayout(bottom);
    resize(sizeHint().expandedTo(QSize(640, 400)));
}

} // namespace ctrm::app
