// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

namespace ctrm::app {

class AppContext;
class MainWindow;

// The crash recovery prompt at start (ui-design 9.10). Task 16 fills it in.
class RecoveryController : public QObject
{
    Q_OBJECT
public:
    RecoveryController(MainWindow &w, AppContext &ctx, QObject *parent);
};

} // namespace ctrm::app
