// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

namespace ctrm::app {

class AppContext;
class MainWindow;

// Runs a batch from the main window (ui-design 9.4 to 9.8). Task 14 fills it in.
class BatchController : public QObject
{
    Q_OBJECT
public:
    BatchController(MainWindow &w, AppContext &ctx, QObject *parent);
};

} // namespace ctrm::app
