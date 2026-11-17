// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs/historywindow.h"

#include "dialogs/reportdialog.h"
#include "exec/batchservice.h"
#include "models/historymodel.h"
#include "presets/preset.h"
#include "rules/rulesummary.h"
#include "text/messages.h"
#include "theme/typography.h"
#include "widgets/buttonstyle.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

namespace ctrm::app {

namespace {

QPushButton *makeButton(const QString &text, const char *name, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(QLatin1StringView(name));
    b->setAutoDefault(false);
    return b;
}

QLabel *makeCaption(const char *name, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QLatin1StringView(name));
    label->setFont(typeStyle(TypeToken::Caption, parent->font()));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

HistoryWindow::HistoryWindow(HistoryStore &store, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_model(new HistoryModel(this))
    , m_batches(new QTableView(this))
    , m_operations(new QStandardItemModel(0, 3, this))
    , m_operationTable(new QTableView(this))
    , m_detail(new QLabel(this))
    , m_detailError(makeCaption("historyDetailError", this))
    , m_detailKeep(makeCaption("historyDetailKeep", this))
    , m_loadRules(makeButton(tr("Load Rules"), "loadRules", this))
    , m_clear(makeButton(tr("Clear History..."), "clearHistory", this))
    , m_showFile(makeButton(showInFileManagerText(), "showFile", this))
    , m_markComplete(makeButton(tr("Mark as Complete"), "markComplete", this))
    , m_rollBack(makeButton(tr("Roll Back..."), "rollBack", this))
    , m_retryRollBack(makeButton(tr("Retry Roll Back..."), "retryRollBack", this))
    , m_undo(makeButton(tr("Undo Batch..."), "undoBatch", this))
{
    setObjectName(QStringLiteral("historyWindow"));
    setWindowTitle(tr("Rename History"));
    setModal(false);

    m_batches->setObjectName(QStringLiteral("historyBatches"));
    m_batches->setModel(m_model);
    m_batches->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_batches->setSelectionMode(QAbstractItemView::SingleSelection);
    m_batches->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_batches->verticalHeader()->hide();
    m_batches->setShowGrid(false);
    m_batches->setTextElideMode(Qt::ElideMiddle);
    m_batches->setWordWrap(false);
    // The folder is the useful part: it gets the free width (ui-design 10.1).
    QHeaderView *header = m_batches->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(HistoryModel::Folders, QHeaderView::Stretch);

    m_operations->setHorizontalHeaderLabels({tr("Old name"), tr("New name"), tr("Result")});
    m_operationTable->setObjectName(QStringLiteral("historyOperations"));
    m_operationTable->setModel(m_operations);
    m_operationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_operationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_operationTable->verticalHeader()->hide();
    m_operationTable->setShowGrid(false);
    m_operationTable->horizontalHeader()->setStretchLastSection(true);
    m_operationTable->setTextElideMode(Qt::ElideMiddle);

    m_detail->setObjectName(QStringLiteral("historyDetail"));
    m_detail->setWordWrap(true);
    m_detail->setTextFormat(Qt::PlainText);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_loadRules->setFlat(true); // tertiary

    auto *detailPane = new QWidget(this);
    auto *detailLayout = new QVBoxLayout(detailPane);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(m_detail);
    detailLayout->addWidget(m_detailError);
    detailLayout->addWidget(m_detailKeep);
    detailLayout->addWidget(m_operationTable, 1);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_batches);
    splitter->addWidget(detailPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setChildrenCollapsible(false);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_loadRules);
    buttons->addWidget(m_clear);
    buttons->addStretch(1);
    buttons->addWidget(m_showFile);
    buttons->addWidget(m_markComplete);
    buttons->addWidget(m_rollBack);
    buttons->addWidget(m_retryRollBack);
    buttons->addWidget(m_undo);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addLayout(buttons);
    resize(760, 560);

    connect(m_batches->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &HistoryWindow::selectionChanged);
    connect(m_model, &QAbstractItemModel::modelReset, this, &HistoryWindow::selectionChanged);
    const auto withId = [this](void (HistoryWindow::*signal)(const QString &)) {
        return [this, signal] {
            const QString id = selectedBatchId();
            if (!id.isEmpty())
                emit(this->*signal)(id);
        };
    };
    connect(m_undo, &QPushButton::clicked, this, withId(&HistoryWindow::undoBatchClicked));
    connect(m_rollBack, &QPushButton::clicked, this, withId(&HistoryWindow::rollBackClicked));
    connect(m_retryRollBack, &QPushButton::clicked, this, withId(&HistoryWindow::rollBackClicked));
    connect(m_markComplete, &QPushButton::clicked, this,
            withId(&HistoryWindow::markCompleteClicked));
    connect(m_loadRules, &QPushButton::clicked, this, withId(&HistoryWindow::loadRulesClicked));
    connect(m_clear, &QPushButton::clicked, this, &HistoryWindow::clearHistoryClicked);
    connect(m_showFile, &QPushButton::clicked, this, &HistoryWindow::showFile);
    selectionChanged();
}

void HistoryWindow::refresh()
{
    const QString selected = selectedBatchId();
    m_model->setBatches(m_store.list());
    selectBatch(selected.isEmpty() && m_model->rowCount() > 0 ? m_model->batch(0).batchId
                                                              : selected);
}

void HistoryWindow::selectBatch(const QString &batchId)
{
    const int row = m_model->rowOf(batchId);
    if (row < 0) {
        m_batches->clearSelection();
        m_batches->setCurrentIndex({});
        selectionChanged();
        return;
    }
    m_batches->selectRow(row);
    m_batches->setCurrentIndex(m_model->index(row, 0));
    selectionChanged();
}

QString HistoryWindow::selectedBatchId() const
{
    const QModelIndex current = m_batches->currentIndex();
    return current.isValid() ? m_model->batch(current.row()).batchId : QString();
}

const HistoryModel *HistoryWindow::model() const
{
    return m_model;
}

void HistoryWindow::setBusy(bool busy)
{
    m_busy = busy;
    selectionChanged();
}

void HistoryWindow::selectionChanged()
{
    const QModelIndex current = m_batches->currentIndex();
    showDetail(current.isValid() ? &m_model->batch(current.row()) : nullptr);
    m_clear->setEnabled(!m_busy && m_model->rowCount() > 0);
}

void HistoryWindow::showDetail(const BatchSummary *s)
{
    m_operations->removeRows(0, m_operations->rowCount());
    m_detailError->clear();
    m_detailError->hide();
    m_detailKeep->hide();
    m_operationTable->setVisible(!(s && s->damaged));
    QPushButton *primary = nullptr;
    const bool damaged = s && s->damaged;
    const bool undoBatch = s && !damaged && !s->undoOf.isEmpty();
    const BatchState state = s ? s->state : BatchState::Complete;
    const bool normal = s && !damaged && !undoBatch;
    const bool canUndo =
            normal && (state == BatchState::Complete || state == BatchState::PartlyUndone);
    const bool canRetry = s && !damaged && state == BatchState::Incomplete;
    const bool inProgress = s && !damaged && state == BatchState::InProgress;
    m_undo->setVisible(canUndo);
    m_retryRollBack->setVisible(canRetry);
    m_rollBack->setVisible(inProgress);
    m_markComplete->setVisible(inProgress);
    m_showFile->setVisible(damaged);
    m_loadRules->setVisible(normal);
    for (QPushButton *b : {m_undo, m_retryRollBack, m_rollBack, m_markComplete, m_loadRules})
        b->setEnabled(!m_busy);
    if (canUndo)
        primary = m_undo;
    else if (canRetry)
        primary = m_retryRollBack;
    else if (inProgress)
        primary = m_rollBack;
    // The others first: in a QDialog, a new default button resets the old one.
    for (QPushButton *b : {m_undo, m_retryRollBack, m_rollBack}) {
        if (b != primary)
            setPrimary(b, false);
    }
    if (primary)
        setPrimary(primary, true);

    if (!s) {
        m_detail->setText(m_model->rowCount() == 0 ? tr("No batches yet.") : QString());
        return;
    }
    if (damaged) {
        m_detail->setText(tr("CTRM cannot read the history file of this batch. It cannot undo or "
                             "roll back this batch."));
        m_detailError->setText(s->error);
        m_detailError->setVisible(!s->error.isEmpty());
        m_detailKeep->setText(tr("CTRM keeps this file. To remove it, show it in the file manager "
                                 "and delete it there."));
        m_detailKeep->show();
        return;
    }

    QString error;
    const std::optional<BatchRecord> rec = m_store.load(s->batchId, &error, ReadMode::BestEffort);
    QStringList lines;
    if (undoBatch) {
        lines.append(tr("This batch is an undo. It cannot be undone."));
    } else {
        QStringList names;
        if (rec) {
            const PresetParseResult parsed =
                    parsePreset(QJsonDocument(rec->header.chain).toJson(QJsonDocument::Compact));
            if (parsed.preset) {
                for (int i = 0; i < parsed.preset->chain.size(); ++i)
                    names.append(ruleDisplayName(parsed.preset->chain.at(i)->type()));
            }
        }
        const QString when = HistoryModel::whenText(s->started, QDateTime::currentDateTime());
        lines.append(
                names.isEmpty()
                        ? tr("Batch: %1").arg(when)
                        : tr("Batch: %1 - rules: %2").arg(when, names.join(QStringLiteral(", "))));
    }
    if (state == BatchState::InProgress && !s->openSteps.isEmpty()) {
        // Lead condition: the user sees each open step before Mark as Complete.
        lines.append(tr("These renames were running when CTRM stopped. Check them before you mark "
                        "the batch as complete:"));
        for (const OpenStep &step : s->openSteps) {
            QString line = QStringLiteral("    %1  →  %2")
                                   .arg(HistoryModel::displayPath(step.from),
                                        HistoryModel::displayPath(step.to));
            if (step.fromIsTemp || step.toIsTemp)
                line += u' ' + tr("(temporary name)");
            lines.append(line);
        }
    }
    m_detail->setText(lines.join(u'\n'));
    if (!rec)
        return;

    QHash<int, QString> failed;
    for (const StepRecord &r : rec->steps) {
        if (!r.ok && r.phase == Phase::Forward)
            failed.insert(r.step, r.errorText);
    }
    const QList<ReportRow> rows = buildReport(rec->plan, rec->steps, failed);
    for (int i = 0; i < rows.size() && i < rec->plan.requests.size(); ++i) {
        const RenameRequest &req = rec->plan.requests.at(i);
        auto *oldName = new QStandardItem(req.oldName);
        oldName->setToolTip(HistoryModel::displayPath(rows.at(i).oldPath));
        auto *newName = new QStandardItem(req.newName);
        newName->setToolTip(HistoryModel::displayPath(rows.at(i).finalPath));
        auto *result = new QStandardItem(ReportDialog::resultText(rows.at(i)));
        m_operations->appendRow({oldName, newName, result});
    }
}

void HistoryWindow::showFile()
{
    const QModelIndex current = m_batches->currentIndex();
    if (!current.isValid())
        return;
    const QString path = m_model->batch(current.row()).filePath;
    // Opens the history folder; the file manager shows the journal file there.
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

} // namespace ctrm::app
