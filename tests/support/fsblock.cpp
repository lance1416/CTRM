// SPDX-License-Identifier: GPL-3.0-or-later
#include "fsblock.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#if defined(Q_OS_WIN)
#include <QProcess>

#include <qt_windows.h>
#endif

namespace {

#if defined(Q_OS_WIN)
QString longNative(const QString &path)
{
    return QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(QDir::cleanPath(path));
}

LPCWSTR wstr(const QString &s)
{
    return reinterpret_cast<LPCWSTR>(s.utf16());
}

// icacls with the SID of Everyone: the name "Everyone" is different in each language.
bool icacls(const QString &folder, const QStringList &change)
{
    QProcess process;
    process.start(QStringLiteral("icacls"), QStringList{QDir::toNativeSeparators(folder)} + change);
    return process.waitForFinished() && process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}
#else
constexpr QFileDevice::Permissions kOwnerAll =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
#endif

} // namespace

RenameBlock::RenameBlock(const QString &itemPath)
{
#if defined(Q_OS_WIN)
    // No FILE_SHARE_DELETE: a rename needs DELETE access to the item.
    HANDLE h = CreateFileW(wstr(longNative(itemPath)), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    m_active = h != INVALID_HANDLE_VALUE;
    if (m_active)
        m_handle = h;
#else
    m_folder = QFileInfo(itemPath).absolutePath();
    m_active = QFile::setPermissions(m_folder, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
#endif
}

RenameBlock::~RenameBlock()
{
    if (!m_active)
        return;
#if defined(Q_OS_WIN)
    CloseHandle(static_cast<HANDLE>(m_handle));
#else
    QFile::setPermissions(m_folder, kOwnerAll);
#endif
}

UnlistableFolder::UnlistableFolder(QString folder)
    : m_folder(std::move(folder))
{
#if defined(Q_OS_WIN)
    // RD: "read data / list folder". A deny ACE applies to administrators too.
    m_active = icacls(m_folder, {QStringLiteral("/deny"), QStringLiteral("*S-1-1-0:(RD)")});
#else
    m_active = QFile::setPermissions(m_folder, QFileDevice::Permissions());
#endif
}

UnlistableFolder::~UnlistableFolder()
{
    if (!m_active)
        return;
#if defined(Q_OS_WIN)
    icacls(m_folder, {QStringLiteral("/remove:d"), QStringLiteral("*S-1-1-0")});
#else
    QFile::setPermissions(m_folder, kOwnerAll);
#endif
}

bool makeFolderLink(const QString &target, const QString &link)
{
#if defined(Q_OS_WIN)
    // mklink is a command of cmd.exe. The command line starts with "mklink", not a quote, so
    // cmd keeps the quotes around the paths.
    QProcess process;
    process.setProgram(QStringLiteral("cmd.exe"));
    process.setNativeArguments(
            QStringLiteral("/c mklink /J \"%1\" \"%2\"")
                    .arg(QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)));
    process.start();
    return process.waitForFinished() && process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
#else
    return QFile::link(target, link);
#endif
}

bool setModificationTimeShared(const QString &path, const QDateTime &time)
{
#if defined(Q_OS_WIN)
    HANDLE h = CreateFileW(wstr(longNative(path)), FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    // FILETIME: 100 ns steps since 1601-01-01.
    constexpr qint64 kEpochDiff100ns = 116444736000000000LL;
    const qint64 ticks = time.toMSecsSinceEpoch() * 10000 + kEpochDiff100ns;
    FILETIME ft;
    ft.dwLowDateTime = DWORD(ticks & 0xFFFFFFFF);
    ft.dwHighDateTime = DWORD(quint64(ticks) >> 32);
    const bool ok = SetFileTime(h, nullptr, nullptr, &ft) != 0;
    CloseHandle(h);
    return ok;
#else
    QFile file(path);
    return file.open(QIODevice::ReadWrite) &&
           file.setFileTime(time, QFileDevice::FileModificationTime);
#endif
}
