// SPDX-License-Identifier: GPL-3.0-or-later
#include "recoverycontroller.h"

namespace ctrm::app {

RecoveryController::RecoveryController(MainWindow &w, AppContext &ctx, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(w);
    Q_UNUSED(ctx);
}

} // namespace ctrm::app
