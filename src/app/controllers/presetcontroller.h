// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

namespace ctrm::app {

class AppContext;
class MainWindow;

// The Presets menu and Manage Presets (ui-design 11). Task 17 fills it in.
class PresetController : public QObject
{
    Q_OBJECT
public:
    PresetController(MainWindow &w, AppContext &ctx, QObject *parent);
};

} // namespace ctrm::app
