// SPDX-License-Identifier: GPL-3.0-or-later
#include "inserteditor.h"

#include "editorhelpers.h"
#include "rules/insertrule.h"
#include "widgets/formutils.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>

namespace ctrm::app {

namespace {
constexpr int kMaxIndex = 99999; // ui-design 6.1 rule 8: a width for 5 digits
}

using Position = InsertRule::Position;

InsertEditor::InsertEditor(QWidget *parent)
    : RuleEditor(parent)
    , m_pendingText(new PendingTextEdit(this))
    , m_text(new QLineEdit)
    , m_prefix(new QRadioButton(tr("&Prefix")))
    , m_suffix(new QRadioButton(tr("&Suffix")))
    , m_atIndex(new QRadioButton(tr("At &index")))
    , m_index(new QSpinBox)
    , m_fromEnd(new QCheckBox(tr("From &end")))
    , m_applyTo(makeApplyToCombo(this))
{
    m_text->setObjectName(QStringLiteral("text"));
    m_prefix->setObjectName(QStringLiteral("prefix"));
    m_suffix->setObjectName(QStringLiteral("suffix"));
    m_atIndex->setObjectName(QStringLiteral("atIndex"));
    m_index->setObjectName(QStringLiteral("index"));
    m_fromEnd->setObjectName(QStringLiteral("fromEnd"));
    m_applyTo->setObjectName(QStringLiteral("applyTo"));

    m_index->setRange(0, kMaxIndex);
    m_index->setAccessibleName(tr("Index"));
    auto *group = new QButtonGroup(this);
    for (QRadioButton *radio : {m_prefix, m_suffix, m_atIndex})
        group->addButton(radio);
    m_prefix->setChecked(true);

    QFormLayout *f = form();
    f->addRow(tr("&Text:"), m_text);
    QLayout *positions =
            makeFieldGroup({m_prefix, m_suffix, makeInlineField(m_atIndex, {m_index, m_fromEnd})});
    f->addRow(tr("Position:"), positions);
    alignLabelToFirstRow(qobject_cast<QLabel *>(f->labelForField(positions)), m_prefix);
    f->addRow(tr("&Apply to:"), m_applyTo);

    watchTextField(m_text);

    m_pendingText->watch(m_text);
    connect(m_text, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (m_rule)
            m_rule->text = text;
    });
    connect(group, &QButtonGroup::buttonToggled, this, [this](QAbstractButton *, bool checked) {
        updateEnabled();
        if (checked)
            writePosition();
    });
    connect(m_index, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_loading || !m_rule)
            return;
        m_rule->index = value;
        emitEdited();
    });
    connect(m_fromEnd, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading || !m_rule)
            return;
        m_rule->fromEnd = on;
        emitEdited();
    });
    connect(m_applyTo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading || !m_rule)
            return;
        m_rule->target = selectedTarget(m_applyTo);
        emitEdited();
    });
    updateEnabled();
}

QString InsertEditor::ruleType() const
{
    return QStringLiteral("insert");
}

void InsertEditor::setRule(ctrm::Rule *rule, int position)
{
    if (m_pendingText->isPending())
        emitEdited(); // the old rule changed; its preview must update
    cancelPendingEdit();
    setTitle(position, tr("Insert"));
    m_rule = dynamic_cast<InsertRule *>(rule);
    setEnabled(m_rule != nullptr);
    if (!m_rule)
        return;
    m_loading = true;
    m_text->setText(m_rule->text);
    switch (m_rule->position) {
    case Position::Prefix:
        m_prefix->setChecked(true);
        break;
    case Position::Suffix:
        m_suffix->setChecked(true);
        break;
    case Position::Index:
        m_atIndex->setChecked(true);
        break;
    }
    m_index->setValue(m_rule->index);
    m_fromEnd->setChecked(m_rule->fromEnd);
    showTarget(m_applyTo, m_rule->target);
    m_loading = false;
    updateEnabled();
}

void InsertEditor::writePosition()
{
    if (m_loading || !m_rule)
        return;
    m_rule->position = m_atIndex->isChecked()  ? Position::Index
                       : m_suffix->isChecked() ? Position::Suffix
                                               : Position::Prefix;
    emitEdited();
}

void InsertEditor::updateEnabled()
{
    m_index->setEnabled(m_atIndex->isChecked());
    m_fromEnd->setEnabled(m_atIndex->isChecked());
}

} // namespace ctrm::app
