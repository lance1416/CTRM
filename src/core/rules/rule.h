// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/nameparts.h"
#include "validate/osrules.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace ctrm {

// The part of a name that a rule changes (PRD 6.1).
enum class Target { Stem, Extension, Name };

QString targetToString(Target t); // "stem" | "extension" | "name"
std::optional<Target> targetFromString(const QString &s);

// Data about the item that a rule changes.
struct RuleContext
{
    int index = 0;          // position in the table (all items)
    int includedIndex = -1; // position among included items, -1 when excluded
    int count = 0;          // all items
    int includedCount = 0;
    bool isFolder = false;
    bool folderExtensions = false;
    bool portableNames = true;
    OsRules os = OsRules::Linux;
};

// A rule option that is not correct. The rule cannot run.
struct RuleProblem
{
    QString message;
    int position = -1;        // grapheme position in the option text, -1 when none
    bool unsupported = false; // the rule type is not known
};

// One rename rule (PRD 8). Subclasses hold their options as public fields.
class Rule
{
public:
    virtual ~Rule() = default;
    virtual QString type() const = 0;
    virtual std::unique_ptr<Rule> clone() const = 0;
    virtual std::optional<RuleProblem> problem() const; // default: none
    virtual bool hasFixedTarget() const;                // default: false
    // Default: transform() on the target part. A new non-empty extension adds the dot.
    virtual void apply(NameParts &parts, const RuleContext &ctx) const;
    virtual QStringList optionKeys() const = 0; // PRD 8.2 order, without type/enabled/target
    virtual QJsonObject optionsToJson() const = 0;
    // A missing key gives the default value.
    virtual void optionsFromJson(const QJsonObject &o, QStringList *warnings) = 0;

    bool enabled = true;
    Target target = Target::Stem;
    QJsonObject unknownKeys; // kept for save (PRD 9.2)

protected:
    Rule() = default;
    Rule(const Rule &) = default;
    Rule &operator=(const Rule &) = default;

    virtual QString transform(const QString &text, const RuleContext &ctx) const = 0;
};

} // namespace ctrm
