// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rule.h"

namespace ctrm {

// F9: changes the extension of a file. The target is always Extension (PRD 8.1).
class ExtensionRule : public Rule
{
public:
    enum class Action { Set, Remove, Lower, Upper, Add };

    ExtensionRule();

    QString type() const override;
    std::unique_ptr<Rule> clone() const override;
    bool hasFixedTarget() const override;
    void apply(NameParts &parts, const RuleContext &ctx) const override;
    QStringList optionKeys() const override; // {"action","value","onlyIfMatches"}
    QJsonObject optionsToJson() const override;
    void optionsFromJson(const QJsonObject &o, QStringList *warnings) override;

    Action action = Action::Lower;
    QString value;         // for Set and Add; one leading '.' is ignored
    QString onlyIfMatches; // "jpeg;jpe", case-insensitive; empty = all items

protected:
    QString transform(const QString &text, const RuleContext &ctx) const override;

private:
    bool matches(const QString &extension) const;
};

} // namespace ctrm
