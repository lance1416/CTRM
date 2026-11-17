// SPDX-License-Identifier: GPL-3.0-or-later
#include "cleanupeditor.h"

#include "editorhelpers.h"
#include "rules/cleanuprule.h"
#include "widgets/formutils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace ctrm::app {

using Spaces = CleanUpRule::Spaces;

CleanUpEditor::CleanUpEditor(QWidget *parent)
    : RuleEditor(parent)
    , m_pendingText(new PendingTextEdit(this))
    , m_trim(new QCheckBox(tr("&Trim spaces at start and end")))
    , m_collapseSpaces(new QCheckBox(tr("Change &double spaces to one space")))
    , m_spacesTo(makeChoiceCombo({{tr("Keep"), int(Spaces::Keep)},
                                  {tr("Change to _"), int(Spaces::Underscore)},
                                  {tr("Change to -"), int(Spaces::Dash)},
                                  {tr("Change to ."), int(Spaces::Dot)},
                                  {tr("Remove"), int(Spaces::Remove)}}))
    , m_replaceInvalid(new QCheckBox(tr("&Replace invalid characters with")))
    , m_replacement(new QLineEdit)
    , m_portableNote(new QLabel)
    , m_applyTo(makeApplyToCombo(this))
{
    m_trim->setObjectName(QStringLiteral("trim"));
    m_collapseSpaces->setObjectName(QStringLiteral("collapseSpaces"));
    m_spacesTo->setObjectName(QStringLiteral("spacesTo"));
    m_replaceInvalid->setObjectName(QStringLiteral("replaceInvalid"));
    m_replacement->setObjectName(QStringLiteral("replacement"));
    m_portableNote->setObjectName(QStringLiteral("portableNote"));
    m_applyTo->setObjectName(QStringLiteral("applyTo"));

    m_replacement->setAccessibleName(tr("Replacement"));
    m_replacement->setText(QStringLiteral("_"));
    m_replacement->setEnabled(false);
    m_portableNote->setWordWrap(true);
    makeDimCaption(m_portableNote, this);

    QFormLayout *f = form();
    f->addRow(QString(), m_trim);
    f->addRow(QString(), m_collapseSpaces);
    f->addRow(tr("&Spaces:"), m_spacesTo);
    f->addRow(QString(), makeInlineField(m_replaceInvalid, {m_replacement}));
    f->addRow(QString(), m_portableNote);
    f->addRow(tr("&Apply to:"), m_applyTo);

    const auto edit = [this](auto write) {
        return [this, write](auto value) {
            if (m_loading || !m_rule)
                return;
            write(*m_rule, value);
            emitEdited();
        };
    };
    connect(m_trim, &QCheckBox::toggled, this, edit([](CleanUpRule &r, bool v) { r.trim = v; }));
    connect(m_collapseSpaces, &QCheckBox::toggled, this,
            edit([](CleanUpRule &r, bool v) { r.collapseSpaces = v; }));
    connect(m_spacesTo, &QComboBox::currentIndexChanged, this, edit([this](CleanUpRule &r, int) {
                r.spacesTo = Spaces(m_spacesTo->currentData().toInt());
            }));
    connect(m_replaceInvalid, &QCheckBox::toggled, m_replacement, &QWidget::setEnabled);
    connect(m_replaceInvalid, &QCheckBox::toggled, this,
            edit([](CleanUpRule &r, bool v) { r.replaceInvalid = v; }));
    connect(m_applyTo, &QComboBox::currentIndexChanged, this,
            edit([this](CleanUpRule &r, int) { r.target = selectedTarget(m_applyTo); }));
    watchTextField(m_replacement);
    m_pendingText->watch(m_replacement);
    connect(m_replacement, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (m_rule)
            m_rule->replacement = t;
    });

    setPortableNames(true);
}

QString CleanUpEditor::ruleType() const
{
    return QStringLiteral("cleanup");
}

void CleanUpEditor::setRule(ctrm::Rule *rule, int position)
{
    if (m_pendingText->isPending())
        emitEdited(); // the old rule changed; its preview must update
    cancelPendingEdit();
    setTitle(position, tr("Clean up"));
    m_rule = dynamic_cast<CleanUpRule *>(rule);
    setEnabled(m_rule != nullptr);
    if (!m_rule)
        return;
    m_loading = true;
    m_trim->setChecked(m_rule->trim);
    m_collapseSpaces->setChecked(m_rule->collapseSpaces);
    selectData(m_spacesTo, int(m_rule->spacesTo));
    m_replaceInvalid->setChecked(m_rule->replaceInvalid);
    m_replacement->setText(m_rule->replacement);
    showTarget(m_applyTo, m_rule->target);
    m_loading = false;
}

void CleanUpEditor::setPortableNames(bool on)
{
    m_portableNote->setText(on ? tr("Uses the Windows rules (portable names is on).")
                               : tr("Uses the rules of this system."));
}

} // namespace ctrm::app
