// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace ctrm {
class Rule;
}

namespace ctrm::app {

// "Replace", "Insert", "Remove", "Change case", "Numbering", "Extension", "Clean up";
// "Unsupported rule" for an unknown type.
QString ruleDisplayName(const QString &type);

// The one-line summary of ui-design 8.9. A target that is not the default adds
// " (extension only)" or " (name and extension)". A rule that is off adds " (off)". A rule with a
// problem gives "Error", an unknown rule "Unsupported rule (<type>)".
QString ruleSummary(const ctrm::Rule &rule);

} // namespace ctrm::app
