// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "exec/historystore.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QStandardItemModel;
class QTableView;
class QVBoxLayout;

namespace ctrm::app {

class HistoryModel;

// The History window (ui-design 10.1): non-modal, a batch table and the operations of the
// selected batch. The buttons only send signals; HistoryController does the work.
class HistoryWindow : public QDialog
{
    Q_OBJECT
public:
    HistoryWindow(ctrm::HistoryStore &store, QWidget *parent);

    void refresh(); // reads the history folder again; keeps the selection
    void selectBatch(const QString &batchId);
    QString selectedBatchId() const;
    const HistoryModel *model() const;
    void setBusy(bool busy); // a batch runs: no undo, roll back or clear

signals:
    void undoBatchClicked(const QString &batchId);
    void rollBackClicked(const QString &batchId); // also Retry Roll Back
    void markCompleteClicked(const QString &batchId);
    void loadRulesClicked(const QString &batchId);
    void clearHistoryClicked();

private:
    void selectionChanged();
    void showDetail(const ctrm::BatchSummary *s);
    void showFile();

    ctrm::HistoryStore &m_store;
    HistoryModel *m_model;
    QTableView *m_batches;
    QStandardItemModel *m_operations;
    QTableView *m_operationTable;
    QVBoxLayout *m_detailLayout = nullptr;
    QLabel *m_detail;     // the "Batch:" line
    QLabel *m_detailNote; // the lines under it
    QLabel *m_detailError;
    QLabel *m_detailKeep;
    QPushButton *m_loadRules;
    QPushButton *m_clear;
    QPushButton *m_showFile;
    QPushButton *m_markComplete;
    QPushButton *m_rollBack;
    QPushButton *m_retryRollBack;
    QPushButton *m_undo;
    bool m_busy = false;
};

} // namespace ctrm::app
