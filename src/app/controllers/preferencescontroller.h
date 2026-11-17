// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>

namespace ctrm::app {

class AppContext;
class MainWindow;
class PreferencesDialog;

// The Preferences dialog (ui-design 16) and the history retention.
class PreferencesController : public QObject
{
    Q_OBJECT
public:
    PreferencesController(MainWindow &w, AppContext &ctx, QObject *parent);

    // Shows the dialog: non-modal on macOS (changes apply at once), window-modal with OK and
    // Cancel on Windows and Linux. A second call shows the same dialog.
    void showPreferences();
    // Deletes old batches with the retention settings. Returns the number of files deleted.
    int applyRetention(const QDateTime &now = QDateTime::currentDateTimeUtc());

private:
    MainWindow &m_window;
    AppContext &m_ctx;
    QPointer<PreferencesDialog> m_dialog;
};

} // namespace ctrm::app
