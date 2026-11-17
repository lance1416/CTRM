// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

namespace ctrm::app {

class AppContext;
class MainWindow;

// The History window, the undo preview and quick undo (ui-design 10). Task 15 fills it in.
class HistoryController : public QObject
{
    Q_OBJECT
public:
    HistoryController(MainWindow &w, AppContext &ctx, QObject *parent);
};

} // namespace ctrm::app
