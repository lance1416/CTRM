// SPDX-License-Identifier: GPL-3.0-or-later
#include "batchcontroller.h"

#include "appcontext.h"
#include "controllers/batchworker.h"
#include "dialogs/failuredialog.h"
#include "dialogs/reportdialog.h"
#include "mainwindow.h"
#include "models/itemtablemodel.h"
#include "text/plural.h"
#include "theme/icons.h"
#include "theme/tokens.h"
#include "widgets/actionbar.h"
#include "widgets/itemtableview.h"
#include "widgets/messagebar.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

namespace ctrm::app {

namespace {

constexpr int kAlertIconSize = 64; // the icon of a macOS alert

// The reason text after "Skipped: " (ui-design 9.4) and the question count of a status.
QString skipReason(Status status)
{
    switch (status) {
    case Status::Conflict:
        return BatchController::tr("conflict");
    case Status::Invalid:
        return BatchController::tr("invalid name");
    case Status::Missing:
        return BatchController::tr("missing");
    case Status::RuleError:
        return BatchController::tr("rule error");
    case Status::Unchanged:
    case Status::Changed:
    case Status::CaseOnly:
        break;
    }
    return {};
}

QString skipText(const SkipQuestion &q)
{
    QStringList counts;
    if (q.conflicts > 0)
        counts.append(
                numerus(BatchController::tr("%n conflict(s)", nullptr, q.conflicts), q.conflicts));
    if (q.invalid > 0)
        counts.append(
                numerus(BatchController::tr("%n invalid name(s)", nullptr, q.invalid), q.invalid));
    if (q.other > 0)
        counts.append(
                numerus(BatchController::tr("%n other problem(s)", nullptr, q.other), q.other));
    QString text = BatchController::tr("These items keep their names: %1.")
                           .arg(counts.join(QStringLiteral(", ")));
    if (q.cascadeCount > 0) {
        text += u'\n' +
                (q.cascadeCount == 1
                         ? BatchController::tr("1 of these items is skipped only because it "
                                               "needs the old name of another skipped item.")
                         : BatchController::tr("%1 of these items are skipped only because they "
                                               "need the old name of another skipped item.")
                                   .arg(q.cascadeCount));
    }
    text += u'\n' + BatchController::tr("You can undo the rename in the History window.");
    return text;
}

bool defaultConfirmSkip(QWidget *parent, const SkipQuestion &q)
{
    const std::unique_ptr<QMessageBox> box = makeSkipConfirmBox(parent, q);
    box->exec();
    return box->clickedButton() && box->buttonRole(box->clickedButton()) == QMessageBox::AcceptRole;
}

int countOf(const QList<ReportRow> &rows, ReportRow::Result result)
{
    return int(std::count_if(rows.cbegin(), rows.cend(),
                             [result](const ReportRow &row) { return row.result == result; }));
}

} // namespace

std::unique_ptr<QMessageBox> makeSkipConfirmBox(QWidget *parent, const SkipQuestion &q)
{
    auto box = std::make_unique<QMessageBox>(parent);
    box->setWindowModality(Qt::WindowModal);
    // The app icon, as macOS alerts show it; not the generic question mark (R6-08).
    box->setIconPixmap(
            appIcon().pixmap(QSize(kAlertIconSize, kAlertIconSize),
                             parent ? parent->devicePixelRatioF() : qApp->devicePixelRatio()));
    const QString items =
            numerus(BatchController::tr("%n item(s)", nullptr, q.skipCount), q.skipCount);
    box->setText(BatchController::tr("Skip %1 and rename %2?").arg(items).arg(q.renameCount));
    box->setInformativeText(skipText(q));
    if (auto *info = box->findChild<QLabel *>(QStringLiteral("qt_msgbox_informativelabel")))
        info->setMinimumWidth(tokens::fieldWMax); // each sentence wraps at most once (R6-08)
    box->addButton(QMessageBox::Cancel);
    QPushButton *skip =
            box->addButton(BatchController::tr("Skip and Rename"), QMessageBox::AcceptRole);
    box->setDefaultButton(skip);
    box->setEscapeButton(QMessageBox::Cancel);
    return box;
}

BatchController::BatchController(MainWindow &w, AppContext &ctx, QObject *parent)
    : QObject(parent)
    , m_w(w)
    , m_ctx(ctx)
    , m_worker(std::make_unique<BatchWorker>())
{
    confirmSkip = [this](const SkipQuestion &q) { return defaultConfirmSkip(&m_w, q); };
    askFailure = [this](const FailureInfo &info) {
        FailureDialog dialog(info, &m_w);
        connect(&dialog, &FailureDialog::showReportClicked, this,
                [this] { openReport(m_lastResult); });
        // The user must choose before CTRM continues, so the dialog waits here. With a parent,
        // a window-modal dialog shows as a sheet on macOS.
        dialog.exec();
        return dialog.choice();
    };
    showReport = [this](const FinishResult &result) { openReport(result); };

    const Actions &a = m_w.actions();
    connect(a.rename, &QAction::triggered, this, &BatchController::renameNow);
    connect(a.skipAndRename, &QAction::triggered, this, &BatchController::skipBlockedAndRename);
    connect(a.cancelBatch, &QAction::triggered, this, &BatchController::cancel);
    // After the main window: the window has the new preview when this runs.
    connect(&m_w.previewScheduler(), &PreviewScheduler::previewReady, this,
            &BatchController::runPending);
    m_w.installEventFilter(this);
}

BatchController::~BatchController()
{
    m_cancel = true;
    m_worker.reset(); // waits for the current job; the session is still alive
}

void BatchController::renameNow()
{
    if (m_running)
        return;
    m_pending = Pending::Rename;
    runPending();
}

void BatchController::skipBlockedAndRename()
{
    if (m_running)
        return;
    m_pending = Pending::Skip;
    runPending();
}

void BatchController::runPending()
{
    // Rename uses the newest preview only (Review Focus 1): wait until it is ready.
    if (m_pending == Pending::None || m_running || !m_w.previewIsCurrent())
        return;
    const bool skip = m_pending == Pending::Skip;
    m_pending = Pending::None;
    prepareAndRun(skip);
}

void BatchController::prepareAndRun(bool skip)
{
    PreviewInput input = m_w.currentPreviewInput();
    PreviewResult shown = m_w.currentPreview();
    m_skipReasons.clear();
    if (skip) {
        const SkipResult s = previewSkippingBlocked(input);
        SkipQuestion q;
        q.skipCount = int(s.skipped.size());
        q.renameCount = s.preview.summary.willChange;
        q.cascadeCount = int(s.cascaded.size());
        for (qsizetype i = 0; i < input.items.size() && i < shown.items.size(); ++i) {
            const ItemId id = input.items.at(i).id;
            if (!s.skipped.contains(id))
                continue;
            const Status status = shown.items.at(i).status;
            // The full reason is the message of the blocked row (ui-design 7.2).
            const ItemTableModel &model = m_w.itemModel();
            const QString full = model.index(int(i), ItemTableModel::Message)
                                         .data(ItemTableModel::MessageRole)
                                         .toString();
            m_skipReasons.insert(id, PostRunState{PostRun::Skipped, skipReason(status), full});
            if (status == Status::Conflict)
                ++q.conflicts;
            else if (status == Status::Invalid)
                ++q.invalid;
            else
                ++q.other;
        }
        if (q.skipCount > 0) {
            if (!confirmSkip(q)) {
                m_skipReasons.clear();
                return;
            }
            input.skip = s.skipped;
            shown = s.preview;
        }
    }

    PrepareResult prepared = m_ctx.batchService().prepare(input, shown);
    switch (prepared.kind) {
    case PrepareResult::Kind::PreviewChanged:
        m_skipReasons.clear();
        // Refresh reads the folders and identities again and makes the new preview. The message
        // comes after it: a refresh ends the result state and closes its message.
        m_w.actions().refresh->trigger();
        m_w.showShortMessage(tr("The files changed on disk. Check the new preview."), true);
        return;
    case PrepareResult::Kind::NothingToDo:
        m_skipReasons.clear();
        m_w.showShortMessage(tr("Nothing to rename."), false);
        return;
    case PrepareResult::Kind::Error:
        m_skipReasons.clear();
        QMessageBox::warning(&m_w, tr("Rename"), prepared.error);
        return;
    case PrepareResult::Kind::Ready:
        break;
    }
    runPlan(*prepared.plan, QString());
}

void BatchController::runPlan(const Plan &plan, const QString &undoOf)
{
    if (m_running)
        return;
    QString error;
    // An undo batch has no rules.
    const RuleChain chain = undoOf.isEmpty() ? m_w.ruleModel().chain() : RuleChain();
    m_session = m_ctx.batchService().start(plan, chain, undoOf, &error);
    if (!m_session) {
        m_skipReasons.clear();
        QMessageBox::warning(&m_w, tr("Rename"), error);
        return;
    }
    m_running = true;
    m_cancel = false;
    m_closeAfterRun = false;
    m_undoOf = undoOf;
    m_startTime = QDateTime::currentDateTime();
    m_lastResult = {};
    // AC-G3: setRunning disables the field with the focus; give it back after the run.
    m_focusBeforeRun = QApplication::focusWidget();
    if (!m_focusBeforeRun)
        m_focusBeforeRun = m_w.focusWidget();
    m_w.setResultShown(false);
    m_w.setRunning(true);
    m_w.actions().cancelBatch->setEnabled(true);
    const int total = int(plan.steps.size());
    m_w.actionBar()->showProgress(0, total);

    BatchSession *session = m_session.get();
    const std::atomic<bool> *cancelFlag = &m_cancel;
    const auto hook = onWorkerProgress;
    // The progress runs on the worker thread. The destructor waits for the worker, so this
    // is alive.
    const Executor::Progress progress = [hook, this](int done, int all) {
        if (hook)
            hook(done, all);
        QMetaObject::invokeMethod(
                this,
                [this, done, all] {
                    if (m_running)
                        m_w.actionBar()->showProgress(done, all);
                },
                Qt::QueuedConnection);
    };
    m_worker->run<RunOutcome>(
            [session, cancelFlag, progress] { return session->run(*cancelFlag, progress); }, this,
            [this](const RunOutcome &outcome) { runDone(outcome); });
}

void BatchController::cancel()
{
    m_cancel = true;
}

bool BatchController::isRunning() const
{
    return m_running;
}

QString BatchController::lastBatchId() const
{
    return m_lastBatchId;
}

quint64 BatchController::lastBatchListVersion() const
{
    return m_lastListVersion;
}

bool BatchController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == &m_w && event->type() == QEvent::Close && m_running) {
        // Review Focus 3: no question. Cancel after the current step, show the stop dialog,
        // then close.
        event->ignore();
        m_closeAfterRun = true;
        cancel();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

FinishResult BatchController::partialReport(const RunOutcome &outcome) const
{
    QList<StepRecord> records;
    for (int step : outcome.okSteps) {
        StepRecord r;
        r.step = step;
        r.ok = true;
        records.append(r);
    }
    QHash<int, QString> failed;
    if (outcome.failedStep >= 0)
        failed.insert(outcome.failedStep, outcome.failure.errorText);
    FinishResult out;
    out.state = BatchState::InProgress;
    out.report = buildReport(m_session->plan(), records, failed);
    return out;
}

void BatchController::runDone(const RunOutcome &outcome)
{
    if (outcome.finished) {
        BatchSession *session = m_session.get();
        finishOnWorker([session] { return session->complete(); });
        return;
    }
    m_w.actionBar()->hideProgress();
    m_w.actions().cancelBatch->setEnabled(false);
    m_lastResult = partialReport(outcome);
    m_lastResultTime = m_startTime;
    FailureInfo info;
    info.cancelled = outcome.cancelled && outcome.failedStep < 0;
    info.total = int(m_session->plan().requests.size());
    for (const ReportRow &row : std::as_const(m_lastResult.report)) {
        if (row.result == ReportRow::Result::Renamed || row.result == ReportRow::Result::LeftAtTemp)
            ++info.done;
        if (row.result == ReportRow::Result::Failed)
            info.failed.append(row);
    }
    const FailureChoice choice = askFailure(info);
    BatchSession *session = m_session.get();
    if (choice == FailureChoice::Keep)
        finishOnWorker([session] { return session->keep(); });
    else
        finishOnWorker([session] { return session->rollBack(); });
    m_failureAsked = true;
}

void BatchController::finishOnWorker(std::function<FinishResult()> job)
{
    m_worker->run<FinishResult>(std::move(job), this,
                                [this](const FinishResult &result) { finished(result); });
}

void BatchController::restoreFocus()
{
    // The control that had the focus before the run, when the user can still use it (an undo
    // from History: the Undo Batch button hides after the undo). Else the item table.
    QWidget *target = m_focusBeforeRun;
    m_focusBeforeRun.clear();
    if (!target || !target->isVisible() || !target->isEnabled() ||
        target->focusPolicy() == Qt::NoFocus)
        target = m_w.tableView();
    target->setFocus(Qt::OtherFocusReason);
}

void BatchController::finished(const FinishResult &result)
{
    const QString batchId = m_session->batchId();
    const QString undoOf = m_undoOf;
    const bool failurePath = m_failureAsked;
    m_failureAsked = false;
    m_session.reset();
    m_running = false;
    m_w.actionBar()->hideProgress();
    m_w.actions().cancelBatch->setEnabled(false);
    m_w.setRunning(false);

    m_lastResult = result;
    m_lastResultTime = m_startTime;
    // The report has the item IDs of the plan. An undo plan has the IDs of the original batch,
    // which can be other items of the list now: find each row by its old path.
    QHash<QString, ItemId> idOfPath;
    for (const Item &item : m_w.itemModel().items())
        idOfPath.insert(item.path(), item.id);
    m_w.itemModel().applyRenames(result.report);
    // S04, ui-design 9.8: after an undo (complete, partial or failed) the list shows the names on
    // disk and a new preview, not a result state.
    const bool undone = !undoOf.isEmpty();
    QHash<ItemId, PostRunState> states;
    for (const ReportRow &row : result.report) {
        if (undone)
            break;
        const auto id = idOfPath.constFind(row.oldPath);
        if (id == idOfPath.cend())
            continue;
        PostRunState s;
        switch (row.result) {
        case ReportRow::Result::Renamed:
            s.state = PostRun::Renamed;
            break;
        case ReportRow::Result::Failed:
            s.state = PostRun::Failed;
            s.text = row.errorText;
            break;
        case ReportRow::Result::RolledBack:
            s.state = PostRun::RolledBack;
            break;
        case ReportRow::Result::LeftAtTemp:
            s.state = PostRun::Failed;
            s.text = tr("The item has a temporary name.");
            break;
        case ReportRow::Result::NotRun:
            continue;
        }
        states.insert(*id, s);
    }
    for (auto it = m_skipReasons.cbegin(); it != m_skipReasons.cend(); ++it)
        states.insert(it.key(), it.value());
    m_skipReasons.clear();
    m_w.itemModel().setPostRun(states);

    const int renamed = countOf(result.report, ReportRow::Result::Renamed);
    if (!undoOf.isEmpty()) {
        m_lastBatchId.clear(); // quick undo is for a forward batch only
    } else if (result.state == BatchState::Complete && renamed > 0) {
        m_lastBatchId = batchId;
        m_lastListVersion = m_w.itemModel().listVersion();
    }

    m_w.folderCache().invalidate();
    m_w.requestPreview();
    showResultMessage(result);
    if (!undone)
        m_w.setResultShown(true, renamed);

    if (failurePath && !m_closeAfterRun)
        showReport(result);
    emit batchFinished(result, batchId, undoOf);
    // After batchFinished: the History window refresh can hide the control that had the focus.
    restoreFocus();
    if (m_closeAfterRun) {
        m_closeAfterRun = false;
        m_w.close();
    }
}

void BatchController::showResultMessage(const FinishResult &result)
{
    const int renamed = countOf(result.report, ReportRow::Result::Renamed);
    const int atTemp = countOf(result.report, ReportRow::Result::LeftAtTemp);
    QList<QPushButton *> buttons;
    StateIcon icon = StateIcon::Done;
    QString text;
    const int total = int(result.report.size());
    if (!m_undoOf.isEmpty() && result.state == BatchState::Complete && renamed == total) {
        text = numerus(tr("Undid the rename of %n item(s).", nullptr, renamed), renamed);
    } else if (!m_undoOf.isEmpty()) {
        // ui-design 9.8: a partial or failed undo.
        icon = renamed > 0 ? StateIcon::Warning : StateIcon::Error;
        text = numerus(tr("Undid %1 of %n item(s).", nullptr, total), total).arg(renamed) + u' ' +
               numerus(tr("%n item(s) could not go back.", nullptr, total - renamed),
                       total - renamed);
    } else if (result.state == BatchState::Complete) {
        text = numerus(tr("Renamed %n item(s).", nullptr, renamed), renamed);
        if (renamed > 0 && m_undoOf.isEmpty()) {
            auto *undo = new QPushButton(tr("Undo"));
            undo->setObjectName(QStringLiteral("resultUndo"));
            connect(undo, &QPushButton::clicked, this, &BatchController::resultUndoClicked);
            buttons.append(undo);
        }
    } else if (result.state == BatchState::RolledBack) {
        icon = StateIcon::RolledBack;
        text = tr("The rename was rolled back.");
    } else {
        icon = StateIcon::Error;
        text = atTemp == 1 ? tr("Roll back is not complete. 1 item has a temporary name.")
                           : tr("Roll back is not complete. %1 items have temporary names.")
                                     .arg(atTemp);
    }
    if (!result.error.isEmpty()) {
        icon = StateIcon::Error;
        text += u' ' + result.error;
    }
    auto *report = new QPushButton(tr("Show Report"));
    report->setObjectName(QStringLiteral("resultShowReport"));
    connect(report, &QPushButton::clicked, this, [this] { showReport(m_lastResult); });
    buttons.append(report);
    for (QPushButton *b : std::as_const(buttons))
        b->setAutoDefault(false);
    m_w.messageBar()->showMessage(icon, text, buttons);
}

void BatchController::openReport(const FinishResult &result)
{
    auto *dialog = new ReportDialog(result, m_lastResultTime, &m_w);
    connect(dialog, &ReportDialog::showInHistoryClicked, m_w.actions().history, &QAction::trigger);
    dialog->open();
}

} // namespace ctrm::app
