// SPDX-License-Identifier: GPL-3.0-or-later
#include "ruleeditorfactory.h"

#include "changecaseeditor.h"
#include "cleanupeditor.h"
#include "extensioneditor.h"
#include "inserteditor.h"
#include "numberingeditor.h"
#include "removeeditor.h"
#include "replaceeditor.h"

namespace ctrm::app {

RuleEditor *createRuleEditor(const QString &type, QWidget *parent)
{
    if (type == QLatin1StringView("replace"))
        return new ReplaceEditor(parent);
    if (type == QLatin1StringView("insert"))
        return new InsertEditor(parent);
    if (type == QLatin1StringView("remove"))
        return new RemoveEditor(parent);
    if (type == QLatin1StringView("cleanup"))
        return new CleanUpEditor(parent);
    if (type == QLatin1StringView("changeCase"))
        return new ChangeCaseEditor(parent);
    if (type == QLatin1StringView("extension"))
        return new ExtensionEditor(parent);
    if (type == QLatin1StringView("numbering"))
        return new NumberingEditor(parent);
    return nullptr;
}

} // namespace ctrm::app
