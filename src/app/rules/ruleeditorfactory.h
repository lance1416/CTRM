// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

class QWidget;

namespace ctrm::app {

class RuleEditor;

// The editor page for a rule type of ctrm::ruleTypes(). nullptr for an unsupported type.
RuleEditor *createRuleEditor(const QString &type, QWidget *parent);

} // namespace ctrm::app
