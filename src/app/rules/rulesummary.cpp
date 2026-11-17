// SPDX-License-Identifier: GPL-3.0-or-later
#include "rulesummary.h"

#include "rules/changecaserule.h"
#include "rules/cleanuprule.h"
#include "rules/extensionrule.h"
#include "rules/insertrule.h"
#include "rules/numberingrule.h"
#include "rules/removerule.h"
#include "rules/replacerule.h"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>

namespace ctrm::app {

namespace {

// The summary has no item count. "Auto" padding shows at least this number of digits.
constexpr int kAutoPaddingDigits = 3;

QString tr(const char *text)
{
    return QCoreApplication::translate("ctrm::app::RuleSummary", text);
}

QString quoted(const QString &text)
{
    return QStringLiteral("\"%1\"").arg(text);
}

QString replaceSummary(const ReplaceRule &r)
{
    QString summary;
    if (r.mode == ReplaceRule::Mode::Literal) {
        summary = r.replace.isEmpty()
                          ? tr("Replace %1 with nothing").arg(quoted(r.find))
                          : tr("Replace %1 with %2").arg(quoted(r.find), quoted(r.replace));
    } else {
        const QString to = r.replace.isEmpty() ? tr("nothing") : r.replace;
        summary =
                (r.mode == ReplaceRule::Mode::Regex ? tr("Regex %1 → %2") : tr("Wildcard %1 → %2"))
                        .arg(r.find, to);
    }
    if (r.occurrence == ReplaceRule::Occurrence::First)
        summary += tr(", first only");
    else if (r.occurrence == ReplaceRule::Occurrence::Last)
        summary += tr(", last only");
    return summary;
}

QString insertSummary(const InsertRule &r)
{
    switch (r.position) {
    case InsertRule::Position::Prefix:
        return tr("Insert %1 as prefix").arg(quoted(r.text));
    case InsertRule::Position::Suffix:
        return tr("Insert %1 as suffix").arg(quoted(r.text));
    case InsertRule::Position::Index:
        return r.fromEnd ? tr("Insert %1 at %2 from the end")
                                   .arg(quoted(r.text), QString::number(r.index))
                         : tr("Insert %1 at %2").arg(quoted(r.text), QString::number(r.index));
    }
    return {};
}

QString removeSummary(const RemoveRule &r)
{
    switch (r.mode) {
    case RemoveRule::Mode::Range: {
        QString summary = r.count == 1
                                  ? tr("Remove 1 character from %1").arg(r.from)
                                  : tr("Remove %1 characters from %2").arg(r.count).arg(r.from);
        if (r.fromEnd)
            summary += tr(" (from the end)");
        return summary;
    }
    case RemoveRule::Mode::Characters: {
        QStringList parts;
        const std::pair<quint32, QString> names[] = {
                {RemoveRule::Digits, tr("digits")},
                {RemoveRule::Letters, tr("letters")},
                {RemoveRule::Spaces, tr("spaces")},
                {RemoveRule::Punctuation, tr("punctuation")},
                {RemoveRule::Symbols, tr("symbols")},
                {RemoveRule::NonAscii, tr("non-ASCII characters")},
        };
        for (const auto &[flag, name] : names) {
            if (r.classes & flag)
                parts.append(name);
        }
        if ((r.classes & RemoveRule::Custom) && !r.custom.isEmpty())
            parts.append(quoted(r.custom));
        if (parts.isEmpty())
            return tr("Remove no characters");
        return tr("Remove %1").arg(parts.join(QStringLiteral(", ")));
    }
    case RemoveRule::Mode::Between:
        return tr("Remove text between %1 %2").arg(r.open, r.close);
    }
    return {};
}

QString caseSummary(const ChangeCaseRule &r)
{
    switch (r.caseMode) {
    case ChangeCaseRule::Case::Lower:
        return tr("lower case");
    case ChangeCaseRule::Case::Upper:
        return tr("UPPER CASE");
    case ChangeCaseRule::Case::Title:
        return tr("Title Case");
    case ChangeCaseRule::Case::Sentence:
        return tr("Sentence case");
    case ChangeCaseRule::Case::Invert:
        return tr("iNVERT cASE");
    }
    return {};
}

QString numberingSummary(const NumberingRule &r)
{
    const int digits = r.padding >= 0 ? r.padding
                                      : std::max(kAutoPaddingDigits,
                                                 NumberingRule::autoDigits(r.start, r.step, 1));
    const QString number = QStringLiteral("%1").arg(r.start, digits, 10, QLatin1Char('0'));
    QString summary;
    switch (r.position) {
    case NumberingRule::Position::Prefix:
        summary = tr("Prefix %1").arg(number + r.separator);
        break;
    case NumberingRule::Position::Suffix:
        summary = tr("Suffix %1").arg(r.separator + number);
        break;
    case NumberingRule::Position::Index:
        summary = r.fromEnd ? tr("At %1 from the end %2").arg(r.index).arg(number)
                            : tr("At %1 %2").arg(r.index).arg(number);
        break;
    case NumberingRule::Position::Replace:
        summary = tr("Name to %1").arg(number);
        break;
    }
    summary += tr(", from %1").arg(r.start);
    if (r.step != 1)
        summary += tr(", step %1").arg(r.step);
    return summary;
}

QString extensionSummary(const ExtensionRule &r)
{
    QString summary;
    switch (r.action) {
    case ExtensionRule::Action::Set:
        summary = tr("Extension to %1").arg(quoted(r.value));
        break;
    case ExtensionRule::Action::Remove:
        summary = tr("Remove extension");
        break;
    case ExtensionRule::Action::Lower:
        summary = tr("Extension to lower case");
        break;
    case ExtensionRule::Action::Upper:
        summary = tr("Extension to UPPER CASE");
        break;
    case ExtensionRule::Action::Add:
        summary = tr("Add extension %1").arg(quoted(r.value));
        break;
    }
    if (!r.onlyIfMatches.isEmpty())
        summary += tr(", only %1").arg(quoted(r.onlyIfMatches));
    return summary;
}

QString cleanUpSummary(const CleanUpRule &r)
{
    QStringList parts;
    if (r.trim)
        parts.append(tr("trim"));
    if (r.collapseSpaces)
        parts.append(tr("single spaces"));
    switch (r.spacesTo) {
    case CleanUpRule::Spaces::Keep:
        break;
    case CleanUpRule::Spaces::Underscore:
        parts.append(tr("spaces to %1").arg(quoted(QStringLiteral("_"))));
        break;
    case CleanUpRule::Spaces::Dash:
        parts.append(tr("spaces to %1").arg(quoted(QStringLiteral("-"))));
        break;
    case CleanUpRule::Spaces::Dot:
        parts.append(tr("spaces to %1").arg(quoted(QStringLiteral("."))));
        break;
    case CleanUpRule::Spaces::Remove:
        parts.append(tr("remove spaces"));
        break;
    }
    if (r.replaceInvalid)
        parts.append(tr("replace bad characters with %1").arg(quoted(r.replacement)));
    if (parts.isEmpty())
        return tr("No change");
    QString summary = parts.join(QStringLiteral(", "));
    summary[0] = summary.at(0).toUpper();
    return summary;
}

QString optionSummary(const Rule &rule)
{
    if (const auto *r = dynamic_cast<const ReplaceRule *>(&rule))
        return replaceSummary(*r);
    if (const auto *r = dynamic_cast<const InsertRule *>(&rule))
        return insertSummary(*r);
    if (const auto *r = dynamic_cast<const RemoveRule *>(&rule))
        return removeSummary(*r);
    if (const auto *r = dynamic_cast<const ChangeCaseRule *>(&rule))
        return caseSummary(*r);
    if (const auto *r = dynamic_cast<const NumberingRule *>(&rule))
        return numberingSummary(*r);
    if (const auto *r = dynamic_cast<const ExtensionRule *>(&rule))
        return extensionSummary(*r);
    if (const auto *r = dynamic_cast<const CleanUpRule *>(&rule))
        return cleanUpSummary(*r);
    return tr("Unsupported rule (%1)").arg(rule.type());
}

} // namespace

QString ruleDisplayName(const QString &type)
{
    if (type == u"replace")
        return tr("Replace");
    if (type == u"insert")
        return tr("Insert");
    if (type == u"remove")
        return tr("Remove");
    if (type == u"changeCase")
        return tr("Change case");
    if (type == u"numbering")
        return tr("Numbering");
    if (type == u"extension")
        return tr("Extension");
    if (type == u"cleanup")
        return tr("Clean up");
    return tr("Unsupported rule");
}

QString ruleSummary(const Rule &rule)
{
    QString summary;
    if (const auto problem = rule.problem()) {
        summary = problem->unsupported ? tr("Unsupported rule (%1)").arg(rule.type()) : tr("Error");
    } else {
        summary = optionSummary(rule);
        if (!rule.hasFixedTarget()) {
            if (rule.target == Target::Extension)
                summary += tr(" (extension only)");
            else if (rule.target == Target::Name)
                summary += tr(" (name and extension)");
        }
    }
    if (!rule.enabled)
        summary += tr(" (off)");
    return summary;
}

} // namespace ctrm::app
