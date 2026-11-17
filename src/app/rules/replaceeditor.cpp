// SPDX-License-Identifier: GPL-3.0-or-later
#include "replaceeditor.h"

#include "editorhelpers.h"
#include "rules/replacerule.h"
#include "widgets/formutils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace ctrm::app {

using Mode = ReplaceRule::Mode;
using Occurrence = ReplaceRule::Occurrence;

ReplaceEditor::ReplaceEditor(QWidget *parent)
    : RuleEditor(parent)
    , m_pendingText(new PendingTextEdit(this))
    , m_find(new QLineEdit)
    , m_hint(new QLabel)
    , m_replace(new QLineEdit)
    , m_mode(makeChoiceCombo({{tr("Literal"), int(Mode::Literal)},
                              {tr("Wildcard"), int(Mode::Wildcard)},
                              {tr("Regular expression"), int(Mode::Regex)}}))
    , m_occurrence(makeChoiceCombo({{tr("All"), int(Occurrence::All)},
                                    {tr("First"), int(Occurrence::First)},
                                    {tr("Last"), int(Occurrence::Last)}}))
    , m_matchCase(new QCheckBox(tr("Match &case")))
    , m_wholeWord(new QCheckBox(tr("W&hole words only")))
    , m_applyTo(makeApplyToCombo(nullptr)) // the form row sets the parent: tab order
{
    m_find->setObjectName(QStringLiteral("find"));
    m_hint->setObjectName(QStringLiteral("hint"));
    m_replace->setObjectName(QStringLiteral("replace"));
    m_mode->setObjectName(QStringLiteral("mode"));
    m_occurrence->setObjectName(QStringLiteral("occurrence"));
    m_matchCase->setObjectName(QStringLiteral("matchCase"));
    m_wholeWord->setObjectName(QStringLiteral("wholeWord"));
    m_applyTo->setObjectName(QStringLiteral("applyTo"));

    m_hint->setWordWrap(true);
    m_hint->setTextFormat(Qt::RichText);
    makeDimCaption(m_hint, this);
    connect(m_hint, &QLabel::linkActivated, this, &ReplaceEditor::syntaxHelpRequested);

    QFormLayout *f = form();
    f->addRow(tr("&Find:"), m_find);
    f->addRow(QString(), m_hint);
    f->addRow(tr("Replace &with:"), m_replace);
    f->addRow(tr("&Mode:"), m_mode);
    f->addRow(tr("&Occurrence:"), m_occurrence);
    f->addRow(QString(), m_matchCase);
    f->addRow(QString(), m_wholeWord);
    f->addRow(tr("&Apply to:"), m_applyTo);

    watchTextField(m_find);

    m_pendingText->watch(m_find);
    watchTextField(m_replace);
    m_pendingText->watch(m_replace);
    connect(m_find, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (!m_rule)
            return;
        m_rule->find = text;
        updateError();
    });
    connect(m_replace, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (m_rule)
            m_rule->replace = text;
    });
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        updateModeRows();
        if (m_loading || !m_rule)
            return;
        m_rule->mode = Mode(m_mode->currentData().toInt());
        updateError();
        emitEdited();
    });
    connect(m_occurrence, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading || !m_rule)
            return;
        m_rule->occurrence = Occurrence(m_occurrence->currentData().toInt());
        emitEdited();
    });
    connect(m_matchCase, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading || !m_rule)
            return;
        m_rule->caseSensitive = on;
        emitEdited();
    });
    connect(m_wholeWord, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading || !m_rule)
            return;
        m_rule->wholeWord = on;
        emitEdited();
    });
    connect(m_applyTo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading || !m_rule)
            return;
        m_rule->target = selectedTarget(m_applyTo);
        emitEdited();
    });

    updateModeRows();
}

QString ReplaceEditor::ruleType() const
{
    return QStringLiteral("replace");
}

void ReplaceEditor::setRule(ctrm::Rule *rule, int position)
{
    if (m_pendingText->isPending())
        emitEdited(); // the old rule changed; its preview must update
    cancelPendingEdit();
    setTitle(position, tr("Replace"));
    m_rule = dynamic_cast<ReplaceRule *>(rule);
    setEnabled(m_rule != nullptr);
    if (!m_rule)
        return;
    m_loading = true;
    m_find->setText(m_rule->find);
    m_replace->setText(m_rule->replace);
    selectData(m_mode, int(m_rule->mode));
    selectData(m_occurrence, int(m_rule->occurrence));
    m_matchCase->setChecked(m_rule->caseSensitive);
    m_wholeWord->setChecked(m_rule->wholeWord);
    showTarget(m_applyTo, m_rule->target);
    m_loading = false;
    updateModeRows();
    updateError();
}

void ReplaceEditor::changeEvent(QEvent *event)
{
    RuleEditor::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        updateModeRows(); // the pattern font follows the editor font size
}

void ReplaceEditor::updateModeRows()
{
    const auto mode = Mode(m_mode->currentData().toInt());
    form()->setRowVisible(m_wholeWord, mode == Mode::Literal);

    switch (mode) {
    case Mode::Literal:
        m_hint->clear();
        break;
    case Mode::Wildcard:
        m_hint->setText(tr("* = any text, ? = one character").toHtmlEscaped());
        break;
    case Mode::Regex:
        m_hint->setText(
                tr("Use $1, $2 ... for groups, $0 for the full match, $$ for a $ sign.")
                        .toHtmlEscaped() +
                QStringLiteral(" <a href=\"help\">%1</a>").arg(tr("Syntax help").toHtmlEscaped()));
        break;
    }
    form()->setRowVisible(m_hint, mode != Mode::Literal);

    // A pattern font only for patterns (design-tokens 3.1). QFont() inherits the editor font.
    const QFont fieldFont = mode == Mode::Literal ? QFont() : patternFont(font());
    m_find->setFont(fieldFont);
    m_replace->setFont(fieldFont);
}

void ReplaceEditor::updateError()
{
    int row = -1;
    QFormLayout::ItemRole role;
    form()->getWidgetPosition(m_find, &row, &role);
    const std::optional<RuleProblem> problem = m_rule ? m_rule->problem() : std::nullopt;
    QString text;
    if (problem) {
        QString message = problem->message;
        if (!message.isEmpty())
            message[0] = message.at(0).toUpper();
        text = problem->position >= 0 ? tr("%1 at position %2.").arg(message).arg(problem->position)
                                      : tr("%1.").arg(message);
    }
    showFieldError(row, text);
}

} // namespace ctrm::app
