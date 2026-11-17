// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/batchservice.h"

#include <QDateTime>
#include <QDialog>

class QTableView;

namespace ctrm::app {

// The batch report (ui-design 9.7): a summary line and one row for each item.
class ReportDialog : public QDialog
{
    Q_OBJECT
public:
    ReportDialog(const ctrm::FinishResult &result, const QDateTime &when, QWidget *parent);

    static QString summaryText(const ctrm::FinishResult &result);
    static QString resultText(const ctrm::ReportRow &row);
    // Tab-separated: a header line, then one line for each row with the full paths.
    static QString copyText(const ctrm::FinishResult &result);

signals:
    void showInHistoryClicked();

private:
    ctrm::FinishResult m_result;
    QTableView *m_table;
};

} // namespace ctrm::app
