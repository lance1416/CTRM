// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rule.h"

#include <QJsonObject>

namespace ctrm {

// A rule of a type that this CTRM version does not know (PRD 9.2). It keeps all keys for save.
// While it is enabled, it blocks the batch. It never changes a name.
class UnsupportedRule : public Rule
{
public:
    explicit UnsupportedRule(QJsonObject original); // keeps all keys

    QString type() const override;                       // the original "type"
    std::optional<RuleProblem> problem() const override; // "Unsupported rule (<type>)"
    bool hasFixedTarget() const override;                // true: "target" stays an original key
    std::unique_ptr<Rule> clone() const override;
    QStringList optionKeys() const override;    // the original keys except type/enabled
    QJsonObject optionsToJson() const override; // the original keys except type/enabled
    void optionsFromJson(const QJsonObject &o, QStringList *warnings) override; // no effect

protected:
    QString transform(const QString &text, const RuleContext &ctx) const override;

private:
    QJsonObject m_original;
};

} // namespace ctrm
