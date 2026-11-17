// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs/reportdialog.h"

#include "models/historymodel.h"
#include "text/plural.h"
#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/typography.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

namespace ctrm::app {

namespace {

using Result = ReportRow::Result;

int countOf(const FinishResult &r, Result result)
{
    return int(std::count_if(r.report.cbegin(), r.report.cend(),
                             [result](const ReportRow &row) { return row.result == result; }));
}

StateIcon iconFor(Result result)
{
    switch (result) {
    case Result::Renamed:
        return StateIcon::Done;
    case Result::Failed:
        return StateIcon::Error;
    case Result::RolledBack:
        return StateIcon::RolledBack;
    case Result::LeftAtTemp:
        return StateIcon::Warning;
    case Result::NotRun:
        break;
    }
    return StateIcon::Excluded;
}

QString fileName(const QString &path)
{
    return QFileInfo(path).fileName();
}

} // namespace

ReportDialog::ReportDialog(const FinishResult &result, const QDateTime &when, QWidget *parent)
    : QDialog(parent)
    , m_result(result)
    , m_table(new QTableView(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    // M12: the time as in the History window ("Today 22:21").
    setWindowTitle(tr("Rename Report - %1")
                           .arg(HistoryModel::whenText(when, QDateTime::currentDateTime())));

    auto *summary = new QLabel(summaryText(result), this);
    summary->setObjectName(QStringLiteral("reportSummary"));
    summary->setFont(typeStyle(TypeToken::LabelStrong, font()));
    summary->setWordWrap(true);

    const CtrmColors colors = CtrmColors::fromPalette(palette());
    auto *model = new QStandardItemModel(0, 4, this);
    model->setHorizontalHeaderLabels(
            {tr("State"), tr("Item (old name)"), tr("Final name"), tr("Result")});
    for (const ReportRow &row : result.report) {
        auto *state = new QStandardItem(stateIcon(iconFor(row.result), colors), QString());
        state->setData(resultText(row), Qt::ToolTipRole);
        state->setData(resultText(row), Qt::AccessibleTextRole);
        auto *item = new QStandardItem(fileName(row.oldPath));
        item->setToolTip(QDir::toNativeSeparators(row.oldPath));
        // A temporary name shows the full path, so the user can find the item.
        auto *final = new QStandardItem(row.result == Result::LeftAtTemp
                                                ? QDir::toNativeSeparators(row.finalPath)
                                                : fileName(row.finalPath));
        final->setToolTip(QDir::toNativeSeparators(row.finalPath));
        auto *text = new QStandardItem(resultText(row));
        text->setToolTip(resultText(row));
        model->appendRow({state, item, final, text});
    }
    m_table->setModel(model);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setTextElideMode(Qt::ElideMiddle);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false); // G1, as the other tables
    // M12: the names get the free width; State and Result are as wide as their content.
    QHeaderView *header = m_table->horizontalHeader();
    header->setStretchLastSection(false);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *copy = buttons->addButton(tr("Copy Report"), QDialogButtonBox::ActionRole);
    copy->setObjectName(QStringLiteral("copyReport"));
    copy->setAutoDefault(false);
    QPushButton *history = buttons->addButton(tr("Show in History"), QDialogButtonBox::ActionRole);
    history->setObjectName(QStringLiteral("showInHistory"));
    history->setAutoDefault(false);
    QPushButton *close = buttons->addButton(QDialogButtonBox::Close);
    close->setDefault(true);
    close->setFocus();

    connect(copy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(copyText(m_result)); });
    connect(history, &QPushButton::clicked, this, [this] {
        emit showInHistoryClicked();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(summary);
    layout->addWidget(m_table, 1);
    layout->addWidget(buttons);
    resize(sizeHint().expandedTo(QSize(640, 360)));
}

QString ReportDialog::summaryText(const FinishResult &r)
{
    const int renamed = countOf(r, Result::Renamed);
    const int failed = countOf(r, Result::Failed);
    const int rolledBack = countOf(r, Result::RolledBack);
    const int atTemp = countOf(r, Result::LeftAtTemp);
    QString text;
    if (atTemp > 0 || r.state == BatchState::Incomplete) {
        if (atTemp == 0)
            text = tr("Roll back is not complete.");
        else if (atTemp == 1)
            text = tr("Roll back is not complete. 1 item has a temporary name.");
        else
            text = tr("Roll back is not complete. %1 items have temporary names.").arg(atTemp);
    } else if (r.state == BatchState::RolledBack) {
        text = rolledBack == 1
                       ? tr("Rolled back. 1 item has its old name again.")
                       : tr("Rolled back. %1 items have their old names again.").arg(rolledBack);
    } else {
        text = numerus(tr("Renamed %n item(s).", nullptr, renamed), renamed);
    }
    if (failed > 0)
        text += u' ' + numerus(tr("%n item(s) failed.", nullptr, failed), failed);
    if (!r.error.isEmpty())
        text += u' ' + r.error;
    return text;
}

QString ReportDialog::resultText(const ReportRow &row)
{
    switch (row.result) {
    case Result::Renamed:
        return tr("Renamed");
    case Result::Failed:
        return tr("Failed: %1").arg(row.errorText);
    case Result::RolledBack:
        return tr("Rolled back");
    case Result::LeftAtTemp:
        return tr("Temporary name");
    case Result::NotRun:
        break;
    }
    return tr("Not renamed");
}

QString ReportDialog::copyText(const FinishResult &r)
{
    QStringList lines;
    lines.append(QStringList{tr("Item (old name)"), tr("Final name"), tr("Result")}.join(u'\t'));
    for (const ReportRow &row : r.report) {
        lines.append(QStringList{QDir::toNativeSeparators(row.oldPath),
                                 QDir::toNativeSeparators(row.finalPath), resultText(row)}
                             .join(u'\t'));
    }
    return lines.join(u'\n') + u'\n';
}

} // namespace ctrm::app
