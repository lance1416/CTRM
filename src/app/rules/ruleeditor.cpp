// SPDX-License-Identifier: GPL-3.0-or-later
#include "ruleeditor.h"

#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/typography.h"
#include "widgets/formutils.h"

#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace ctrm::app {

namespace {

constexpr int kTextDebounceMs = 150;
constexpr int kErrorIconSize = 16;

void colorErrorRow(QWidget *row)
{
    const CtrmColors colors = CtrmColors::fromPalette(row->palette());
    if (auto *icon = row->findChild<QLabel *>(QStringLiteral("fieldErrorIcon")))
        icon->setPixmap(
                stateIcon(StateIcon::Error, colors)
                        .pixmap(QSize(kErrorIconSize, kErrorIconSize), row->devicePixelRatioF()));
    if (auto *text = row->findChild<QLabel *>(QStringLiteral("fieldErrorText"))) {
        QPalette p = text->palette();
        p.setColor(QPalette::WindowText, colors.stateError);
        text->setPalette(p);
    }
}

QWidget *makeErrorRow()
{
    auto *row = new QWidget;
    row->setObjectName(QStringLiteral("fieldError"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *icon = new QLabel(row);
    icon->setObjectName(QStringLiteral("fieldErrorIcon"));
    icon->setAlignment(Qt::AlignTop);
    auto *text = new QLabel(row);
    text->setObjectName(QStringLiteral("fieldErrorText"));
    text->setWordWrap(true);
    text->setTextFormat(Qt::PlainText);
    layout->addWidget(icon, 0, Qt::AlignTop);
    layout->addWidget(text, 1);
    colorErrorRow(row);
    return row;
}

} // namespace

RuleEditor::RuleEditor(QWidget *parent)
    : QWidget(parent)
    , m_title(new QLabel(this))
{
    m_title->setObjectName(QStringLiteral("ruleEditorTitle"));
    m_title->setTextFormat(Qt::PlainText);
    auto *layout = new QVBoxLayout(this);
    // The rule panel gives the side margins: the title starts on the line of the "Rules"
    // header, and the form on the line of Chain options (M19).
    const QMargins margins = layout->contentsMargins();
    layout->setContentsMargins(0, margins.top(), 0, margins.bottom());
    layout->addWidget(m_title);
    auto *formWidget = new QWidget(this);
    m_form = makeForm(formWidget);
    m_form->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(formWidget);
    layout->addStretch(1);

    m_textTimer.setSingleShot(true);
    m_textTimer.setTimerType(Qt::PreciseTimer);
    m_textTimer.setInterval(kTextDebounceMs);
    connect(&m_textTimer, &QTimer::timeout, this, &RuleEditor::ruleEdited);
    updateFonts();
}

QFormLayout *RuleEditor::form() const
{
    return m_form;
}

void RuleEditor::showFieldError(int row, const QString &text)
{
    QLayoutItem *item = m_form->itemAt(row, QFormLayout::FieldRole);
    QWidget *field = item ? item->widget() : nullptr;
    if (!field)
        return;
    QPointer<QWidget> errorRow = m_errorRows.value(field);
    if (!errorRow) {
        if (text.isEmpty())
            return;
        errorRow = makeErrorRow();
        m_form->insertRow(row + 1, QString(), errorRow);
        m_errorRows.insert(field, errorRow);
    }
    errorRow->findChild<QLabel *>(QStringLiteral("fieldErrorText"))->setText(text);
    m_form->setRowVisible(errorRow, !text.isEmpty() && m_form->isRowVisible(row));
}

void RuleEditor::setTitle(int position, const QString &displayName)
{
    m_title->setText(tr("Rule %1: %2").arg(position).arg(displayName));
}

void RuleEditor::watchTextField(QLineEdit *field)
{
    connect(field, &QLineEdit::textEdited, &m_textTimer, qOverload<>(&QTimer::start));
}

void RuleEditor::cancelPendingEdit()
{
    m_textTimer.stop();
}

void RuleEditor::emitEdited()
{
    m_textTimer.stop();
    emit ruleEdited();
}

void RuleEditor::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::FontChange)
        updateFonts();
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        for (const QPointer<QWidget> &row : std::as_const(m_errorRows)) {
            if (row)
                colorErrorRow(row);
        }
    }
    QWidget::changeEvent(event);
}

void RuleEditor::updateFonts()
{
    m_title->setFont(typeStyle(TypeToken::Heading, font()));
}

} // namespace ctrm::app
