// SPDX-License-Identifier: GPL-3.0-or-later
#include "version.h"

#include <QApplication>
#include <QMainWindow>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationDomain(QStringLiteral("ctrm.app"));
    QApplication::setApplicationName(QStringLiteral("CTRM"));
    QApplication::setApplicationVersion(QStringLiteral(CTRM_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral(CTRM_APP_ID));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("CTRM"));
    window.show();

    return QApplication::exec();
}
