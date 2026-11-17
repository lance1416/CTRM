// SPDX-License-Identifier: GPL-3.0-or-later
#include "preferencescontroller.h"

namespace ctrm::app {

PreferencesController::PreferencesController(MainWindow &w, AppContext &ctx, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(w);
    Q_UNUSED(ctx);
}

} // namespace ctrm::app
