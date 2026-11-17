// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dialogs/folderdropdialog.h"

#include <QObject>
#include <QStringList>

#include <functional>

class QWidget;

namespace ctrm {
class FileSystem;
}

namespace ctrm::app {

class ItemTableModel;
class Settings;

// Adds dropped and chosen items to the list (PRD 6.6, ui-design 9.9). Files are always added,
// also hidden ones. For folders, the user chooses one time for all: rename the folders
// themselves or add their direct contents.
class ItemAdder : public QObject
{
    Q_OBJECT
public:
    ItemAdder(ctrm::FileSystem &fs, ItemTableModel &model, Settings &settings, QObject *parent);

    // Called one time for each add with folders when the folder answer is Ask. The default shows
    // FolderDropDialog on the parent of the adder (when it is a widget) or the active window.
    std::function<FolderDropReply(const FolderDropQuestion &)> askFolderDrop;

    // Drops and Add Files. Absolute paths; the items are added in this order.
    void addPaths(const QStringList &paths);
    // QFileDialog::getExistingDirectory (native), then addPaths.
    void addFolderViaDialog(QWidget *parent);
    // QFileDialog::getOpenFileNames (native), then addPaths.
    void addFilesViaDialog(QWidget *parent);

signals:
    // Duplicates and items that were not added. Not sent when all items were added.
    void statusMessage(const QString &text);

private:
    ctrm::FileSystem &m_fs;
    ItemTableModel &m_model;
    Settings &m_settings;
};

} // namespace ctrm::app
