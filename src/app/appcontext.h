// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "settings.h"

#include <QString>

#include <memory>

class QSettings;

namespace ctrm {
class FileSystem;
}

namespace ctrm::app {

// The services that the windows share. Task 13 adds the batch service, history, presets and
// app information.
class AppContext
{
public:
    // Native file system. History dir: AppDataLocation + "/history". Presets dir:
    // AppConfigLocation + "/presets". The constructor makes both folders.
    AppContext();
    // For tests. The constructor makes both folders. It does not own settings.
    AppContext(std::unique_ptr<ctrm::FileSystem> fs, const QString &historyDir,
               const QString &presetsDir, QSettings *settings);
    ~AppContext();
    AppContext(const AppContext &) = delete;
    AppContext &operator=(const AppContext &) = delete;

    ctrm::FileSystem &fileSystem();
    QString historyDir() const;
    QString presetsDir() const;
    Settings &settings();

private:
    std::unique_ptr<ctrm::FileSystem> m_fs;
    QString m_historyDir;
    QString m_presetsDir;
    Settings m_settings;
};

} // namespace ctrm::app
