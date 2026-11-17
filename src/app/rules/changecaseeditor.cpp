// SPDX-License-Identifier: GPL-3.0-or-later
#include "changecaseeditor.h"

#include "editorhelpers.h"
#include "rules/changecaserule.h"
#include "widgets/formutils.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QToolButton>

namespace ctrm::app {

using Case = ChangeCaseRule::Case;

ChangeCaseEditor::ChangeCaseEditor(QWidget *parent)
    : RuleEditor(parent)
    , m_pendingText(new PendingTextEdit(this))
    , m_cases(new QButtonGroup(this))
    , m_applyTo(makeApplyToCombo(this))
    , m_more(nullptr)
    , m_separators(new QLineEdit)
    , m_smallWords(new QLineEdit)
    , m_builtInSmallWords(new QCheckBox(tr("&Use the built-in English list")))
{
    m_applyTo->setObjectName(QStringLiteral("applyTo"));
    m_separators->setObjectName(QStringLiteral("separators"));
    m_smallWords->setObjectName(QStringLiteral("smallWords"));
    m_builtInSmallWords->setObjectName(QStringLiteral("builtInSmallWords"));

    // Each label shows its own result style (ui-design 8.5).
    const QList<std::tuple<QString, const char *, Case>> cases{
            {tr("lower case"), "lower", Case::Lower},
            {tr("UPPER CASE"), "upper", Case::Upper},
            {tr("Title Case"), "title", Case::Title},
            {tr("Sentence case"), "sentence", Case::Sentence},
            {tr("iNVERT cASE"), "invert", Case::Invert},
    };
    QList<QWidget *> radios;
    for (const auto &[text, name, mode] : cases) {
        auto *radio = new QRadioButton(text);
        radio->setObjectName(QString::fromLatin1(name));
        m_cases->addButton(radio, int(mode));
        radios.append(radio);
    }
    m_cases->button(int(Case::Lower))->setChecked(true);

    QFormLayout *f = form();
    QLayout *caseGroup = makeFieldGroup(radios);
    f->addRow(tr("Case:"), caseGroup);
    alignLabelToFirstRow(qobject_cast<QLabel *>(f->labelForField(caseGroup)), radios.first());
    f->addRow(tr("&Apply to:"), m_applyTo);
    auto *more = new MoreOptions(f, tr("More options"), this);
    m_more = more->button();
    m_more->setObjectName(QStringLiteral("moreOptions"));
    f->addRow(tr("Se&parators:"), m_separators);
    f->addRow(tr("S&mall words:"), m_smallWords);
    f->addRow(QString(), m_builtInSmallWords);
    // The rows are for Title Case only, so this editor sets their visibility itself. This
    // connection runs after the one of MoreOptions.
    connect(m_more, &QToolButton::toggled, this, [this] { updateTitleRows(); });

    connect(m_cases, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (!checked)
            return;
        updateTitleRows();
        if (m_loading || !m_rule)
            return;
        m_rule->caseMode = Case(id);
        emitEdited();
    });
    connect(m_applyTo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading || !m_rule)
            return;
        m_rule->target = selectedTarget(m_applyTo);
        emitEdited();
    });
    connect(m_builtInSmallWords, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading || !m_rule)
            return;
        m_rule->builtInSmallWords = on;
        emitEdited();
    });
    for (QLineEdit *field : {m_separators, m_smallWords}) {
        watchTextField(field);
        m_pendingText->watch(field);
    }
    connect(m_separators, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (m_rule)
            m_rule->separators = text;
    });
    connect(m_smallWords, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (!m_rule)
            return;
        QStringList words;
        for (const QString &word : text.split(QLatin1Char(','))) {
            const QString trimmed = word.trimmed();
            if (!trimmed.isEmpty())
                words.append(trimmed);
        }
        m_rule->smallWords = words;
    });

    m_separators->setText(ChangeCaseRule().separators);
    updateTitleRows();
}

QString ChangeCaseEditor::ruleType() const
{
    return QStringLiteral("changeCase");
}

void ChangeCaseEditor::setRule(ctrm::Rule *rule, int position)
{
    if (m_pendingText->isPending())
        emitEdited(); // the old rule changed; its preview must update
    cancelPendingEdit();
    setTitle(position, tr("Change case"));
    m_rule = dynamic_cast<ChangeCaseRule *>(rule);
    setEnabled(m_rule != nullptr);
    if (!m_rule)
        return;
    m_loading = true;
    m_cases->button(int(m_rule->caseMode))->setChecked(true);
    showTarget(m_applyTo, m_rule->target);
    m_separators->setText(m_rule->separators);
    m_smallWords->setText(m_rule->smallWords.join(QStringLiteral(", ")));
    m_builtInSmallWords->setChecked(m_rule->builtInSmallWords);
    m_loading = false;
    updateTitleRows();
}

void ChangeCaseEditor::updateTitleRows()
{
    const bool title = m_cases->checkedId() == int(Case::Title);
    QFormLayout *f = form();
    f->setRowVisible(m_more, title);
    for (QWidget *w : QList<QWidget *>{m_separators, m_smallWords, m_builtInSmallWords})
        f->setRowVisible(w, title && m_more->isChecked());
}

} // namespace ctrm::app
