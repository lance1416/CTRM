// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"

#include "fs/filesystem.h"

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

} // namespace ctrm::app
