// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

namespace ctrm::app {

// Report a Problem... (ui-design 13): the system information to copy into a report, and a
// button that opens the issue page in the browser. CTRM sends nothing itself (AC-G2).
class ReportProblemDialog : public QDialog
{
    Q_OBJECT
public:
    ReportProblemDialog(const QString &info, QWidget *parent);

signals:
    void openIssuePageClicked();
};

} // namespace ctrm::app
