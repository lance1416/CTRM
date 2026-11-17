// SPDX-License-Identifier: GPL-3.0-or-later
#include "text/messages.h"

#include <QCoreApplication>

namespace ctrm::app {

namespace {

// Translation context for the texts of this file.
class Messages
{
    Q_DECLARE_TR_FUNCTIONS(Messages)
};

QString warningPhrase(DiagCode c)
{
    switch (c) {
    case DiagCode::BecomesHidden:
        return Messages::tr("the item will become hidden");
    case DiagCode::BecomesVisible:
        return Messages::tr("the item will become visible");
    case DiagCode::ExtensionChanged:
        return Messages::tr("the extension changes");
    case DiagCode::PathLongerThan260:
        return Messages::tr("the path is longer than 260 characters");
    case DiagCode::EndsWithSpaceOrDot:
        return Messages::tr("the name ends with a space or a dot");
    case DiagCode::LeadingOrTrailingWhitespace:
        return Messages::tr("the name starts or ends with a space");
    case DiagCode::WeakIdentity:
        return Messages::tr("weak identity check");
    default:
        return {};
    }
}

} // namespace

QString diagnosticText(const Diagnostic &d, bool portableNames, const QStringList &ruleNames)
{
    if (isWarning(d.code))
        return Messages::tr("Will rename. Warning: ") + warningPhrase(d.code);

    switch (d.code) {
    case DiagCode::DuplicateInBatch:
        return Messages::tr("Same new name as %n other item(s)", nullptr, d.count);
    case DiagCode::ExistsInFolder:
        return Messages::tr("An item named '%1' is already in this folder").arg(d.text);
    case DiagCode::ExistsIgnoringCase:
        return Messages::tr("Same name as '%1' when letter case is ignored. This folder ignores "
                            "letter case.")
                .arg(d.text);
    case DiagCode::ExistsNormalization:
        return Messages::tr(
                       "Same name as '%1' in this folder (the folder treats these characters as "
                       "equal).")
                .arg(d.text);
    case DiagCode::EmptyName:
        return Messages::tr("Invalid: the name is empty");
    case DiagCode::DotName:
        return Messages::tr("Invalid: '.' and '..' are not permitted");
    case DiagCode::ForbiddenChar:
        return portableNames ? Messages::tr("Invalid: '%1' is not permitted in portable names")
                                       .arg(d.text)
                             : Messages::tr("Invalid: '%1' is not permitted in names").arg(d.text);
    case DiagCode::ReservedName:
        return Messages::tr("Invalid: '%1' is a reserved name on Windows").arg(d.text);
    case DiagCode::TrailingSpaceOrDot:
        return Messages::tr("Invalid: the name ends with a space or a dot");
    case DiagCode::NameTooLong:
        return Messages::tr("Invalid: the name is too long (%1 of %2)").arg(d.count).arg(d.limit);
    case DiagCode::PathTooLong:
        return Messages::tr("Invalid: the path is too long (%1 of %2)").arg(d.count).arg(d.limit);
    case DiagCode::NotOnDisk:
        return Messages::tr("Missing: the item is not on disk. Refresh or remove it.");
    case DiagCode::IdentityChanged:
        return Messages::tr("Missing: the item changed on disk. Refresh or remove it.");
    case DiagCode::RuleFailed: {
        const QString name = ruleNames.value(d.ruleIndex);
        QString text =
                name.isEmpty()
                        ? Messages::tr("Rule error in rule %1: %2").arg(d.ruleIndex + 1).arg(d.text)
                        : Messages::tr("Rule error in rule %1 (%2): %3")
                                  .arg(d.ruleIndex + 1)
                                  .arg(name, d.text);
        if (d.count >= 0) // count is a 0-based grapheme position; users count from 1
            text += Messages::tr(" at position %1").arg(d.count + 1);
        return text;
    }
    case DiagCode::UnsupportedRule:
        return Messages::tr("Rule error in rule %1: Unsupported rule (%2)")
                .arg(d.ruleIndex + 1)
                .arg(d.text);
    default:
        return {};
    }
}

QString statusText(const ItemPreview &p, bool portableNames)
{
    if (!p.included)
        return Messages::tr("Excluded");
    if (isBlocking(p.status) && !p.problems.isEmpty())
        return diagnosticText(p.problems.first(), portableNames);
    switch (p.status) {
    case Status::Changed:
    case Status::CaseOnly:
        if (!p.warnings.isEmpty())
            return diagnosticText(p.warnings.first(), portableNames);
        return p.status == Status::Changed ? Messages::tr("Will rename")
                                           : Messages::tr("Will rename (letter case only)");
    default:
        return Messages::tr("No change");
    }
}

QString postRunText(const PostRunState &s)
{
    switch (s.state) {
    case PostRun::Renamed:
        return Messages::tr("Renamed");
    case PostRun::Failed:
        return Messages::tr("Failed: %1").arg(s.text);
    case PostRun::Skipped:
        return Messages::tr("Skipped: %1").arg(s.text);
    case PostRun::RolledBack:
        return Messages::tr("Rolled back to the old name");
    case PostRun::None:
        break;
    }
    return {};
}

QString blockedOldNameText(const QString &oldName)
{
    return Messages::tr("Conflicts with the old name of '%1', which is blocked.").arg(oldName);
}

} // namespace ctrm::app
