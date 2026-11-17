// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"

#include "exec/batchservice.h"
#include "exec/historystore.h"
#include "fs/filesystem.h"
#include "presets/presetstore.h"
#include "version.h"

#include <QDir>
#include <QStandardPaths>

namespace ctrm::app {

namespace {

QString makeFolder(const QString &path)
{
    if (!QDir().mkpath(path))
        qWarning("Cannot make the folder %s", qUtf8Printable(QDir::toNativeSeparators(path)));
    return path;
}

} // namespace

AppContext::AppContext()
    : AppContext(createNativeFileSystem(),
                 QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                         QStringLiteral("/history"),
                 QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                         QStringLiteral("/presets"),
                 nullptr)
{}

AppContext::AppContext(std::unique_ptr<ctrm::FileSystem> fs, const QString &historyDir,
                       const QString &presetsDir, QSettings *settings)
    : m_fs(std::move(fs))
    , m_historyDir(makeFolder(historyDir))
    , m_presetsDir(makeFolder(presetsDir))
    , m_settings(settings)
    , m_history(std::make_unique<ctrm::HistoryStore>(m_historyDir))
    , m_presets(std::make_unique<ctrm::PresetStore>(m_presetsDir))
    , m_appInfo(std::make_unique<ctrm::AppInfo>(
              ctrm::AppInfo{QStringLiteral(CTRM_VERSION), QString()}))
    , m_batchService(std::make_unique<ctrm::BatchService>(*m_fs, *m_history, *m_appInfo))
{}

AppContext::~AppContext() = default;

ctrm::FileSystem &AppContext::fileSystem()
{
    return *m_fs;
}

QString AppContext::historyDir() const
{
    return m_historyDir;
}

QString AppContext::presetsDir() const
{
    return m_presetsDir;
}

Settings &AppContext::settings()
{
    return m_settings;
}

ctrm::HistoryStore &AppContext::history()
{
    return *m_history;
}

ctrm::PresetStore &AppContext::presets()
{
    return *m_presets;
}

ctrm::BatchService &AppContext::batchService()
{
    return *m_batchService;
}

const ctrm::AppInfo &AppContext::appInfo() const
{
    return *m_appInfo;
}

} // namespace ctrm::app
