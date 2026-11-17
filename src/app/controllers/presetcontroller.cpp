// SPDX-License-Identifier: GPL-3.0-or-later
#include "presetcontroller.h"

namespace ctrm::app {

PresetController::PresetController(MainWindow &w, AppContext &ctx, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(w);
    Q_UNUSED(ctx);
}

} // namespace ctrm::app
