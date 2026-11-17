// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dialogs/recoverydialog.h"
#include "exec/batchservice.h"

#include <QObject>

#include <functional>

namespace ctrm::app {

class AppContext;
class HistoryController;
class MainWindow;

// The crash recovery prompt at start (ui-design 9.10). Each in-progress batch is asked in
// turn, oldest first. CTRM never acts on a batch without a user decision (PRD 7.4).
class RecoveryController : public QObject
{
    Q_OBJECT
public:
    RecoveryController(MainWindow &w, AppContext &ctx, HistoryController &history, QObject *parent);

    // The dialogs. Tests replace them.
    std::function<RecoveryChoice(const RecoveryInfo &)> askRecovery; // default: RecoveryDialog
    std::function<void(const ctrm::FinishResult &, const QDateTime &started)> showReport;

    void checkAtStart(); // called by main.cpp before the user can use the window

private:
    RecoveryInfo infoOf(const ctrm::BatchSummary &summary) const;
    void showError(const QString &text);

    MainWindow &m_w;
    AppContext &m_ctx;
    HistoryController &m_history;
};

} // namespace ctrm::app
