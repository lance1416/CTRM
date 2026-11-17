// SPDX-License-Identifier: GPL-3.0-or-later
#include "presets/preset.h"

#include "rules/ruleregistry.h"
#include "version.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>

namespace ctrm {

namespace {

const QString kSchemaKey = QStringLiteral("$schema");
const QString kFormatKey = QStringLiteral("format");
const QString kVersionKey = QStringLiteral("formatVersion");
const QString kNameKey = QStringLiteral("name");
const QString kDescriptionKey = QStringLiteral("description");
const QString kCreatedWithKey = QStringLiteral("createdWith");
const QString kOptionsKey = QStringLiteral("options");
const QString kRulesKey = QStringLiteral("rules");

const QString kSchemaUrl = QStringLiteral("https://ctrm.example/schema/preset-v1.json");
const QString kFormat = QStringLiteral("ctrm-preset");
constexpr int kFormatVersion = 1;

const QStringList kTopOrder{kSchemaKey,      kFormatKey,      kVersionKey, kNameKey,
                            kDescriptionKey, kCreatedWithKey, kOptionsKey, kRulesKey};
const QStringList kOptionsOrder{QStringLiteral("portableNames"), QStringLiteral("folderExtensions"),
                                QStringLiteral("autoResolveDuplicates")};

void warn(QStringList *warnings, const QString &text)
{
    warnings->append(text);
}

void warnBadValue(QStringList *warnings, const QString &key)
{
    warn(warnings, QStringLiteral("Bad value for '%1' in preset").arg(key));
}

// Reads an optional string. A value of an other type gives a warning and an empty string.
QString readText(const QJsonObject &o, const QString &key, bool required, QStringList *warnings)
{
    const QJsonValue v = o.value(key);
    if (v.isString())
        return v.toString();
    if (required || !v.isUndefined())
        warnBadValue(warnings, key);
    return QString();
}

// --- Ordered writer. QJsonDocument sorts the keys, so it cannot write the PRD order. ---

void writeIndent(QByteArray &out, int depth)
{
    out.append(QByteArray(qsizetype(depth) * 2, ' '));
}

// A scalar as JSON text. QJsonDocument writes the string escapes and the number format.
QByteArray scalarText(const QJsonValue &v)
{
    const QByteArray array = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return array.mid(1, array.size() - 2);
}

void writeValue(QByteArray &out, const QJsonValue &v, int depth);

// The keys of `o`: the keys of `first` that are in `o`, in that order, then the other keys sorted.
QStringList keyOrder(const QJsonObject &o, const QStringList &first)
{
    QStringList keys;
    for (const QString &k : first) {
        if (o.contains(k) && !keys.contains(k))
            keys.append(k);
    }
    for (const QString &k : o.keys()) { // QJsonObject::keys() is sorted
        if (!keys.contains(k))
            keys.append(k);
    }
    return keys;
}

void writeObject(QByteArray &out, const QJsonObject &o, const QStringList &first, int depth)
{
    if (o.isEmpty()) {
        out.append("{}");
        return;
    }
    out.append("{\n");
    const QStringList keys = keyOrder(o, first);
    for (qsizetype i = 0; i < keys.size(); ++i) {
        writeIndent(out, depth + 1);
        out.append(scalarText(keys.at(i)));
        out.append(": ");
        writeValue(out, o.value(keys.at(i)), depth + 1);
        out.append(i + 1 < keys.size() ? ",\n" : "\n");
    }
    writeIndent(out, depth);
    out.append('}');
}

// Writes the array. `firstKeys(i)` gives the key order of element i when it is an object.
template <typename FirstKeys>
void writeArray(QByteArray &out, const QJsonArray &a, int depth, FirstKeys firstKeys)
{
    if (a.isEmpty()) {
        out.append("[]");
        return;
    }
    out.append("[\n");
    for (qsizetype i = 0; i < a.size(); ++i) {
        writeIndent(out, depth + 1);
        if (a.at(i).isObject())
            writeObject(out, a.at(i).toObject(), firstKeys(i), depth + 1);
        else
            writeValue(out, a.at(i), depth + 1);
        out.append(i + 1 < a.size() ? ",\n" : "\n");
    }
    writeIndent(out, depth);
    out.append(']');
}

void writeValue(QByteArray &out, const QJsonValue &v, int depth)
{
    if (v.isObject())
        writeObject(out, v.toObject(), {}, depth);
    else if (v.isArray())
        writeArray(out, v.toArray(), depth, [](qsizetype) { return QStringList(); });
    else
        out.append(scalarText(v));
}

} // namespace

PresetParseResult parsePreset(const QByteArray &json)
{
    PresetParseResult result;
    QJsonParseError parseError;
    // Some editors write a UTF-8 BOM. JSON (RFC 8259) lets a parser ignore it.
    const QByteArray bom("\xEF\xBB\xBF");
    const QJsonDocument doc = QJsonDocument::fromJson(
            json.startsWith(bom) ? json.sliced(bom.size()) : json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.error = QStringLiteral("This file is not valid JSON (%1 at offset %2).")
                               .arg(parseError.errorString())
                               .arg(parseError.offset);
        return result;
    }
    if (!doc.isObject()) {
        result.error = QStringLiteral("This file is not a CTRM preset.");
        return result;
    }
    const QJsonObject o = doc.object();
    if (o.value(kFormatKey) != QJsonValue(kFormat)) {
        result.error = QStringLiteral("This file is not a CTRM preset.");
        return result;
    }

    const QJsonValue version = o.value(kVersionKey);
    const double v = version.toDouble();
    if (!version.isDouble() || !std::isfinite(v) || std::floor(v) != v || v < 1) {
        result.error = QStringLiteral("This preset has a bad format version.");
        return result;
    }
    if (v > kFormatVersion) {
        result.error = QStringLiteral("This preset needs a newer CTRM (format version %1).")
                               .arg(QString::number(v, 'f', 0));
        return result;
    }

    QStringList *warnings = &result.warnings;
    Preset preset;
    preset.name = readText(o, kNameKey, true, warnings);
    preset.description = readText(o, kDescriptionKey, false, warnings);
    preset.createdWith = readText(o, kCreatedWithKey, false, warnings);

    if (const QJsonValue options = o.value(kOptionsKey); options.isObject())
        preset.chain.options = chainOptionsFromJson(options.toObject(), warnings);
    else if (!options.isUndefined())
        warnBadValue(warnings, kOptionsKey);

    const QJsonValue rules = o.value(kRulesKey);
    if (!rules.isArray() && !rules.isUndefined()) {
        result.error = QStringLiteral("This preset has a bad rule list.");
        result.warnings.clear();
        return result;
    }
    for (const QJsonValue rule : rules.toArray()) {
        // A rule that is not an object cannot be kept on save, so the preset is not used.
        if (!rule.isObject()) {
            result.error = QStringLiteral("This preset has a bad rule list.");
            result.warnings.clear();
            return result;
        }
        preset.chain.insert(preset.chain.size(), ruleFromJson(rule.toObject(), warnings));
    }

    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (kTopOrder.contains(it.key()))
            continue;
        preset.unknownKeys.insert(it.key(), it.value());
        warn(warnings, QStringLiteral("Unknown key '%1' in preset").arg(it.key()));
    }

    result.preset = std::move(preset);
    return result;
}

QJsonObject presetToJsonObject(const Preset &preset)
{
    QJsonObject o{
            {kSchemaKey, kSchemaUrl},
            {kFormatKey, kFormat},
            {kVersionKey, kFormatVersion},
            {kNameKey, preset.name},
            {kDescriptionKey, preset.description},
            {kCreatedWithKey, QStringLiteral("CTRM " CTRM_VERSION)},
            {kOptionsKey, chainOptionsToJson(preset.chain.options)},
            {kRulesKey, chainRulesToJson(preset.chain)},
    };
    for (auto it = preset.unknownKeys.constBegin(); it != preset.unknownKeys.constEnd(); ++it) {
        if (!o.contains(it.key()))
            o.insert(it.key(), it.value());
    }
    return o;
}

QByteArray serializePreset(const Preset &preset)
{
    const QJsonObject o = presetToJsonObject(preset);
    QByteArray out("{\n");
    const QStringList keys = keyOrder(o, kTopOrder);
    for (qsizetype i = 0; i < keys.size(); ++i) {
        const QString &key = keys.at(i);
        writeIndent(out, 1);
        out.append(scalarText(key));
        out.append(": ");
        if (key == kOptionsKey) {
            writeObject(out, o.value(key).toObject(), kOptionsOrder, 1);
        } else if (key == kRulesKey) {
            writeArray(out, o.value(key).toArray(), 1, [&](qsizetype r) {
                const Rule *rule = preset.chain.at(int(r));
                return QStringList{QStringLiteral("type"), QStringLiteral("enabled"),
                                   QStringLiteral("target")} +
                       rule->optionKeys();
            });
        } else {
            writeValue(out, o.value(key), 1);
        }
        out.append(i + 1 < keys.size() ? ",\n" : "\n");
    }
    out.append("}\n");
    return out;
}

} // namespace ctrm
