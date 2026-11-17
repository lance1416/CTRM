// SPDX-License-Identifier: GPL-3.0-or-later
#include "recoverycontroller.h"

#include "appcontext.h"
#include "controllers/historycontroller.h"
#include "dialogs/reportdialog.h"
#include "exec/undo.h"
#include "mainwindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#include <QSet>
#include <QUrl>

#include <algorithm>

namespace ctrm::app {

RecoveryController::RecoveryController(MainWindow &w, AppContext &ctx, HistoryController &history,
                                       QObject *parent)
    : QObject(parent)
    , m_w(w)
    , m_ctx(ctx)
    , m_history(history)
{
    askRecovery = [this](const RecoveryInfo &info) {
        RecoveryDialog dialog(info, &m_w);
        dialog.exec();
        return dialog.choice();
    };
    showReport = [this](const FinishResult &result, const QDateTime &started) {
        ReportDialog dialog(result, started, &m_w);
        dialog.exec();
    };
}

RecoveryInfo RecoveryController::infoOf(const BatchSummary &summary) const
{
    RecoveryInfo info;
    info.batchId = summary.batchId;
    info.filePath = summary.filePath;
    info.started = summary.started;
    info.damaged = summary.damaged;
    info.folders = summary.folders;
    info.openSteps = summary.openSteps;
    if (summary.damaged)
        return info;
    QString error;
    const std::optional<BatchRecord> rec = m_ctx.history().load(summary.batchId, &error);
    if (!rec) {
        // The summary said readable; treat a failed strict read as damaged (no roll back).
        info.damaged = true;
        return info;
    }
    const QList<int> done = replayDoneSteps(rec->plan, rec->steps);
    info.done = int(done.size());
    info.total = int(rec->plan.steps.size());
    QSet<ItemId> items;
    for (const int i : done) {
        for (const PlanStep &s : rec->plan.steps) {
            if (s.index == i)
                items.insert(s.item);
        }
    }
    info.doneItems = int(items.size());
    return info;
}

void RecoveryController::checkAtStart()
{
    // Only in-progress batches: CTRM stopped during them. An incomplete batch already had a
    // decision; the History window shows it with Retry Roll Back.
    QList<BatchSummary> pending = m_ctx.batchService().pendingRecovery();
    std::ranges::reverse(pending); // oldest first
    for (const BatchSummary &summary : pending) {
        if (summary.state != BatchState::InProgress)
            continue;
        const RecoveryInfo info = infoOf(summary);
        RecoveryChoice choice = askRecovery(info);
        while (choice == RecoveryChoice::ShowFile) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(info.filePath).absolutePath()));
            choice = askRecovery(info);
        }
        switch (choice) {
        case RecoveryChoice::RollBack: {
            if (info.damaged)
                break; // never from a damaged journal; the dialog has no Roll Back
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const FinishResult result = m_ctx.batchService().recoverRollBack(info.batchId);
            QApplication::restoreOverrideCursor();
            if (!result.error.isEmpty() && result.report.isEmpty())
                showError(result.error);
            else
                showReport(result, info.started);
            break;
        }
        case RecoveryChoice::MarkComplete: {
            QString error;
            if (!m_ctx.batchService().markComplete(info.batchId, &error))
                showError(error);
            break;
        }
        case RecoveryChoice::ShowDetails:
            m_history.showHistory(info.batchId);
            break;
        case RecoveryChoice::ShowFile:
        case RecoveryChoice::Close:
            break; // nothing changes; asked again at the next start
        }
    }
}

void RecoveryController::showError(const QString &text)
{
    QMessageBox::warning(&m_w, tr("Recovery"), text);
}

} // namespace ctrm::app
