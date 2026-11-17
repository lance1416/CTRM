// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QLockFile>
#include <QString>
#include <QStringList>

namespace ctrm::app {

class PreferencesController;
class RecoveryController;

// One CTRM at a time for a user. A second CTRM could see the journal of a running batch as
// in-progress and offer Roll Back while the first CTRM still renames (PRD 7.4).
class InstanceLock
{
public:
    // The lock file is in folder (the app data folder).
    explicit InstanceLock(const QString &folder);

    // True when this process holds the lock now. A lock of a process that no longer runs is
    // stale: tryLock takes it. The lock stays until the object goes.
    bool tryLock();
    QString filePath() const;

private:
    QString m_path;
    QLockFile m_lock;
};

// The message for a second CTRM (ui-design 9.11). items is the number of files and folders
// the second start got (Open With): they are not added, and the message says so.
QString alreadyOpenText();
QString alreadyOpenInformativeText(int items);
// The arguments that are items, not options (QApplication removes its own options).
int itemArgumentCount(const QStringList &arguments);
// Shows that CTRM is already open (before any other window).
void showAlreadyOpen(int items);

// The checks at start, before the user can use the window: first the recovery prompt for
// the batches that did not end, then history retention (ui-design 9.10, 10.1). Retention
// never deletes an in-progress, incomplete or damaged batch.
void runStartChecks(RecoveryController &recovery, PreferencesController &preferences,
                    const QDateTime &now = QDateTime::currentDateTimeUtc());

} // namespace ctrm::app
