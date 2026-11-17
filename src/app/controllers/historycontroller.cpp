// SPDX-License-Identifier: GPL-3.0-or-later
#include "historycontroller.h"

#include "appcontext.h"
#include "controllers/batchcontroller.h"
#include "dialogs/historywindow.h"
#include "dialogs/reportdialog.h"
#include "dialogs/undopreviewdialog.h"
#include "exec/batchservice.h"
#include "mainwindow.h"
#include "presets/preset.h"
#include "text/plural.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>

namespace ctrm::app {

namespace {

// In a text field, Ctrl+Z undoes text in the field (ui-design 10.3).
bool textFieldHasFocus()
{
    QWidget *w = QApplication::focusWidget();
    if (!w)
        return false;
    if (auto *line = qobject_cast<QLineEdit *>(w))
        return !line->isReadOnly();
    if (auto *text = qobject_cast<QTextEdit *>(w))
        return !text->isReadOnly();
    if (auto *plain = qobject_cast<QPlainTextEdit *>(w))
        return !plain->isReadOnly();
    return qobject_cast<QAbstractSpinBox *>(w) != nullptr;
}

} // namespace

HistoryController::HistoryController(MainWindow &w, AppContext &ctx, BatchController &batch,
                                     QObject *parent)
    : QObject(parent)
    , m_w(w)
    , m_ctx(ctx)
    , m_batch(batch)
{
    showUndoPreview = [this](const UndoPreview &preview) {
        UndoPreviewDialog dialog(preview, m_window && m_window->isVisible()
                                                  ? static_cast<QWidget *>(m_window.data())
                                                  : &m_w);
        return dialog.exec() == QDialog::Accepted;
    };
    confirmClearHistory = [this](int damaged) {
        QMessageBox box(m_window ? static_cast<QWidget *>(m_window.data()) : &m_w);
        box.setWindowModality(Qt::WindowModal);
        box.setIcon(QMessageBox::Warning);
        box.setText(tr("Clear all history?"));
        box.setInformativeText(clearHistoryInformativeText(damaged));
        QPushButton *cancel = box.addButton(QMessageBox::Cancel);
        QPushButton *clear = box.addButton(tr("Clear History"), QMessageBox::DestructiveRole);
        box.setDefaultButton(cancel);
        box.setEscapeButton(cancel);
        box.exec();
        return box.clickedButton() == clear;
    };
    confirmRollBack = [this](const BatchSummary &) {
        QMessageBox box(m_window ? static_cast<QWidget *>(m_window.data()) : &m_w);
        box.setWindowModality(Qt::WindowModal);
        box.setIcon(QMessageBox::Question);
        box.setText(tr("Roll back this batch?"));
        box.setInformativeText(tr("CTRM puts the renamed items back to their old names."));
        box.addButton(QMessageBox::Cancel);
        QPushButton *roll = box.addButton(tr("Roll Back"), QMessageBox::AcceptRole);
        box.setDefaultButton(roll);
        box.setEscapeButton(QMessageBox::Cancel);
        box.exec();
        return box.clickedButton() == roll;
    };

    const Actions &a = m_w.actions();
    connect(a.history, &QAction::triggered, this, [this] { showHistory(); });
    connect(a.undoRename, &QAction::triggered, this, &HistoryController::quickUndo);
    connect(&m_batch, &BatchController::batchFinished, this,
            [this](const FinishResult &result, const QString &batchId, const QString &undoOf) {
                if (undoOf.isEmpty() && batchId == m_batch.lastBatchId()) {
                    m_lastCount = 0;
                    for (const ReportRow &row : result.report)
                        m_lastCount += row.result == ReportRow::Result::Renamed ? 1 : 0;
                }
                refreshWindow();
                updateUndoAction();
            });
    // cancelBatch is enabled exactly while a batch runs.
    connect(a.cancelBatch, &QAction::enabledChanged, this, [this](bool running) {
        if (m_window)
            m_window->setBusy(running);
        updateUndoAction();
    });
    connect(&m_w.itemModel(), &ItemTableModel::listChanged, this,
            &HistoryController::updateUndoAction);
    connect(qApp, &QApplication::focusChanged, this, &HistoryController::updateUndoAction);
    updateUndoAction();
}

HistoryController::~HistoryController()
{
    delete m_window.data();
}

QString HistoryController::clearHistoryInformativeText(int damagedCount)
{
    QString text = tr("You cannot undo the batches in it after this.");
    if (damagedCount == 1)
        text += u'\n' + tr("CTRM keeps the damaged batch. You can remove its file by hand.");
    else if (damagedCount > 1)
        text += u'\n' + tr("CTRM keeps the %1 damaged batches. You can remove their files by hand.")
                                .arg(damagedCount);
    return text;
}

void HistoryController::showHistory(const QString &selectBatchId)
{
    if (!m_window) {
        m_window = new HistoryWindow(m_ctx.history(), &m_w);
        // A dialog with a parent stays above the main window; it is not modal.
        m_window->setWindowFlag(Qt::Window);
        connect(m_window, &HistoryWindow::undoBatchClicked, this, &HistoryController::undoBatch);
        connect(m_window, &HistoryWindow::rollBackClicked, this, &HistoryController::rollBack);
        connect(m_window, &HistoryWindow::markCompleteClicked, this,
                &HistoryController::markComplete);
        connect(m_window, &HistoryWindow::loadRulesClicked, this, &HistoryController::loadRules);
        connect(m_window, &HistoryWindow::clearHistoryClicked, this,
                &HistoryController::clearHistory);
    }
    m_window->setBusy(m_batch.isRunning());
    m_window->refresh();
    if (!selectBatchId.isEmpty())
        m_window->selectBatch(selectBatchId);
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

bool HistoryController::quickUndoAvailable() const
{
    const QString id = m_batch.lastBatchId();
    return !id.isEmpty() && id != m_clearedBatchId && !m_batch.isRunning() &&
           m_batch.lastBatchListVersion() == m_w.itemModel().listVersion() && !textFieldHasFocus();
}

void HistoryController::quickUndo()
{
    if (!quickUndoAvailable())
        return;
    QString error;
    const UndoPreview preview = m_ctx.batchService().previewUndo(m_batch.lastBatchId(), &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }
    // Spec 10a.1: the same check as the History undo. No dialog only when every item passes
    // with a strong identity check; never a partial undo without the preview.
    if (preview.quickUndoAllowed() || showUndoPreview(preview))
        runUndo(preview);
}

void HistoryController::undoBatch(const QString &batchId)
{
    if (m_batch.isRunning())
        return;
    QString error;
    const UndoPreview preview = m_ctx.batchService().previewUndo(batchId, &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }
    if (showUndoPreview(preview))
        runUndo(preview);
}

void HistoryController::runUndo(const UndoPreview &preview)
{
    PlanResult plan = m_ctx.batchService().planUndo(preview);
    if (!plan.plan) {
        showError(plan.error);
        return;
    }
    m_batch.runPlan(*plan.plan, preview.originalBatchId);
}

void HistoryController::loadRules(const QString &batchId)
{
    QString error;
    const std::optional<BatchRecord> rec =
            m_ctx.history().load(batchId, &error, ReadMode::BestEffort);
    if (!rec) {
        showError(error);
        return;
    }
    const PresetParseResult parsed =
            parsePreset(QJsonDocument(rec->header.chain).toJson(QJsonDocument::Compact));
    if (!parsed.preset) {
        showError(parsed.error);
        return;
    }
    m_w.ruleModel().setChain(parsed.preset->chain);
}

void HistoryController::rollBack(const QString &batchId)
{
    if (m_batch.isRunning())
        return;
    BatchSummary summary;
    for (const BatchSummary &s : m_ctx.history().list()) {
        if (s.batchId == batchId)
            summary = s;
    }
    if (!confirmRollBack(summary))
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const FinishResult result = m_ctx.batchService().recoverRollBack(batchId);
    QApplication::restoreOverrideCursor();
    refreshWindow();
    auto *report = new ReportDialog(result, summary.started,
                                    m_window ? static_cast<QWidget *>(m_window.data()) : &m_w);
    report->open();
}

void HistoryController::markComplete(const QString &batchId)
{
    if (m_batch.isRunning())
        return;
    QString error;
    if (!m_ctx.batchService().markComplete(batchId, &error))
        showError(error);
    refreshWindow();
}

void HistoryController::clearHistory()
{
    if (m_batch.isRunning())
        return;
    int damaged = 0;
    for (const BatchSummary &s : m_ctx.history().list())
        damaged += s.damaged ? 1 : 0;
    if (!confirmClearHistory(damaged))
        return;
    m_ctx.history().clear();
    m_clearedBatchId = m_batch.lastBatchId();
    refreshWindow();
    updateUndoAction();
}

void HistoryController::updateUndoAction()
{
    QAction *undo = m_w.actions().undoRename;
    const bool available = quickUndoAvailable();
    undo->setEnabled(available);
    undo->setText(
            available ? numerus(tr("Undo Rename of %n Item(s)", nullptr, m_lastCount), m_lastCount)
                      : tr("Undo Rename"));
}

void HistoryController::refreshWindow()
{
    if (m_window)
        m_window->refresh();
}

void HistoryController::showError(const QString &text)
{
    QMessageBox::warning(m_window && m_window->isVisible() ? static_cast<QWidget *>(m_window.data())
                                                           : &m_w,
                         tr("History"), text);
}

} // namespace ctrm::app
