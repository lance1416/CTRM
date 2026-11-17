// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QUrl>

#include <functional>

class QDialog;

namespace ctrm::app {

class MainWindow;

// The web pages of the Help menu. CTRM itself makes no network connection (AC-G2): the
// browser opens them.
struct HelpLinks
{
    static QUrl help();
    static QUrl regexHelp();
    static QUrl reportProblem();
};

// The Help menu (ui-design 13, 17): CTRM Help, Regular Expression Help, Report a Problem...
// and About CTRM.
class HelpController : public QObject
{
    Q_OBJECT
public:
    HelpController(MainWindow &w, QObject *parent);

    // Test hooks. The defaults use QDesktopServices::openUrl, QDialog::exec() and
    // QMessageBox::warning.
    std::function<bool(const QUrl &url)> openUrl;
    std::function<void(QDialog &dialog)> runDialog;
    std::function<void(const QString &text)> showError;

    void showAbout();
    void showReportProblem();

private:
    void open(const QUrl &url);

    MainWindow &m_window;
};

// The information for a problem report: the CTRM, Qt and OS versions. No file names.
QString problemReportInfo();

} // namespace ctrm::app
