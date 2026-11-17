// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rule.h"

namespace ctrm {

// F6: removes a grapheme range, characters by class, or text between delimiters (PRD 8.2).
class RemoveRule : public Rule
{
public:
    enum class Mode { Range, Characters, Between };
    enum Class : quint32 {
        Digits = 1,
        Letters = 2,
        Spaces = 4,
        Punctuation = 8,
        Symbols = 16,
        NonAscii = 32,
        Custom = 64
    };
    enum class Occurrence { All, First };

    Mode mode = Mode::Range;
    // Range mode: grapheme positions. Clamp to the text.
    int from = 0;
    int count = 1;
    bool fromEnd = false;
    // Characters mode: a set of Class bits. Custom uses the grapheme clusters in `custom`.
    quint32 classes = 0;
    QString custom;
    // Between mode: no nesting. An open without a close removes nothing.
    QString open = QStringLiteral("(");
    QString close = QStringLiteral(")");
    bool keepDelimiters = false;
    Occurrence occurrence = Occurrence::All;

    QString type() const override;
    std::unique_ptr<Rule> clone() const override;
    // mode, from, count, fromEnd, classes, custom, open, close, keepDelimiters, occurrence
    QStringList optionKeys() const override;
    QJsonObject optionsToJson() const override;
    void optionsFromJson(const QJsonObject &o, QStringList *warnings) override;

protected:
    QString transform(const QString &text, const RuleContext &ctx) const override;
};

} // namespace ctrm
