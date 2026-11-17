// SPDX-License-Identifier: GPL-3.0-or-later
#include "startup.h"

#include "controllers/preferencescontroller.h"
#include "controllers/recoverycontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

namespace ctrm::app {

namespace {

QString tr(const char *text, int n = -1)
{
    return QCoreApplication::translate("ctrm::app::Startup", text, nullptr, n);
}

} // namespace

InstanceLock::InstanceLock(const QString &folder)
    : m_path(QDir(folder).filePath(QStringLiteral("ctrm.lock")))
    , m_lock(m_path)
{
    // Stale only when the process that holds the lock no longer runs. No time limit: a CTRM
    // that is open for a long time keeps its lock.
    m_lock.setStaleLockTime(0);
}

bool InstanceLock::tryLock()
{
    if (m_lock.isLocked())
        return true;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    return m_lock.tryLock(0);
}

QString InstanceLock::filePath() const
{
    return m_path;
}

QString alreadyOpenText()
{
    return tr("CTRM is already open.");
}

QString alreadyOpenInformativeText(int items)
{
    QString text = tr("Use the CTRM window that is open. Two open copies could rename the same "
                      "files at the same time.");
    if (items > 0) {
        // Two source texts, so that English has a singular with no translation file.
        text += QStringLiteral("\n\n") +
                (items == 1 ? tr("The item was not added. Add it in the open CTRM window.", items)
                            : tr("The items were not added. Add them in the open CTRM window.",
                                 items));
    }
    return text;
}

int itemArgumentCount(const QStringList &arguments)
{
    int count = 0;
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        if (!arguments.at(i).startsWith(u'-'))
            ++count;
    }
    return count;
}

void showAlreadyOpen(int items)
{
    QMessageBox box(QMessageBox::Information, QCoreApplication::applicationName(),
                    alreadyOpenText(), QMessageBox::Ok);
    box.setInformativeText(alreadyOpenInformativeText(items));
    box.setDefaultButton(QMessageBox::Ok);
    box.exec();
}

void runStartChecks(RecoveryController &recovery, PreferencesController &preferences,
                    const QDateTime &now)
{
    recovery.checkAtStart(); // the user decides each open batch first
    preferences.applyRetention(now);
}

} // namespace ctrm::app
