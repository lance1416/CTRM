// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rule.h"
#include "rules/rulechain.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace ctrm {

// "replace", "insert", "remove", "cleanup", "changeCase", "extension", "numbering"
QStringList ruleTypes();

// A rule with default options. nullptr when the type is unknown.
std::unique_ptr<Rule> createRule(const QString &type);

// Reads type, enabled (default true), target (default per rule; ignored with a warning for a
// rule with a fixed target) and the options. Other keys go to unknownKeys with the warning
// "Unknown key '<k>' in rule <type>". An unknown or missing type gives an UnsupportedRule.
// Never returns nullptr.
std::unique_ptr<Rule> ruleFromJson(const QJsonObject &o, QStringList *warnings);

// type, enabled, target (not for a rule with a fixed target), options, then unknownKeys that
// do not have the name of a known key.
QJsonObject ruleToJson(const Rule &r);

QJsonArray chainRulesToJson(const RuleChain &chain);

// portableNames, autoResolveDuplicates, folderExtensions.
QJsonObject chainOptionsToJson(const ChainOptions &o);
// A missing key gives the default. A bad value gives the default and the warning
// "Bad value for '<k>' in options". An unknown key gives "Unknown key '<k>' in options".
ChainOptions chainOptionsFromJson(const QJsonObject &o, QStringList *warnings);

} // namespace ctrm
