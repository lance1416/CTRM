// SPDX-License-Identifier: GPL-3.0-or-later
#include "formutils.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace ctrm::app {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ctrm::app::FormUtils", text);
}

bool isFontOrStyleChange(const QEvent *event)
{
    return event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange;
}

// Keeps a label as tall as the first control of its field group.
class LabelAligner : public QObject
{
public:
    LabelAligner(QLabel *label, QWidget *control)
        : QObject(label)
        , m_label(label)
        , m_control(control)
    {
        label->installEventFilter(this);
        control->installEventFilter(this);
        apply();
    }

    void apply()
    {
        if (!m_label || !m_control)
            return;
        // QFormLayout can make a label taller than its size hint. A fixed height keeps the
        // text center on the center of the first control.
        m_label->setSizePolicy(m_label->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
        m_label->setMinimumHeight(m_control->sizeHint().height());
        m_label->setAlignment((m_label->alignment() & Qt::AlignHorizontal_Mask) | Qt::AlignVCenter);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (isFontOrStyleChange(event)) {
            // The label and the control can get the change in any order: apply now and
            // again when both have it.
            apply();
            QTimer::singleShot(0, this, [this] { apply(); });
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QLabel> m_label;
    QPointer<QWidget> m_control;
};

} // namespace

QFormLayout *makeForm(QWidget *parent)
{
    auto *form = new QFormLayout(parent);
    const QStyle *style = parent ? parent->style() : QApplication::style();
    const auto horizontal =
            Qt::Alignment(style->styleHint(QStyle::SH_FormLayoutLabelAlignment, nullptr, parent)) &
            Qt::AlignHorizontal_Mask;
    form->setLabelAlignment(horizontal | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    return form;
}

QWidget *makeFieldGroup(const QList<QWidget *> &controls)
{
    auto *group = new QWidget;
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    for (QWidget *control : controls)
        layout->addWidget(control);
    return group;
}

QWidget *makeInlineField(QWidget *first, const QList<QWidget *> &more)
{
    auto *field = new QWidget;
    auto *layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(first);
    for (QWidget *control : more)
        layout->addWidget(control);
    layout->addStretch(1);
    return field;
}

void alignLabelToFirstRow(QLabel *label, QWidget *firstControl)
{
    new LabelAligner(label, firstControl);
}

LabelColumnSync::LabelColumnSync(QObject *parent)
    : QObject(parent)
{}

void LabelColumnSync::addForm(QFormLayout *form)
{
    m_forms.append(form);
    if (QWidget *owner = form->parentWidget())
        owner->installEventFilter(this);
}

void LabelColumnSync::sync()
{
    m_syncPending = false;
    QList<QWidget *> labels;
    int widest = 0;
    for (const QPointer<QFormLayout> &form : std::as_const(m_forms)) {
        if (!form)
            continue;
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
            if (QWidget *label = item ? item->widget() : nullptr) {
                labels.append(label);
                widest = std::max(widest, label->sizeHint().width());
            }
        }
    }
    for (QWidget *label : std::as_const(labels))
        label->setMinimumWidth(widest);
}

bool LabelColumnSync::eventFilter(QObject *watched, QEvent *event)
{
    if (isFontOrStyleChange(event) && !m_syncPending) {
        m_syncPending = true;
        QTimer::singleShot(0, this, [this] { sync(); });
    }
    return QObject::eventFilter(watched, event);
}

MoreOptions::MoreOptions(QFormLayout *form, const QString &text, QWidget *parent)
    : QObject(parent)
    , m_form(form)
    , m_button(new QToolButton(parent))
{
    m_button->setText(text);
    m_button->setAccessibleName(text);
    m_button->setCheckable(true);
    m_button->setAutoRaise(true);
    m_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_button->setArrowType(Qt::RightArrow);
    form->addRow(QString(), m_button);
    connect(m_button, &QToolButton::toggled, this, [this](bool open) { setOpen(open); });
}

void MoreOptions::addRow(int row)
{
    if (!m_form)
        return;
    QLayoutItem *item = m_form->itemAt(row, QFormLayout::FieldRole);
    if (!item)
        item = m_form->itemAt(row, QFormLayout::SpanningRole);
    QWidget *field = item ? item->widget() : nullptr;
    if (!field) {
        qWarning("MoreOptions: row %d has no field widget", row);
        return;
    }
    m_rows.append(field);
    m_form->setRowVisible(field, m_button->isChecked());
}

QToolButton *MoreOptions::button() const
{
    return m_button;
}

void MoreOptions::setOpen(bool open)
{
    m_button->setChecked(open);
    m_button->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    if (!m_form)
        return;
    for (const QPointer<QWidget> &field : std::as_const(m_rows)) {
        if (field)
            m_form->setRowVisible(field, open);
    }
}

QComboBox *makeApplyToCombo(QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    combo->addItem(tr("Name only"), QStringLiteral("stem"));
    combo->addItem(tr("Extension only"), QStringLiteral("extension"));
    combo->addItem(tr("Name and extension"), QStringLiteral("name"));
    return combo;
}

} // namespace ctrm::app
