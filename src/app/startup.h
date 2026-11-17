// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>

namespace ctrm::app {

class PreferencesController;
class RecoveryController;

// The checks at start, before the user can use the window: first the recovery prompt for
// the batches that did not end, then history retention (ui-design 9.10, 10.1). Retention
// never deletes an in-progress, incomplete or damaged batch.
void runStartChecks(RecoveryController &recovery, PreferencesController &preferences,
                    const QDateTime &now = QDateTime::currentDateTimeUtc());

} // namespace ctrm::app
