// SPDX-License-Identifier: GPL-3.0-or-later
#include "preferencescontroller.h"

#include "actions.h"
#include "appcontext.h"
#include "dialogs/preferencesdialog.h"
#include "exec/historystore.h"
#include "mainwindow.h"

#include <QAction>

namespace ctrm::app {

PreferencesController::PreferencesController(MainWindow &w, AppContext &ctx, QObject *parent)
    : QObject(parent)
    , m_window(w)
    , m_ctx(ctx)
{
    connect(w.actions().preferences, &QAction::triggered, this,
            &PreferencesController::showPreferences);
    // The View menu item and the dialog show the same setting.
    connect(w.actions().highlightChanged, &QAction::toggled, this, [this] {
        if (m_dialog)
            m_dialog->syncHighlight();
    });
}

void PreferencesController::showPreferences()
{
    if (!m_dialog) {
        m_dialog = new PreferencesDialog(m_ctx.settings(), PreferencesDialog::platformMode(),
                                         &m_window);
        connect(m_dialog, &PreferencesDialog::settingsChanged, this, [this] {
            m_window.actions().highlightChanged->setChecked(m_ctx.settings().highlightChanged());
        });
    }
    if (!m_dialog->isVisible())
        m_dialog->load();
    if (PreferencesDialog::platformMode() == PreferencesDialog::ApplyMode::Immediately) {
        m_dialog->show();
        m_dialog->raise();
        m_dialog->activateWindow();
    } else {
        m_dialog->open();
    }
}

int PreferencesController::applyRetention(const QDateTime &now)
{
    const Settings &s = m_ctx.settings();
    return m_ctx.history().applyRetention(s.retentionCount(), s.retentionDays(), now);
}

} // namespace ctrm::app
