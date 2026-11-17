// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/status.h"

namespace ctrm {

bool isBlocking(Status s)
{
    switch (s) {
    case Status::Conflict:
    case Status::Invalid:
    case Status::Missing:
    case Status::RuleError:
        return true;
    case Status::Unchanged:
    case Status::Changed:
    case Status::CaseOnly:
        return false;
    }
    return false;
}

bool isWarning(DiagCode c)
{
    switch (c) {
    case DiagCode::BecomesHidden:
    case DiagCode::BecomesVisible:
    case DiagCode::ExtensionChanged:
    case DiagCode::PathLongerThan260:
    case DiagCode::EndsWithSpaceOrDot:
    case DiagCode::LeadingOrTrailingWhitespace:
    case DiagCode::WeakIdentity:
        return true;
    case DiagCode::DuplicateInBatch:
    case DiagCode::ExistsInFolder:
    case DiagCode::ExistsIgnoringCase:
    case DiagCode::ExistsNormalization:
    case DiagCode::EmptyName:
    case DiagCode::DotName:
    case DiagCode::ForbiddenChar:
    case DiagCode::ReservedName:
    case DiagCode::TrailingSpaceOrDot:
    case DiagCode::NameTooLong:
    case DiagCode::PathTooLong:
    case DiagCode::NotOnDisk:
    case DiagCode::IdentityChanged:
    case DiagCode::RuleFailed:
    case DiagCode::UnsupportedRule:
        return false;
    }
    return false;
}

} // namespace ctrm
