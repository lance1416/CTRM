// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"
#include "controllers/batchcontroller.h"
#include "controllers/historycontroller.h"
#include "controllers/preferencescontroller.h"
#include "controllers/presetcontroller.h"
#include "controllers/recoverycontroller.h"
#include "mainwindow.h"
#include "startup.h"
#include "theme/icons.h"
#include "version.h"

#include <QApplication>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationDomain(QStringLiteral("ctrm.app"));
    QApplication::setApplicationName(QStringLiteral("CTRM"));
    QApplication::setApplicationVersion(QStringLiteral(CTRM_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral(CTRM_APP_ID));

#ifdef Q_OS_LINUX
    // Use the platform style. Use Fusion only when the platform theme gives no style.
    if (QApplication::style()->name().isEmpty())
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
#endif

    QApplication::setWindowIcon(ctrm::app::appIcon());

    // One CTRM at a time, before the history is read (no recovery check in a second CTRM).
    ctrm::app::InstanceLock lock(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    if (!lock.tryLock()) {
        ctrm::app::showAlreadyOpen(ctrm::app::itemArgumentCount(QApplication::arguments()));
        return 0;
    }

    ctrm::app::AppContext context;
    ctrm::app::MainWindow window(context);
    ctrm::app::BatchController batch(window, context, &window);
    ctrm::app::HistoryController history(window, context, batch, &window);
    ctrm::app::RecoveryController recovery(window, context, history, &window);
    ctrm::app::PresetController presets(window, context, &window);
    ctrm::app::PreferencesController preferences(window, context, &window);
    window.show();
    // Before the user can use the window (ui-design 9.10): recovery, then history retention.
    QMetaObject::invokeMethod(
            &window, [&] { ctrm::app::runStartChecks(recovery, preferences); },
            Qt::QueuedConnection);

    return QApplication::exec();
}
