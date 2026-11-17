// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

namespace ctrm::app {

class AppContext;
class MainWindow;

// The Preferences dialog (ui-design 16). Task 18 fills it in.
class PreferencesController : public QObject
{
    Q_OBJECT
public:
    PreferencesController(MainWindow &w, AppContext &ctx, QObject *parent);
};

} // namespace ctrm::app
