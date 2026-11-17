// SPDX-License-Identifier: GPL-3.0-or-later
#include "startup.h"

#include "controllers/preferencescontroller.h"
#include "controllers/recoverycontroller.h"

namespace ctrm::app {

void runStartChecks(RecoveryController &recovery, PreferencesController &preferences,
                    const QDateTime &now)
{
    recovery.checkAtStart(); // the user decides each open batch first
    preferences.applyRetention(now);
}

} // namespace ctrm::app
