// SPDX-License-Identifier: GPL-3.0-or-later
// Native file system for Windows (core Task 12). CMake compiles this file only for WIN32.
#include <QtGlobal>

#if defined(Q_OS_WIN)

#include "fs/filesystem.h"

#include <QDir>

#include <cstring>
#include <qt_windows.h>

namespace ctrm {

namespace {

// Values from the Windows 10 SDK. They are defined here because the SDK declares them only for
// newer NTDDI_VERSION values than the one that Qt sets.
constexpr auto kFileIdInfo = static_cast<FILE_INFO_BY_HANDLE_CLASS>(18);
constexpr auto kFileCaseSensitiveInfo = static_cast<FILE_INFO_BY_HANDLE_CLASS>(23);
constexpr ULONG kCaseSensitiveDirFlag = 0x00000001; // FILE_CS_FLAG_CASE_SENSITIVE_DIR

struct IdInfo // FILE_ID_INFO
{
    ULONGLONG volumeSerialNumber;
    BYTE fileId[16]; // FILE_ID_128
};
static_assert(sizeof(IdInfo) == 24);

struct CaseSensitiveInfo // FILE_CASE_SENSITIVE_INFO
{
    ULONG flags;
};

// 100-ns intervals between 1601-01-01 and 1970-01-01.
constexpr qint64 kEpochDiff100ns = 116444736000000000LL;

class Handle
{
public:
    explicit Handle(HANDLE h)
        : m_h(h)
    {}
    ~Handle()
    {
        if (valid())
            CloseHandle(m_h);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    bool valid() const { return m_h != INVALID_HANDLE_VALUE && m_h != nullptr; }
    HANDLE get() const { return m_h; }

private:
    HANDLE m_h;
};

// Absolute path with '/' separators -> "\\?\C:\..." or "\\?\UNC\server\share\...".
// The "\\?\" form turns off Win32 path parsing: no MAX_PATH limit, no change of "." or "..",
// trailing spaces or dots. Thus the path is cleaned first.
QString toWinPath(const QString &path)
{
    QString p = QDir::toNativeSeparators(QDir::cleanPath(path));
    if (p.startsWith(QStringLiteral("\\\\?\\")))
        return p;
    if (p.size() == 2 && p.at(1) == u':')
        p += u'\\'; // "C:" is the current folder of drive C, not its root
    if (p.startsWith(QStringLiteral("\\\\")))
        return QStringLiteral("\\\\?\\UNC\\") + p.mid(2);
    return QStringLiteral("\\\\?\\") + p;
}

LPCWSTR wstr(const QString &s)
{
    return reinterpret_cast<LPCWSTR>(s.utf16());
}

QString errorText(DWORD code)
{
    LPWSTR buffer = nullptr;
    const DWORD len =
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text;
    if (len > 0 && buffer)
        text = QString::fromWCharArray(buffer, int(len)).trimmed();
    if (buffer)
        LocalFree(buffer);
    if (text.isEmpty())
        text = QStringLiteral("Windows error %1").arg(code);
    return text;
}

// Opens the item itself (a symbolic link is not followed). Folders need backup semantics.
HANDLE openItem(const QString &path)
{
    return CreateFileW(wstr(toWinPath(path)), FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                       nullptr);
}

bool isStrongFs(const QString &fsName)
{
    return fsName.compare(QStringLiteral("NTFS"), Qt::CaseInsensitive) == 0 ||
           fsName.compare(QStringLiteral("ReFS"), Qt::CaseInsensitive) == 0;
}

// The name must survive a UTF-8 round trip (journal, presets). NTFS permits unpaired surrogates.
bool roundTripsUtf8(const QString &name)
{
    return QString::fromUtf8(name.toUtf8()) == name;
}

class WinFileSystem : public FileSystem
{
public:
    VolumeInfo volumeInfo(const QString &folder) override
    {
        VolumeInfo info;
        const Handle h(openItem(folder));
        if (!h.valid())
            return info;

        wchar_t fsName[MAX_PATH + 1] = {};
        if (GetVolumeInformationByHandleW(h.get(), nullptr, 0, nullptr, nullptr, nullptr, fsName,
                                          MAX_PATH + 1))
            info.fsName = QString::fromWCharArray(fsName);
        info.identityType = isStrongFs(info.fsName) ? IdentityType::Strong : IdentityType::Weak;
        info.normalizationInsensitive = false;

        CaseSensitiveInfo cs{};
        if (GetFileInformationByHandleEx(h.get(), kFileCaseSensitiveInfo, &cs, sizeof(cs))) {
            info.caseSensitivity = (cs.flags & kCaseSensitiveDirFlag) != 0
                                           ? CaseSensitivity::Sensitive
                                           : CaseSensitivity::Insensitive;
        } else {
            info.caseSensitivity = isStrongFs(info.fsName) ? CaseSensitivity::Insensitive
                                                           : CaseSensitivity::Unknown;
        }
        return info;
    }

    std::optional<QList<DirEntry>> listFolder(const QString &folder, QString *error) override
    {
        QString pattern = toWinPath(folder);
        if (!pattern.endsWith(u'\\'))
            pattern += u'\\';
        pattern += u'*';

        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileExW(wstr(pattern), FindExInfoBasic, &data, FindExSearchNameMatch,
                                       nullptr, FIND_FIRST_EX_LARGE_FETCH);
        QList<DirEntry> entries;
        if (find == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            // An empty drive root has no "." entry.
            if (code == ERROR_FILE_NOT_FOUND)
                return entries;
            if (error)
                *error = errorText(code);
            return std::nullopt;
        }

        DWORD code = ERROR_SUCCESS;
        do {
            const QString name = QString::fromWCharArray(data.cFileName);
            if (name == u"." || name == u"..")
                continue;
            DirEntry e;
            e.name = name;
            e.isFolder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            e.isSymlink = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                          data.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
            e.hiddenAttribute = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
            e.nameSupported = roundTripsUtf8(name);
            entries.append(e);
        } while (FindNextFileW(find, &data));
        code = GetLastError();
        FindClose(find);

        if (code != ERROR_NO_MORE_FILES) {
            if (error)
                *error = errorText(code);
            return std::nullopt;
        }
        return entries;
    }

    bool exists(const QString &path) override
    {
        // GetFileAttributesW does not follow a symbolic link.
        if (GetFileAttributesW(wstr(toWinPath(path))) != INVALID_FILE_ATTRIBUTES)
            return true;
        const DWORD code = GetLastError();
        // When Windows cannot tell (access denied, sharing, a network share that does not
        // answer), report "exists": the safe answer.
        return code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND &&
               code != ERROR_INVALID_NAME && code != ERROR_INVALID_DRIVE;
    }

    std::optional<FileIdentity> identity(const QString &path, IdentityType type) override
    {
        const Handle h(openItem(path));
        if (!h.valid())
            return std::nullopt;

        FileIdentity id;
        id.type = type;
        if (type == IdentityType::Strong) {
            IdInfo info{};
            if (!GetFileInformationByHandleEx(h.get(), kFileIdInfo, &info, sizeof(info)))
                return std::nullopt;
            id.volume = info.volumeSerialNumber;
            // FILE_ID_128 is little-endian: bytes 0-7 are the low half.
            std::memcpy(&id.idLow, info.fileId, 8);
            std::memcpy(&id.idHigh, info.fileId + 8, 8);
            return id;
        }

        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(h.get(), &info))
            return std::nullopt;
        if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // A folder: type only (its time changes when an item in it is renamed).
            id.size = -1;
            return id;
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(h.get(), &size))
            return std::nullopt;
        id.size = size.QuadPart;
        const qint64 ft = (qint64(info.ftLastWriteTime.dwHighDateTime) << 32) |
                          qint64(info.ftLastWriteTime.dwLowDateTime);
        id.mtimeNs = (ft - kEpochDiff100ns) * 100;
        return id;
    }

    RenameResult renameNoReplace(const QString &from, const QString &to) override
    {
        RenameResult r;
        // Flags 0: fail when the target exists, and never copy to another volume.
        if (MoveFileExW(wstr(toWinPath(from)), wstr(toWinPath(to)), 0)) {
            r.ok = true;
            return r;
        }
        const DWORD code = GetLastError();
        r.errorCode = int(code);
        r.errorText = errorText(code);
        return r;
    }
};

} // namespace

std::unique_ptr<FileSystem> createNativeFileSystem()
{
    return std::make_unique<WinFileSystem>();
}

} // namespace ctrm

#endif // Q_OS_WIN
