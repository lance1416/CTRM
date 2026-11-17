// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rulechain.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace ctrm {

// A rule chain with its name, in the preset file format (PRD 9).
struct Preset
{
    QString name;
    QString description;
    QString createdWith;
    RuleChain chain;
    QJsonObject unknownKeys; // top-level keys that CTRM does not know, kept for save
};

struct PresetParseResult
{
    std::optional<Preset> preset; // empty when the file cannot be used; see error
    QString error;
    QStringList warnings;
};

PresetParseResult parsePreset(const QByteArray &json);

// The file bytes: PRD key order, 2-space indent, "\n" line ends, final newline, UTF-8 without BOM.
// createdWith is always "CTRM <version>".
QByteArray serializePreset(const Preset &preset);

// The same content as serializePreset, as a JSON object.
QJsonObject presetToJsonObject(const Preset &preset);

} // namespace ctrm
