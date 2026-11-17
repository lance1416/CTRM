// SPDX-License-Identifier: GPL-3.0-or-later
#include "presets/presetstore.h"

#include "fs/itemsource.h"
#include "rules/graphemes.h"
#include "validate/namevalidator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

namespace ctrm {

namespace {

const QString kSuffix = QStringLiteral(".ctrm.json");
const QString kBuiltInDir = QStringLiteral(":/ctrm/presets/");
const QStringList kBuiltInFiles{QStringLiteral("lower-case-extensions"),
                                QStringLiteral("spaces-to-underscores"),
                                QStringLiteral("photo-numbering"), QStringLiteral("remove-tags")};
constexpr qsizetype kMaxBaseBytes = 200; // UTF-8 bytes; leaves space for " (N).ctrm.json"

bool isBuiltIn(const PresetEntry &entry)
{
    return entry.builtIn || entry.filePath.startsWith(QLatin1Char(':'));
}

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

bool refuseBuiltIn(const PresetEntry &entry, QString *error)
{
    if (!isBuiltIn(entry))
        return false;
    setError(error, QStringLiteral("You cannot change a built-in preset."));
    return true;
}

std::optional<QByteArray> readBytes(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error,
                 QStringLiteral("Cannot read the file \"%1\": %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    return file.readAll();
}

bool writeBytes(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(error,
                 QStringLiteral("Cannot write the file \"%1\": %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

std::filesystem::path nativePath(const QString &path)
{
    return std::filesystem::path(path.toStdU16String());
}

// True when both paths are the same file, also when only the letter case is different on a
// case-insensitive volume.
bool sameFile(const QString &a, const QString &b)
{
    std::error_code ec;
    return std::filesystem::equivalent(nativePath(a), nativePath(b), ec) && !ec;
}

// The file name without ".ctrm.json" (for a file that does not give a preset name).
QString baseName(const QString &path)
{
    QString name = QFileInfo(path).fileName();
    if (name.endsWith(kSuffix))
        name.chop(kSuffix.size());
    return name;
}

// The preset name as a file base name that is valid on all platforms.
QString fileBase(const QString &name)
{
    const NameValidator portable(OsRules::Windows, true);
    QString base = portable.replaceForbidden(name, QStringLiteral("_"));

    // Cut at a grapheme boundary so that the base has at most kMaxBaseBytes UTF-8 bytes.
    const QList<int> bounds = graphemeBoundaries(base);
    qsizetype bytes = 0;
    for (qsizetype i = 0; i + 1 < bounds.size(); ++i) {
        bytes += QStringView(base)
                         .sliced(bounds.at(i), bounds.at(i + 1) - bounds.at(i))
                         .toUtf8()
                         .size();
        if (bytes > kMaxBaseBytes) {
            base.truncate(bounds.at(i));
            break;
        }
    }

    if (base.isEmpty())
        base = QStringLiteral("Preset");
    if (portable.checkName(base + kSuffix).has_value()) // for example a reserved name: "CON"
        base.prepend(QLatin1Char('_'));
    return base;
}

} // namespace

PresetStore::PresetStore(QString userDir)
    : m_userDir(std::move(userDir))
{}

QList<PresetEntry> PresetStore::builtIns() const
{
    QList<PresetEntry> list;
    for (const QString &file : kBuiltInFiles) {
        PresetEntry entry{QString(), kBuiltInDir + file + kSuffix, true};
        const PresetParseResult parsed = load(entry);
        entry.name = parsed.preset ? parsed.preset->name : file;
        list.append(entry);
    }
    return list;
}

QList<PresetEntry> PresetStore::userPresets() const
{
    QList<PresetEntry> list;
    const QDir dir(m_userDir);
    for (const QString &file : dir.entryList({QStringLiteral("*") + kSuffix}, QDir::Files)) {
        PresetEntry entry{QString(), dir.filePath(file), false};
        const PresetParseResult parsed = load(entry);
        // A file that does not parse stays in the list, so that the user can see and remove it.
        entry.name = parsed.preset && !parsed.preset->name.isEmpty() ? parsed.preset->name
                                                                     : baseName(entry.filePath);
        list.append(entry);
    }
    std::stable_sort(list.begin(), list.end(), [](const PresetEntry &a, const PresetEntry &b) {
        if (naturalLess(a.name, b.name) || naturalLess(b.name, a.name))
            return naturalLess(a.name, b.name);
        return a.filePath < b.filePath;
    });
    return list;
}

PresetParseResult PresetStore::load(const PresetEntry &entry) const
{
    PresetParseResult result;
    const std::optional<QByteArray> bytes = readBytes(entry.filePath, &result.error);
    if (!bytes)
        return result;
    return parsePreset(*bytes);
}

std::optional<PresetEntry> PresetStore::save(const Preset &preset, const QString &filePath,
                                             QString *error)
{
    if (refuseBuiltIn(PresetEntry{preset.name, filePath, false}, error))
        return std::nullopt;
    QString path = filePath;
    if (path.isEmpty()) {
        if (!QDir().mkpath(m_userDir)) {
            setError(error, QStringLiteral("Cannot make the folder \"%1\".").arg(m_userDir));
            return std::nullopt;
        }
        path = newFilePath(preset.name);
    }
    if (!writeBytes(path, serializePreset(preset), error))
        return std::nullopt;
    return PresetEntry{preset.name, path, false};
}

bool PresetStore::remove(const PresetEntry &entry, QString *error)
{
    if (refuseBuiltIn(entry, error))
        return false;
    QFile file(entry.filePath);
    if (!file.remove()) {
        setError(error, QStringLiteral("Cannot remove the file \"%1\": %2")
                                .arg(entry.filePath, file.errorString()));
        return false;
    }
    return true;
}

std::optional<PresetEntry> PresetStore::rename(const PresetEntry &entry, const QString &newName,
                                               QString *error)
{
    if (refuseBuiltIn(entry, error))
        return std::nullopt;
    PresetParseResult loaded = load(entry);
    if (!loaded.preset) {
        setError(error, loaded.error);
        return std::nullopt;
    }
    Preset preset = std::move(*loaded.preset);
    preset.name = newName;

    const QString target = newFilePath(newName, entry.filePath);
    if (target == entry.filePath || sameFile(target, entry.filePath)) {
        // The same file: write in place, then change the letter case of the file name if needed.
        if (!save(preset, entry.filePath, error))
            return std::nullopt;
        if (target != entry.filePath) {
            std::error_code ec;
            std::filesystem::rename(nativePath(entry.filePath), nativePath(target), ec);
            if (ec) {
                setError(error, QStringLiteral("Cannot rename the file \"%1\": %2")
                                        .arg(entry.filePath, QString::fromStdString(ec.message())));
                return std::nullopt;
            }
        }
        return PresetEntry{newName, target, false};
    }

    std::optional<PresetEntry> saved = save(preset, target, error);
    if (!saved)
        return std::nullopt;
    if (!remove(entry, error)) {
        QFile::remove(target);
        return std::nullopt;
    }
    return saved;
}

std::optional<PresetEntry> PresetStore::duplicate(const PresetEntry &entry, QString *error)
{
    PresetParseResult loaded = load(entry);
    if (!loaded.preset) {
        setError(error, loaded.error);
        return std::nullopt;
    }
    QSet<QString> names;
    for (const QList<PresetEntry> &list : {builtIns(), userPresets()}) {
        for (const PresetEntry &e : list)
            names.insert(e.name.toCaseFolded());
    }
    Preset preset = std::move(*loaded.preset);
    const QString base = preset.name;
    for (int n = 2;; ++n) {
        preset.name = QStringLiteral("%1 (%2)").arg(base).arg(n);
        if (!names.contains(preset.name.toCaseFolded()))
            break;
    }
    return save(preset, QString(), error);
}

std::optional<PresetEntry> PresetStore::importFile(const QString &path, QString *error)
{
    const std::optional<QByteArray> bytes = readBytes(path, error);
    if (!bytes)
        return std::nullopt;
    const PresetParseResult parsed = parsePreset(*bytes);
    if (!parsed.preset) {
        setError(error, parsed.error);
        return std::nullopt;
    }
    if (!QDir().mkpath(m_userDir)) {
        setError(error, QStringLiteral("Cannot make the folder \"%1\".").arg(m_userDir));
        return std::nullopt;
    }
    const QString target = newFilePath(parsed.preset->name);
    if (!writeBytes(target, *bytes, error))
        return std::nullopt;
    return PresetEntry{parsed.preset->name, target, false};
}

bool PresetStore::exportTo(const Preset &preset, const QString &path, QString *error)
{
    return writeBytes(path, serializePreset(preset), error);
}

QString PresetStore::newFilePath(const QString &name, const QString &self) const
{
    const QString base = QDir(m_userDir).filePath(fileBase(name));
    for (int n = 1;; ++n) {
        const QString path =
                n == 1 ? base + kSuffix : QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(kSuffix);
        if (!QFileInfo::exists(path) || (!self.isEmpty() && sameFile(path, self)))
            return path;
    }
}

} // namespace ctrm
