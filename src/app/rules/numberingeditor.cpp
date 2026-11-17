// SPDX-License-Identifier: GPL-3.0-or-later
#include "numberingeditor.h"

#include "editorhelpers.h"
#include "rules/numberingrule.h"
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
constexpr int kMaxSpin = 99999; // ui-design 6.1 rule 8: a width for 5 digits
constexpr int kMaxPadding = 10;
} // namespace

using Position = NumberingRule::Position;
using Scope = NumberingRule::Scope;

NumberingEditor::NumberingEditor(QWidget *parent)
    : RuleEditor(parent)
    , m_pendingText(new PendingTextEdit(this))
    , m_start(new QSpinBox)
    , m_step(new QSpinBox)
    , m_padding(new QComboBox)
    , m_example(new QLabel)
    , m_positions(new QButtonGroup(this))
    , m_index(new QSpinBox)
    , m_fromEnd(new QCheckBox(tr("From &end")))
    , m_separator(new QLineEdit)
    , m_scope(makeChoiceCombo({{tr("Included items only"), int(Scope::Checked)},
                               {tr("All items"), int(Scope::All)}}))
    , m_applyTo(makeApplyToCombo(this))
    , m_orderNote(new QLabel(tr("Order: the table order. Sort or drag rows to change it.")))
{
    m_start->setObjectName(QStringLiteral("start"));
    m_step->setObjectName(QStringLiteral("step"));
    m_padding->setObjectName(QStringLiteral("padding"));
    m_example->setObjectName(QStringLiteral("example"));
    m_index->setObjectName(QStringLiteral("index"));
    m_fromEnd->setObjectName(QStringLiteral("fromEnd"));
    m_separator->setObjectName(QStringLiteral("separator"));
    m_scope->setObjectName(QStringLiteral("scope"));
    m_applyTo->setObjectName(QStringLiteral("applyTo"));
    m_orderNote->setObjectName(QStringLiteral("orderNote"));

    const NumberingRule defaults;
    m_start->setRange(0, kMaxSpin);
    m_start->setValue(defaults.start);
    m_step->setRange(1, kMaxSpin);
    m_step->setValue(defaults.step);
    m_padding->addItem(tr("Auto"), -1);
    for (int digits = 1; digits <= kMaxPadding; ++digits)
        m_padding->addItem(QString::number(digits), digits);
    m_index->setRange(0, kMaxSpin);
    m_index->setAccessibleName(tr("Index"));
    m_separator->setText(defaults.separator);
    m_orderNote->setWordWrap(true);
    makeDimCaption(m_orderNote, this);

    const QList<std::tuple<QString, const char *, Position>> positions{
            {tr("Suffix"), "suffix", Position::Suffix},
            {tr("Prefix"), "prefix", Position::Prefix},
            {tr("At index"), "atIndex", Position::Index},
            {tr("Replace the name"), "replaceName", Position::Replace},
    };
    QList<QWidget *> positionRows;
    for (const auto &[text, name, position] : positions) {
        auto *radio = new QRadioButton(text);
        radio->setObjectName(QString::fromLatin1(name));
        m_positions->addButton(radio, int(position));
        positionRows.append(position == Position::Index
                                    ? makeInlineField(radio, {m_index, m_fromEnd})
                                    : static_cast<QWidget *>(radio));
    }
    m_positions->button(int(defaults.position))->setChecked(true);

    QFormLayout *f = form();
    f->addRow(tr("&Start at:"), m_start);
    f->addRow(tr("S&tep:"), m_step);
    QWidget *paddingRow = makeInlineField(m_padding, {m_example});
    paddingRow->setFocusProxy(m_padding); // the label mnemonic goes to the combo box
    f->addRow(tr("&Padding:"), paddingRow);
    QLayout *positionGroup = makeFieldGroup(positionRows);
    f->addRow(tr("Position:"), positionGroup);
    alignLabelToFirstRow(qobject_cast<QLabel *>(f->labelForField(positionGroup)),
                         m_positions->button(int(Position::Suffix)));
    f->addRow(tr("Separato&r:"), m_separator);
    f->addRow(tr("&Number:"), m_scope);
    f->addRow(tr("&Apply to:"), m_applyTo);
    f->addRow(QString(), m_orderNote);

    const auto edit = [this](auto write) {
        return [this, write](auto value) {
            if (m_loading || !m_rule)
                return;
            write(*m_rule, value);
            emitEdited();
        };
    };
    connect(m_start, &QSpinBox::valueChanged, this, [this] { updateExample(); });
    connect(m_step, &QSpinBox::valueChanged, this, [this] { updateExample(); });
    connect(m_padding, &QComboBox::currentIndexChanged, this, [this] { updateExample(); });
    connect(m_start, &QSpinBox::valueChanged, this,
            edit([](NumberingRule &r, int v) { r.start = v; }));
    connect(m_step, &QSpinBox::valueChanged, this,
            edit([](NumberingRule &r, int v) { r.step = v; }));
    connect(m_padding, &QComboBox::currentIndexChanged, this,
            edit([this](NumberingRule &r, int) { r.padding = m_padding->currentData().toInt(); }));
    connect(m_positions, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        const bool atIndex = m_positions->checkedId() == int(Position::Index);
        m_index->setEnabled(atIndex);
        m_fromEnd->setEnabled(atIndex);
        if (!checked || m_loading || !m_rule)
            return;
        m_rule->position = Position(id);
        emitEdited();
    });
    connect(m_index, &QSpinBox::valueChanged, this,
            edit([](NumberingRule &r, int v) { r.index = v; }));
    connect(m_fromEnd, &QCheckBox::toggled, this,
            edit([](NumberingRule &r, bool v) { r.fromEnd = v; }));
    connect(m_scope, &QComboBox::currentIndexChanged, this, edit([this](NumberingRule &r, int) {
                r.scope = Scope(m_scope->currentData().toInt());
            }));
    connect(m_applyTo, &QComboBox::currentIndexChanged, this,
            edit([this](NumberingRule &r, int) { r.target = selectedTarget(m_applyTo); }));
    watchTextField(m_separator);
    m_pendingText->watch(m_separator);
    connect(m_separator, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (m_rule)
            m_rule->separator = text;
    });

    m_index->setEnabled(false);
    m_fromEnd->setEnabled(false);
    updateExample();
}

QString NumberingEditor::ruleType() const
{
    return QStringLiteral("numbering");
}

void NumberingEditor::setRule(ctrm::Rule *rule, int position)
{
    if (m_pendingText->isPending())
        emitEdited(); // the old rule changed; its preview must update
    cancelPendingEdit();
    setTitle(position, tr("Numbering"));
    m_rule = dynamic_cast<NumberingRule *>(rule);
    setEnabled(m_rule != nullptr);
    if (!m_rule)
        return;
    m_loading = true;
    m_start->setValue(m_rule->start);
    m_step->setValue(m_rule->step);
    // Padding 0 and 1 give the same result. The combo box shows 0 as 1.
    selectData(m_padding, m_rule->padding < 0 ? -1 : qBound(1, m_rule->padding, kMaxPadding));
    m_positions->button(int(m_rule->position))->setChecked(true);
    m_index->setValue(m_rule->index);
    m_fromEnd->setChecked(m_rule->fromEnd);
    m_separator->setText(m_rule->separator);
    selectData(m_scope, int(m_rule->scope));
    showTarget(m_applyTo, m_rule->target);
    m_loading = false;
    updateExample();
}

void NumberingEditor::setNumberedCount(int n)
{
    m_numberedCount = qMax(1, n);
    updateExample();
}

void NumberingEditor::updateExample()
{
    const int padding = m_padding->currentData().toInt();
    const int digits = padding < 0 ? NumberingRule::autoDigits(m_start->value(), m_step->value(),
                                                               m_numberedCount)
                                   : padding;
    m_example->setText(tr("Example: %1").arg(m_start->value(), digits, 10, QLatin1Char('0')));
}

} // namespace ctrm::app
