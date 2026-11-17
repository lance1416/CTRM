// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/historystore.h"
#include "exec/undo.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace ctrm::app {

class AppContext;
class BatchController;
class HistoryWindow;
class MainWindow;

// The History window, the undo preview and quick undo (ui-design 10). An undo runs as a new
// batch through BatchController (same progress, failure and report UI).
class HistoryController : public QObject
{
    Q_OBJECT
public:
    HistoryController(MainWindow &w, AppContext &ctx, BatchController &batch, QObject *parent);
    ~HistoryController() override;

    // The dialogs. Tests replace them.
    std::function<bool(const ctrm::UndoPreview &)> showUndoPreview;  // true: run the undo
    std::function<bool(int damagedCount)> confirmClearHistory;       // Cancel is the default
    std::function<bool(const ctrm::BatchSummary &)> confirmRollBack; // Roll Back..., Retry

    void showHistory(const QString &selectBatchId = {});
    void undoBatch(const QString &batchId); // History "Undo Batch...": always the preview
    void quickUndo();                       // message bar Undo, actions.undoRename
    // The last batch, the list did not change since (E6), no batch runs, and the focus is not
    // in a text field.
    bool quickUndoAvailable() const;
    void loadRules(const QString &batchId);
    void rollBack(const QString &batchId); // Roll Back... (in progress), Retry Roll Back...
    void markComplete(const QString &batchId);
    void clearHistory();

    // "You cannot undo ... after this." and the line about the damaged batches that stay.
    static QString clearHistoryInformativeText(int damagedCount);

private:
    void runUndo(const ctrm::UndoPreview &preview);
    void updateUndoAction();
    void refreshWindow();
    void showError(const QString &text);

    MainWindow &m_w;
    AppContext &m_ctx;
    BatchController &m_batch;
    QPointer<HistoryWindow> m_window;
    int m_lastCount = 0;      // items renamed by the last batch
    QString m_clearedBatchId; // the last batch was deleted by Clear History
};

} // namespace ctrm::app
