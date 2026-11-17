// SPDX-License-Identifier: GPL-3.0-or-later
#include "historycontroller.h"

namespace ctrm::app {

HistoryController::HistoryController(MainWindow &w, AppContext &ctx, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(w);
    Q_UNUSED(ctx);
}

} // namespace ctrm::app
