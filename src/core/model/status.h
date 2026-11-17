// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace ctrm {

// Preview status of one item (PRD 7.1).
enum class Status { Unchanged, Changed, CaseOnly, Conflict, Invalid, Missing, RuleError };

// True for Conflict, Invalid, Missing and RuleError.
bool isBlocking(Status s);

// Diagnostic codes. Core makes no UI text: the app formats each code with its arguments.
enum class DiagCode {
    // Conflict
    DuplicateInBatch,    // count = number of other items with the same name
    ExistsInFolder,      // text = existing name (exact match)
    ExistsIgnoringCase,  // text = existing name (match after case folding)
    ExistsNormalization, // text = existing name (match after NFD, macOS)
    // Invalid
    EmptyName,
    DotName,
    ForbiddenChar, // text = the character
    ReservedName,  // text = reserved word
    TrailingSpaceOrDot,
    NameTooLong, // count = length, limit = 255, text = "utf16"|"utf8"
    PathTooLong, // count, limit
    // Missing
    NotOnDisk,
    IdentityChanged,
    // RuleError
    RuleFailed,      // ruleIndex, text = message, count = position or -1
    UnsupportedRule, // ruleIndex, text = type
    // Warnings (never blocking)
    BecomesHidden,
    BecomesVisible,
    ExtensionChanged,
    PathLongerThan260,
    EndsWithSpaceOrDot,
    LeadingOrTrailingWhitespace,
    WeakIdentity, // text = fsName
};

// True for the warning codes (BecomesHidden ... WeakIdentity).
bool isWarning(DiagCode c);

struct Diagnostic
{
    DiagCode code;
    QString text;
    int count = 0;
    int limit = 0;
    int ruleIndex = -1;
    bool operator==(const Diagnostic &) const = default;
};

} // namespace ctrm
