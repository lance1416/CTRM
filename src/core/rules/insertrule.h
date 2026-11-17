// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rule.h"

namespace ctrm {

// F5: inserts text as a prefix, a suffix or at a grapheme position (PRD 8.2).
class InsertRule : public Rule
{
public:
    enum class Position { Prefix, Suffix, Index };

    QString text;
    Position position = Position::Prefix;
    int index = 0;        // grapheme position, used only for Position::Index; clamps to the text
    bool fromEnd = false; // index counts from the end

    QString type() const override;
    std::unique_ptr<Rule> clone() const override;
    QStringList optionKeys() const override; // text, position, index, fromEnd
    QJsonObject optionsToJson() const override;
    void optionsFromJson(const QJsonObject &o, QStringList *warnings) override;

protected:
    QString transform(const QString &text, const RuleContext &ctx) const override;
};

} // namespace ctrm
