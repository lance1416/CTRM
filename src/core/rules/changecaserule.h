// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rule.h"

#include <QString>
#include <QStringList>

namespace ctrm {

// F7: change case (PRD 8.2). Unicode default case rules, never the system locale.
class ChangeCaseRule : public Rule
{
public:
    enum class Case { Lower, Upper, Title, Sentence, Invert };

    Case caseMode = Case::Lower;
    QString separators = QStringLiteral(" _-.()[]{}"); // word separators for Title
    QStringList smallWords; // stay lower case in Title, except the first word; ignores case
    bool builtInSmallWords = false;

    // a, an, and, as, at, but, by, for, in, nor, of, on, or, the, to, up, via
    static QStringList englishSmallWords();

    QString type() const override;
    std::unique_ptr<Rule> clone() const override;
    QStringList optionKeys() const override;
    QJsonObject optionsToJson() const override;
    void optionsFromJson(const QJsonObject &o, QStringList *warnings) override;

protected:
    QString transform(const QString &text, const RuleContext &ctx) const override;
};

} // namespace ctrm
