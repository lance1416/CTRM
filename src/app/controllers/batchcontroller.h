// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/batchservice.h"
#include "exec/plan.h"
#include "model/item.h"
#include "text/messages.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

namespace ctrm::app {

class AppContext;
class BatchWorker;
class MainWindow;

// The question before "Skip blocked items" (ui-design 9.4).
struct SkipQuestion
{
    int skipCount = 0;
    int renameCount = 0;
    int conflicts = 0; // also the items that are skipped only because of another skipped item
    int invalid = 0;
    int other = 0; // missing items and rule errors
    int cascadeCount = 0;
};

// The question before "Skip blocked items" as a message box (ui-design 9.4): the app icon, and
// the text at least field-w-max wide, so that each sentence wraps at most once (audit R6-08). The
// Skip and Rename button has the AcceptRole.
std::unique_ptr<QMessageBox> makeSkipConfirmBox(QWidget *parent, const SkipQuestion &q);

enum class FailureChoice { RollBack, Keep };

// The failure or cancel question (ui-design 9.6).
struct FailureInfo
{
    bool cancelled = false;
    int done = 0;  // items at the new name
    int total = 0; // items in the batch
    QList<ctrm::ReportRow> failed;
};

// Runs a batch from the main window (ui-design 9.4 to 9.8). The steps run on a worker thread.
class BatchController : public QObject
{
    Q_OBJECT
public:
    BatchController(MainWindow &w, AppContext &ctx, QObject *parent);
    ~BatchController() override;

    // The dialogs. Tests replace them.
    std::function<bool(const SkipQuestion &)> confirmSkip;        // default: QMessageBox (9.4)
    std::function<FailureChoice(const FailureInfo &)> askFailure; // default: FailureDialog (9.6)
    std::function<void(const ctrm::FinishResult &)> showReport;   // default: ReportDialog (9.7)
    // Called on the worker thread after each step. For tests.
    std::function<void(int done, int total)> onWorkerProgress;

    void renameNow();            // actions.rename; waits for the current preview
    void skipBlockedAndRename(); // actions.skipAndRename
    void runPlan(const ctrm::Plan &plan, const QString &undoOf); // also used by undo (Task 15)
    void cancel();                                               // actions.cancelBatch; thread-safe
    bool isRunning() const;
    QString lastBatchId() const;          // last complete forward batch
    quint64 lastBatchListVersion() const; // itemModel.listVersion() after applyRenames

signals:
    void batchFinished(const ctrm::FinishResult &result, const QString &batchId,
                       const QString &undoOf);
    void resultUndoClicked(); // the Undo button of the result message

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Pending { None, Rename, Skip };

    void runPending();
    void prepareAndRun(bool skip);
    void runDone(const ctrm::RunOutcome &outcome);
    void finishOnWorker(std::function<ctrm::FinishResult()> job);
    void finished(const ctrm::FinishResult &result);
    void restoreFocus();
    void showResultMessage(const ctrm::FinishResult &result);
    void openReport(const ctrm::FinishResult &result);
    ctrm::FinishResult partialReport(const ctrm::RunOutcome &outcome) const;

    MainWindow &m_w;
    AppContext &m_ctx;
    Pending m_pending = Pending::None;
    bool m_running = false;
    bool m_closeAfterRun = false;
    bool m_failureAsked = false; // the run failed or was cancelled: show the report at the end
    std::atomic<bool> m_cancel = false;
    std::unique_ptr<ctrm::BatchSession> m_session;
    QString m_undoOf;
    QDateTime m_startTime;
    QHash<ctrm::ItemId, PostRunState> m_skipReasons; // PostRun::Skipped, short and full reason
    ctrm::FinishResult m_lastResult;
    QPointer<QWidget> m_focusBeforeRun;
    QDateTime m_lastResultTime;
    QString m_lastBatchId;
    quint64 m_lastListVersion = 0;
    // Last member: deleted first, so a running job ends before the session is deleted.
    std::unique_ptr<BatchWorker> m_worker;
};

} // namespace ctrm::app
