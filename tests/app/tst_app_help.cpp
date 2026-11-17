// SPDX-License-Identifier: GPL-3.0-or-later
#include "actions.h"
#include "appcontext.h"
#include "controllers/helpcontroller.h"
#include "dialogs/aboutdialog.h"
#include "dialogs/reportproblemdialog.h"
#include "fs/filesystem.h"
#include "mainwindow.h"
#include "rules/replaceeditor.h"
#include "version.h"
#include "widgets/rulepanel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace ctrm;
using namespace ctrm::app;

namespace {

struct Fixture
{
    Fixture()
        : qsettings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat)
        , ctx(createNativeFileSystem(), dir.filePath(QStringLiteral("history")),
              dir.filePath(QStringLiteral("presets")), &qsettings)
        , window(ctx)
        , help(window, &window)
    {
        help.openUrl = [this](const QUrl &url) {
            opened.append(url);
            return openResult;
        };
        help.showError = [this](const QString &text) { errors.append(text); };
        help.runDialog = [this](QDialog &dialog) {
            dialogs.append(dialog.objectName());
            if (onDialog)
                onDialog(dialog);
        };
    }

    QTemporaryDir dir;
    QSettings qsettings;
    AppContext ctx;
    MainWindow window;
    HelpController help;
    QList<QUrl> opened;
    QStringList errors;
    QStringList dialogs;
    bool openResult = true;
    std::function<void(QDialog &)> onDialog;
};

} // namespace

class TestAppHelp : public QObject
{
    Q_OBJECT
private slots:
    void helpOpensBrowser();
    void regexHelpFromEditorLink();
    void browserFailureShowsAddress();
    void aboutShowsBrand();
    void reportProblemCopiesAndOpens();
};

void TestAppHelp::helpOpensBrowser()
{
    // M03: every Help item does something. The browser opens the pages (no network in CTRM).
    Fixture f;
    f.window.actions().help->trigger();
    QCOMPARE(f.opened, QList<QUrl>{HelpLinks::help()});
    for (const QUrl &url : {HelpLinks::help(), HelpLinks::regexHelp(), HelpLinks::reportProblem()})
        QVERIFY2(url.isValid() && url.scheme() == QStringLiteral("https"),
                 qPrintable(url.toString()));
    QVERIFY(f.errors.isEmpty());
}

void TestAppHelp::regexHelpFromEditorLink()
{
    Fixture f;
    f.window.actions()
            .addRuleMenu->findChild<QAction *>(QStringLiteral("addRule_replace"))
            ->trigger();
    auto *editor = f.window.rulePanel()->findChild<ReplaceEditor *>();
    QVERIFY(editor);
    emit editor->syntaxHelpRequested();
    QCOMPARE(f.opened, QList<QUrl>{HelpLinks::regexHelp()});
}

void TestAppHelp::browserFailureShowsAddress()
{
    Fixture f;
    f.openResult = false;
    f.window.actions().regexHelp->trigger();
    QCOMPARE(f.errors.size(), 1);
    QVERIFY(f.errors.first().contains(HelpLinks::regexHelp().toString()));
}

void TestAppHelp::aboutShowsBrand()
{
    Fixture f;
    QString name;
    QString version;
    bool hasIcon = false;
    f.onDialog = [&](QDialog &d) {
        name = d.findChild<QLabel *>(QStringLiteral("aboutName"))->text();
        version = d.findChild<QLabel *>(QStringLiteral("aboutVersion"))->text();
        hasIcon = !d.findChild<QLabel *>(QStringLiteral("aboutIcon"))->pixmap().isNull();
    };
    f.window.actions().about->trigger();
    QCOMPARE(f.dialogs, QStringList{QStringLiteral("aboutDialog")});
    QCOMPARE(name, QStringLiteral("Cheesy Toast Rename Manager"));
    QVERIFY(version.contains(QStringLiteral(CTRM_VERSION)));
    QVERIFY(hasIcon);
}

void TestAppHelp::reportProblemCopiesAndOpens()
{
    Fixture f;
    QString shown;
    f.onDialog = [&](QDialog &d) {
        shown = d.findChild<QPlainTextEdit *>(QStringLiteral("reportProblemInfo"))->toPlainText();
        d.findChild<QPushButton *>(QStringLiteral("copyInformation"))->click();
        auto *open = d.findChild<QPushButton *>(QStringLiteral("openIssuePage"));
        QVERIFY(open->isDefault());
        open->click();
    };
    f.window.actions().reportProblem->trigger();
    QCOMPARE(f.dialogs, QStringList{QStringLiteral("reportProblemDialog")});
    QCOMPARE(shown, problemReportInfo());
    QVERIFY(shown.contains(QStringLiteral("CTRM " CTRM_VERSION)));
    QVERIFY(shown.contains(QString::fromLatin1(qVersion())));
    QCOMPARE(QApplication::clipboard()->text(), shown);
    QCOMPARE(f.opened, QList<QUrl>{HelpLinks::reportProblem()});
}

QTEST_MAIN(TestAppHelp)
#include "tst_app_help.moc"
