// SPDX-License-Identifier: GPL-3.0-or-later
#include "actions.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QKeySequence>
#include <QMenu>

namespace ctrm::app {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ctrm::app::Actions", text);
}

// The tooltip is the text without "..." and with the first shortcut in brackets.
void setToolTip(QAction *action)
{
    QString tip = action->text();
    tip.remove(QLatin1Char('&'));
    if (tip.endsWith(QLatin1StringView("...")))
        tip.chop(3);
    const QKeySequence key = action->shortcut();
    if (!key.isEmpty())
        tip += QStringLiteral(" (%1)").arg(key.toString(QKeySequence::NativeText));
    action->setToolTip(tip);
}

QAction *make(QObject *parent, const char *objectName, const QString &text,
              const QList<QKeySequence> &shortcuts = {})
{
    auto *action = new QAction(text, parent);
    action->setObjectName(QLatin1StringView(objectName));
    action->setShortcuts(shortcuts);
    action->setMenuRole(QAction::NoRole);
    setToolTip(action);
    return action;
}

QAction *makeCheckable(QObject *parent, const char *objectName, const QString &text, bool checked)
{
    QAction *action = make(parent, objectName, text);
    action->setCheckable(true);
    action->setChecked(checked);
    return action;
}

QList<QKeySequence> keys(std::initializer_list<const char *> portable)
{
    QList<QKeySequence> result;
    for (const char *k : portable)
        result.append(QKeySequence(QLatin1StringView(k)));
    return result;
}

} // namespace

Actions createActions(QObject *parent)
{
    Actions a{};
#ifdef Q_OS_MACOS
    constexpr bool mac = true;
#else
    constexpr bool mac = false;
#endif

    // File
    a.addFiles = make(parent, "addFiles", tr("Add Files..."), keys({"Ctrl+O"}));
    a.addFolder = make(parent, "addFolder", tr("Add Folder..."), keys({"Ctrl+Shift+O"}));
    a.removeFromList = make(parent, "removeFromList", tr("Remove from List"),
                            mac ? keys({"Del", "Ctrl+Backspace"}) : keys({"Del"}));
    a.clearList = make(parent, "clearList", tr("Clear List"),
                       keys({mac ? "Ctrl+Shift+Backspace" : "Ctrl+Shift+Del"}));
    a.refresh = make(parent, "refresh", tr("Refresh"), keys({mac ? "Ctrl+R" : "F5"}));
    a.closeWindow = make(parent, "closeWindow", tr("Close Window"),
                         QKeySequence::keyBindings(QKeySequence::Close));
    a.quit = make(parent, "quit", tr("Quit"), QKeySequence::keyBindings(QKeySequence::Quit));
    a.quit->setMenuRole(QAction::QuitRole);

    // Edit
    a.undoRename = make(parent, "undoRename", tr("Undo Rename"), keys({"Ctrl+Z"}));
    a.undoRename->setShortcutContext(Qt::WindowShortcut);
    // Include and Exclude have no shortcut. Space toggles the state in the table (ui-design 12,
    // R2-03).
    a.include = make(parent, "include", tr("Include"));
    a.exclude = make(parent, "exclude", tr("Exclude"));
    a.selectAll = make(parent, "selectAll", tr("Select All"),
                       QKeySequence::keyBindings(QKeySequence::SelectAll));
    a.find = make(parent, "find", tr("Find"), keys({"Ctrl+F"}));

    // Rules
    auto *parentWidget = qobject_cast<QWidget *>(parent);
    a.addRuleMenu = new QMenu(tr("Add Rule"), parentWidget);
    a.addRuleMenu->setObjectName(QStringLiteral("addRuleMenu"));
    if (!parentWidget) {
        QObject::connect(parent, &QObject::destroyed, a.addRuleMenu,
                         [menu = a.addRuleMenu] { delete menu; });
    }
    QAction *addRule = a.addRuleMenu->menuAction();
    addRule->setObjectName(QStringLiteral("addRule"));
    addRule->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    setToolTip(addRule);
    a.duplicateRule = make(parent, "duplicateRule", tr("Duplicate Rule"), keys({"Ctrl+D"}));
    // Delete and Space work on a rule only when the focus is in the rule list (ui-design 12).
    a.deleteRule = make(parent, "deleteRule", tr("Delete Rule"));
    a.moveRuleUp = make(parent, "moveRuleUp", tr("Move Up"), keys({"Ctrl+Alt+Up"}));
    a.moveRuleDown = make(parent, "moveRuleDown", tr("Move Down"), keys({"Ctrl+Alt+Down"}));
    a.toggleRule = make(parent, "toggleRule", tr("Turn On or Off"));
    a.clearRules = make(parent, "clearRules", tr("Clear All Rules"));

    // Presets
    a.saveAsPreset =
            make(parent, "saveAsPreset", tr("Save Current Rules as Preset..."), keys({"Ctrl+S"}));
    a.updatePreset = make(parent, "updatePreset", tr("Update Preset"));
    a.managePresets = make(parent, "managePresets", tr("Manage Presets..."));
    a.importPreset = make(parent, "importPreset", tr("Import Preset..."));
    a.exportRules = make(parent, "exportRules", tr("Export Current Rules..."));

    // View
    a.showSteps = makeCheckable(parent, "showSteps", tr("Show Steps"), true);
    a.highlightChanged =
            makeCheckable(parent, "highlightChanged", tr("Highlight Changed Characters"), false);
    a.showAllItems = makeCheckable(parent, "showAllItems", tr("Show All Items"), true);
    a.showChangedOnly = makeCheckable(parent, "showChangedOnly", tr("Show Changed Only"), false);
    a.showProblemsOnly = makeCheckable(parent, "showProblemsOnly", tr("Show Problems Only"), false);
    auto *showGroup = new QActionGroup(parent);
    showGroup->setExclusive(true);
    for (QAction *action : {a.showAllItems, a.showChangedOnly, a.showProblemsOnly})
        showGroup->addAction(action);
    a.showToolbar = makeCheckable(parent, "showToolbar", tr("Show Toolbar"), true);

    // Rename
    a.rename = make(parent, "rename", tr("Rename Items"), keys({"Ctrl+Return", "Ctrl+Enter"}));
    a.skipAndRename = make(parent, "skipAndRename", tr("Skip Blocked Items and Rename..."));
    a.nextProblem = make(parent, "nextProblem", tr("Next Problem"), keys({"F8"}));
    a.previousProblem = make(parent, "previousProblem", tr("Previous Problem"), keys({"Shift+F8"}));
    a.history = make(parent, "history", tr("History..."), keys({mac ? "Ctrl+Y" : "Ctrl+H"}));
    a.cancelBatch = make(parent, "cancelBatch", tr("Cancel"),
                         mac ? keys({"Escape", "Ctrl+."}) : keys({"Escape"}));
    a.cancelBatch->setEnabled(false);

    // Focus
    a.focusRules = make(parent, "focusRules", tr("Go to Rules"), keys({"Ctrl+1"}));
    a.focusTable = make(parent, "focusTable", tr("Go to Items"), keys({"Ctrl+2"}));
    a.focusEditor = make(parent, "focusEditor", tr("Go to Rule Editor"), keys({"Ctrl+3"}));

#ifdef Q_OS_WIN
    a.preferences = make(parent, "preferences", tr("Options..."));
#else
    a.preferences = make(parent, "preferences", tr("Preferences..."),
                         QKeySequence::keyBindings(QKeySequence::Preferences));
#endif
    a.preferences->setMenuRole(QAction::PreferencesRole);

    // Help
    a.help = make(parent, "help", tr("CTRM Help"),
                  QKeySequence::keyBindings(QKeySequence::HelpContents));
    a.regexHelp = make(parent, "regexHelp", tr("Regular Expression Help"));
    a.reportProblem = make(parent, "reportProblem", tr("Report a Problem..."));
    a.about = make(parent, "about", tr("About CTRM"));
    a.about->setMenuRole(QAction::AboutRole);

    return a;
}

} // namespace ctrm::app
