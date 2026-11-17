// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rule.h"

#include <QString>

namespace ctrm {

// F8: numbering (PRD 8.2). The number of an item is start + step * n, where n is its
// position among the numbered items (scope Checked: included items, scope All: all items).
// ASCII digits only. Padding never truncates.
class NumberingRule : public Rule
{
public:
    enum class Position { Prefix, Suffix, Index, Replace };
    enum class Scope { Checked, All };

    int start = 1;    // >= 0
    int step = 1;     // >= 1
    int padding = -1; // -1 = "auto"; else 0..10
    Position position = Position::Suffix;
    QString separator = QStringLiteral("_"); // between number and text; none when text is empty
    int index = 0;                           // grapheme index for Position::Index, >= 0
    bool fromEnd = false;
    Scope scope = Scope::Checked;

    // Digits of the largest number: start + step * (numberedCount - 1). At least 1.
    static int autoDigits(int start, int step, int numberedCount);

    QString type() const override;
    std::unique_ptr<Rule> clone() const override;
    QStringList optionKeys() const override;
    QJsonObject optionsToJson() const override;
    void optionsFromJson(const QJsonObject &o, QStringList *warnings) override;

protected:
    QString transform(const QString &text, const RuleContext &ctx) const override;
};

} // namespace ctrm
