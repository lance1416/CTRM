// SPDX-License-Identifier: GPL-3.0-or-later
#include "controllers/helpcontroller.h"

#include "actions.h"
#include "dialogs/aboutdialog.h"
#include "dialogs/reportproblemdialog.h"
#include "mainwindow.h"
#include "version.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QStyle>
#include <QSysInfo>

namespace ctrm::app {

// The one place for the web addresses of the Help menu.
QUrl HelpLinks::help()
{
    return QUrl(QStringLiteral("https://github.com/lance1416/CTRM#readme"));
}

QUrl HelpLinks::regexHelp()
{
    // QRegularExpression uses PCRE2.
    return QUrl(QStringLiteral("https://www.pcre.org/current/doc/html/pcre2syntax.html"));
}

QUrl HelpLinks::reportProblem()
{
    return QUrl(QStringLiteral("https://github.com/lance1416/CTRM/issues/new"));
}

QString problemReportInfo()
{
    return QStringLiteral("CTRM %1\nQt %2\n%3 (%4)\nStyle: %5")
            .arg(QStringLiteral(CTRM_VERSION), QString::fromLatin1(qVersion()),
                 QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
                 QApplication::style()->name());
}

HelpController::HelpController(MainWindow &w, QObject *parent)
    : QObject(parent)
    , m_window(w)
{
    openUrl = [](const QUrl &url) { return QDesktopServices::openUrl(url); };
    runDialog = [](QDialog &dialog) { dialog.exec(); };
    showError = [this](const QString &text) { QMessageBox::warning(&m_window, tr("CTRM"), text); };

    const Actions &a = w.actions();
    connect(a.help, &QAction::triggered, this, [this] { open(HelpLinks::help()); });
    connect(a.regexHelp, &QAction::triggered, this, [this] { open(HelpLinks::regexHelp()); });
    connect(a.reportProblem, &QAction::triggered, this, &HelpController::showReportProblem);
    connect(a.about, &QAction::triggered, this, &HelpController::showAbout);
}

void HelpController::open(const QUrl &url)
{
    if (!openUrl(url))
        showError(tr("CTRM cannot open the web browser. Open this address in your browser: %1")
                          .arg(url.toString()));
}

void HelpController::showAbout()
{
    AboutDialog dialog(&m_window);
    runDialog(dialog);
}

void HelpController::showReportProblem()
{
    ReportProblemDialog dialog(problemReportInfo(), &m_window);
    connect(&dialog, &ReportProblemDialog::openIssuePageClicked, this,
            [this] { open(HelpLinks::reportProblem()); });
    runDialog(dialog);
}

} // namespace ctrm::app
