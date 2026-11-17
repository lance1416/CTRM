// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rule.h"

#include <QString>

namespace ctrm {

// F4: replace text (PRD 8.2). Literal, wildcard or regular expression.
// Regex replacement syntax: $0 (full match), $1-$99, ${name}, $$ (a literal '$').
// A reference to a group that does not exist stays as literal text. In literal and
// wildcard mode the replacement is literal text.
class ReplaceRule : public Rule
{
public:
    enum class Mode { Literal, Regex, Wildcard };
    enum class Occurrence { All, First, Last };

    QString find;
    QString replace;
    Mode mode = Mode::Literal;
    bool caseSensitive = true;
    Occurrence occurrence = Occurrence::All;
    bool wholeWord = false; // literal mode only

    QString type() const override;
    std::unique_ptr<Rule> clone() const override;
    std::optional<RuleProblem> problem() const override; // regex mode: bad pattern
    QStringList optionKeys() const override;
    QJsonObject optionsToJson() const override;
    void optionsFromJson(const QJsonObject &o, QStringList *warnings) override;

protected:
    QString transform(const QString &text, const RuleContext &ctx) const override;
};

} // namespace ctrm
