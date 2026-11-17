// SPDX-License-Identifier: GPL-3.0-or-later
#include "removeeditor.h"

#include "editorhelpers.h"
#include "rules/removerule.h"
#include "widgets/formutils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>

namespace ctrm::app {

namespace {
constexpr int kMaxSpin = 99999; // ui-design 6.1 rule 8: a width for 5 digits
}

using Mode = RemoveRule::Mode;
using Occurrence = RemoveRule::Occurrence;

template <typename Write>
auto RemoveEditor::edit(Write write)
{
    return [this, write](auto value) {
        if (m_loading || !m_rule)
            return;
        write(*m_rule, value);
        emitEdited();
    };
}

RemoveEditor::RemoveEditor(QWidget *parent)
    : RuleEditor(parent)
    , m_pendingText(new PendingTextEdit(this))
    , m_mode(makeChoiceCombo({{tr("Range"), int(Mode::Range)},
                              {tr("Characters"), int(Mode::Characters)},
                              {tr("Text between"), int(Mode::Between)}}))
    , m_from(new QSpinBox)
    , m_rangeFromEnd(new QCheckBox(tr("From &end")))
    , m_count(new QSpinBox)
    , m_customText(new QLineEdit)
    , m_open(new QLineEdit)
    , m_close(new QLineEdit)
    , m_occurrence(makeChoiceCombo(
              {{tr("All"), int(Occurrence::All)}, {tr("First"), int(Occurrence::First)}}))
    , m_keepDelimiters(new QCheckBox(tr("&Keep the delimiters")))
    , m_applyTo(makeApplyToCombo(this))
{
    m_mode->setObjectName(QStringLiteral("mode"));
    m_from->setObjectName(QStringLiteral("from"));
    m_rangeFromEnd->setObjectName(QStringLiteral("rangeFromEnd"));
    m_count->setObjectName(QStringLiteral("count"));
    m_customText->setObjectName(QStringLiteral("customText"));
    m_open->setObjectName(QStringLiteral("open"));
    m_close->setObjectName(QStringLiteral("close"));
    m_occurrence->setObjectName(QStringLiteral("betweenOccurrence"));
    m_keepDelimiters->setObjectName(QStringLiteral("keepDelimiters"));
    m_applyTo->setObjectName(QStringLiteral("applyTo"));

    m_from->setRange(0, kMaxSpin);
    m_count->setRange(0, kMaxSpin);
    m_customText->setAccessibleName(tr("These characters"));

    const QList<std::tuple<QString, const char *, quint32>> classes{
            {tr("&Digits"), "digits", RemoveRule::Digits},
            {tr("&Letters"), "letters", RemoveRule::Letters},
            {tr("&Spaces"), "spaces", RemoveRule::Spaces},
            {tr("&Punctuation"), "punctuation", RemoveRule::Punctuation},
            {tr("S&ymbols"), "symbols", RemoveRule::Symbols},
            {tr("&Non-ASCII characters"), "nonAscii", RemoveRule::NonAscii},
            {tr("&These characters:"), "custom", RemoveRule::Custom},
    };
    QList<QWidget *> classRows;
    for (const auto &[text, name, bit] : classes) {
        auto *check = new QCheckBox(text);
        check->setObjectName(QString::fromLatin1(name));
        m_classes.append({check, bit});
        connect(check, &QCheckBox::toggled, this, [this] {
            m_customText->setEnabled(m_classes.last().first->isChecked());
            writeClasses();
        });
        classRows.append(bit == RemoveRule::Custom ? makeInlineField(check, {m_customText})
                                                   : check);
    }
    m_classesGroup = makeFieldGroup(classRows);

    QFormLayout *f = form();
    f->addRow(tr("&Mode:"), m_mode);
    QWidget *fromRow = makeInlineField(m_from, {m_rangeFromEnd});
    fromRow->setFocusProxy(m_from); // the label mnemonic goes to the spin box
    f->addRow(tr("&From:"), fromRow);
    f->addRow(tr("&Count:"), m_count);
    f->addRow(tr("Remove:"), m_classesGroup);
    alignLabelToFirstRow(qobject_cast<QLabel *>(f->labelForField(m_classesGroup)),
                         m_classes.first().first);
    f->addRow(tr("&Open:"), m_open);
    f->addRow(tr("Clos&e:"), m_close);
    f->addRow(tr("O&ccurrence:"), m_occurrence);
    f->addRow(QString(), m_keepDelimiters);
    f->addRow(tr("&Apply to:"), m_applyTo);

    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        updateModeRows();
        if (m_loading || !m_rule)
            return;
        m_rule->mode = Mode(m_mode->currentData().toInt());
        emitEdited();
    });
    connect(m_from, &QSpinBox::valueChanged, this, edit([](RemoveRule &r, int v) { r.from = v; }));
    connect(m_count, &QSpinBox::valueChanged, this,
            edit([](RemoveRule &r, int v) { r.count = v; }));
    connect(m_rangeFromEnd, &QCheckBox::toggled, this,
            edit([](RemoveRule &r, bool v) { r.fromEnd = v; }));
    connect(m_keepDelimiters, &QCheckBox::toggled, this,
            edit([](RemoveRule &r, bool v) { r.keepDelimiters = v; }));
    connect(m_occurrence, &QComboBox::currentIndexChanged, this, edit([this](RemoveRule &r, int) {
                r.occurrence = Occurrence(m_occurrence->currentData().toInt());
            }));
    connect(m_applyTo, &QComboBox::currentIndexChanged, this,
            edit([this](RemoveRule &r, int) { r.target = selectedTarget(m_applyTo); }));

    for (QLineEdit *field : {m_customText, m_open, m_close}) {
        watchTextField(field);
        m_pendingText->watch(field);
    }
    connect(m_customText, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (m_rule)
            m_rule->custom = t;
    });
    connect(m_open, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (m_rule)
            m_rule->open = t;
    });
    connect(m_close, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (m_rule)
            m_rule->close = t;
    });

    m_customText->setEnabled(false);
    updateModeRows();
}

QString RemoveEditor::ruleType() const
{
    return QStringLiteral("remove");
}

void RemoveEditor::setRule(ctrm::Rule *rule, int position)
{
    if (m_pendingText->isPending())
        emitEdited(); // the old rule changed; its preview must update
    cancelPendingEdit();
    setTitle(position, tr("Remove"));
    m_rule = dynamic_cast<RemoveRule *>(rule);
    setEnabled(m_rule != nullptr);
    if (!m_rule)
        return;
    m_loading = true;
    selectData(m_mode, int(m_rule->mode));
    m_from->setValue(m_rule->from);
    m_rangeFromEnd->setChecked(m_rule->fromEnd);
    m_count->setValue(m_rule->count);
    for (const auto &[check, bit] : std::as_const(m_classes))
        check->setChecked(m_rule->classes & bit);
    m_customText->setText(m_rule->custom);
    m_open->setText(m_rule->open);
    m_close->setText(m_rule->close);
    selectData(m_occurrence, int(m_rule->occurrence));
    m_keepDelimiters->setChecked(m_rule->keepDelimiters);
    showTarget(m_applyTo, m_rule->target);
    m_loading = false;
    updateModeRows();
}

void RemoveEditor::updateModeRows()
{
    const auto mode = Mode(m_mode->currentData().toInt());
    QFormLayout *f = form();
    f->setRowVisible(m_from->parentWidget(), mode == Mode::Range);
    f->setRowVisible(m_count, mode == Mode::Range);
    f->setRowVisible(m_classesGroup, mode == Mode::Characters);
    for (QWidget *w : QList<QWidget *>{m_open, m_close, m_occurrence, m_keepDelimiters})
        f->setRowVisible(w, mode == Mode::Between);
}

void RemoveEditor::writeClasses()
{
    if (m_loading || !m_rule)
        return;
    quint32 classes = 0;
    for (const auto &[check, bit] : std::as_const(m_classes)) {
        if (check->isChecked())
            classes |= bit;
    }
    m_rule->classes = classes;
    emitEdited();
}

} // namespace ctrm::app
