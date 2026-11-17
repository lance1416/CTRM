// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "presets/preset.h"

#include <QList>
#include <QString>

#include <optional>

namespace ctrm {

struct PresetEntry
{
    QString name;
    QString filePath;
    bool builtIn = false;
};

// The built-in presets (read-only, in the resources) and the user presets (*.ctrm.json files in
// one folder). Each function that can fail sets *error to a message for the user.
class PresetStore
{
public:
    explicit PresetStore(QString userDir);

    QList<PresetEntry> builtIns() const;
    QList<PresetEntry> userPresets() const; // natural order of the names

    PresetParseResult load(const PresetEntry &entry) const;

    // Writes the preset. An empty filePath makes a new file in the user folder.
    std::optional<PresetEntry> save(const Preset &preset, const QString &filePath, QString *error);
    bool remove(const PresetEntry &entry, QString *error);
    std::optional<PresetEntry> rename(const PresetEntry &entry, const QString &newName,
                                      QString *error);
    // A copy in the user folder, with a name that no other preset has: "Name (2)".
    std::optional<PresetEntry> duplicate(const PresetEntry &entry, QString *error);
    // Copies the file as it is into the user folder, only when it parses.
    std::optional<PresetEntry> importFile(const QString &path, QString *error);
    bool exportTo(const Preset &preset, const QString &path, QString *error);

private:
    QString newFilePath(const QString &name, const QString &self = QString()) const;

    QString m_userDir;
};

} // namespace ctrm
