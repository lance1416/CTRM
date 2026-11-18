// SPDX-License-Identifier: GPL-3.0-or-later
#include "extensioneditor.h"

#include "editorhelpers.h"
#include "rules/extensionrule.h"
#include "widgets/formutils.h"

#include <QButtonGroup>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>

namespace ctrm::app {

using Action = ExtensionRule::Action;

ExtensionEditor::ExtensionEditor(QWidget *parent)
    : RuleEditor(parent)
    , m_pendingText(new PendingTextEdit(this))
    , m_actions(new QButtonGroup(this))
    , m_setValue(new QLineEdit)
    , m_addValue(new QLineEdit)
    , m_onlyIfMatches(new QLineEdit)
    , m_onlyForHint(new QLabel(tr("Empty = all items")))
    , m_folderNote(new QLabel(
              tr("Folders are changed too (Treat folder names as name + extension is on).")))
{
    m_setValue->setObjectName(QStringLiteral("setValue"));
    m_addValue->setObjectName(QStringLiteral("addValue"));
    m_onlyIfMatches->setObjectName(QStringLiteral("onlyIfMatches"));
    m_onlyForHint->setObjectName(QStringLiteral("onlyForHint"));
    m_folderNote->setObjectName(QStringLiteral("folderNote"));

    m_setValue->setAccessibleName(tr("New extension"));
    m_addValue->setAccessibleName(tr("Extension to add"));
    makeDimCaption(m_onlyForHint, this);
    m_folderNote->setWordWrap(true);
    makeDimCaption(m_folderNote, this);

    const QList<std::tuple<QString, const char *, Action, QLineEdit *>> actions{
            {tr("lower case"), "lower", Action::Lower, nullptr},
            {tr("UPPER CASE"), "upper", Action::Upper, nullptr},
            {tr("Set to"), "set", Action::Set, m_setValue},
            {tr("Add"), "add", Action::Add, m_addValue},
            {tr("Remove"), "remove", Action::Remove, nullptr},
    };
    QList<QWidget *> actionRows;
    for (const auto &[text, name, action, field] : actions) {
        auto *radio = new QRadioButton(text);
        radio->setObjectName(QString::fromLatin1(name));
        m_actions->addButton(radio, int(action));
        actionRows.append(field ? makeInlineField(radio, {field}) : static_cast<QWidget *>(radio));
    }
    m_actions->button(int(Action::Lower))->setChecked(true);

    QFormLayout *f = form();
    QLayout *actionGroup = makeFieldGroup(actionRows);
    f->addRow(tr("Action:"), actionGroup);
    alignLabelToFirstRow(qobject_cast<QLabel *>(f->labelForField(actionGroup)),
                         m_actions->button(int(Action::Lower)));
    // A field group: the visible edge of the field is the field edge (C04).
    QLayout *onlyForRow = makeFieldGroup({makeInlineField(m_onlyIfMatches, {m_onlyForHint})});
    f->addRow(tr("&Only for:"), onlyForRow);
    qobject_cast<QLabel *>(f->labelForField(onlyForRow))->setBuddy(m_onlyIfMatches);
    f->addRow(QString(), m_folderNote);
    f->setRowVisible(m_folderNote, false);

    connect(m_actions, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        updateEnabled();
        if (!checked || m_loading || !m_rule)
            return;
        m_rule->action = Action(id);
        emitEdited();
    });
    // Set and Add use the same value, so both fields show it.
    for (QLineEdit *field : {m_setValue, m_addValue, m_onlyIfMatches}) {
        watchTextField(field);
        m_pendingText->watch(field);
    }
    for (QLineEdit *field : {m_setValue, m_addValue}) {
        QLineEdit *other = field == m_setValue ? m_addValue : m_setValue;
        connect(field, &QLineEdit::textEdited, this, [this, other](const QString &text) {
            other->setText(text);
            if (m_rule)
                m_rule->value = text;
        });
    }
    connect(m_onlyIfMatches, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (m_rule)
            m_rule->onlyIfMatches = text;
    });
    updateEnabled();
}

QString ExtensionEditor::ruleType() const
{
    return QStringLiteral("extension");
}

void ExtensionEditor::setRule(ctrm::Rule *rule, int position)
{
    if (m_pendingText->isPending())
        emitEdited(); // the old rule changed; its preview must update
    cancelPendingEdit();
    setTitle(position, tr("Extension"));
    m_rule = dynamic_cast<ExtensionRule *>(rule);
    setEnabled(m_rule != nullptr);
    if (!m_rule)
        return;
    m_loading = true;
    m_actions->button(int(m_rule->action))->setChecked(true);
    m_setValue->setText(m_rule->value);
    m_addValue->setText(m_rule->value);
    m_onlyIfMatches->setText(m_rule->onlyIfMatches);
    m_loading = false;
    updateEnabled();
}

void ExtensionEditor::setFolderExtensions(bool on)
{
    form()->setRowVisible(m_folderNote, on);
}

void ExtensionEditor::updateEnabled()
{
    const int id = m_actions->checkedId();
    m_setValue->setEnabled(id == int(Action::Set));
    m_addValue->setEnabled(id == int(Action::Add));
}

} // namespace ctrm::app
