// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rule.h"

namespace ctrm {

// F10: trims and collapses spaces, replaces spaces and forbidden characters.
// A space is a grapheme cluster whose first code point is Unicode White_Space.
class CleanUpRule : public Rule
{
public:
    enum class Spaces { Keep, Underscore, Dash, Dot, Remove };

    QString type() const override;
    std::unique_ptr<Rule> clone() const override;
    // {"trim","collapseSpaces","spacesTo","replaceInvalid","replacement"}
    QStringList optionKeys() const override;
    QJsonObject optionsToJson() const override; // spacesTo: "keep" | "_" | "-" | "." | ""
    void optionsFromJson(const QJsonObject &o, QStringList *warnings) override;

    bool trim = true;
    bool collapseSpaces = true;
    Spaces spacesTo = Spaces::Keep;
    bool replaceInvalid = false;
    QString replacement = QStringLiteral("_");

protected:
    // Order: trim, collapse, spacesTo, replaceInvalid.
    QString transform(const QString &text, const RuleContext &ctx) const override;
};

} // namespace ctrm
