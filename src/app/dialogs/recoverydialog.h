// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/historystore.h"

#include <QDateTime>
#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

namespace ctrm::app {

enum class RecoveryChoice { RollBack, MarkComplete, ShowDetails, ShowFile, Close };

// One unfinished batch for the prompt at start.
struct RecoveryInfo
{
    QString batchId;
    QString filePath; // the journal (Show File)
    QDateTime started;
    int done = 0;      // done forward steps ("operations")
    int total = 0;     // steps of the plan
    int doneItems = 0; // items that a roll back puts back
    bool damaged = false;
    QStringList folders;
    QList<ctrm::OpenStep> openSteps; // from BatchSummary::openSteps
};

// "The last rename did not finish." (ui-design 9.10). Escape or close: Close.
class RecoveryDialog : public QDialog
{
    Q_OBJECT
public:
    RecoveryDialog(const RecoveryInfo &info, QWidget *parent);

    RecoveryChoice choice() const { return m_choice; }

    static QString headingText();
    static QString bodyText(const RecoveryInfo &info);
    static QString openStepsLabelText(int count);
    static QString openStepsNoteText(int count);
    // Tab-separated full paths, one step per line.
    static QString copyText(const QList<ctrm::OpenStep> &steps);

    void reject() override;

private:
    void finish(RecoveryChoice choice);

    RecoveryChoice m_choice = RecoveryChoice::Close;
};

} // namespace ctrm::app
