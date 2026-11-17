// SPDX-License-Identifier: GPL-3.0-or-later
#include "formutils.h"

#include "tertiarybutton.h"
#include "theme/typography.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
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

// Set on the widget of makeInlineField, so that makeFieldGroup can take its widgets.
constexpr const char *kInlineFieldProperty = "ctrmInlineField";

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
    // The form starts at the left edge, also when no field grows (the macOS style centers it).
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    return form;
}

namespace {

// The grid of a field group. A style can draw a control inside its widget rect and give layout
// margins for that (macOS: radio and check boxes, combo boxes). QGridLayout keeps each widget
// inside the layout rect, so the visible edge of the controls in column 0 moves in by that
// margin. This grid starts that much before its rect: the visible edge is the field edge, as
// for a control that is the field itself (ui-design 6.1 rule 2).
class FieldGroupLayout : public QGridLayout
{
public:
    using QGridLayout::QGridLayout;

    void setGeometry(const QRect &rect) override
    {
        const bool rtl = parentWidget() && parentWidget()->isRightToLeft();
        int inset = 0;
        for (int i = 0; i < count(); ++i) {
            int row = 0;
            int column = 0;
            int rowSpan = 0;
            int columnSpan = 0;
            getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
            QLayoutItem *item = itemAt(i);
            QWidget *w = item->widget();
            if (column != 0 || !w || w->isHidden())
                continue;
            // QWidgetItem::geometry is the layout item rect: the widget rect less the margins.
            const QRect visible = item->geometry();
            const QRect widget = w->geometry();
            inset = std::max(inset, rtl ? widget.right() - visible.right()
                                        : visible.left() - widget.left());
        }
        QRect r = rect;
        if (rtl)
            r.setRight(r.right() + inset);
        else
            r.setLeft(r.left() - inset);
        QGridLayout::setGeometry(r);
    }

    // QGridLayout can give a maximum height below its minimum height when a style has layout
    // margins (macOS: a line edit or spin box next to a radio button). A form uses the maximum,
    // so the fields were squashed (M05). The minimum is the lower limit of the maximum.
    QSize maximumSize() const override
    {
        return QGridLayout::maximumSize().expandedTo(minimumSize());
    }
};

} // namespace

QLayout *makeFieldGroup(const QList<QWidget *> &controls)
{
    // A grid: a control alone spans all columns. The sub-fields of inline fields start in
    // column 1, so they have one start line (ui-design 6.1 rule 14). The widgets of an inline
    // field go into the grid itself: a nested widget would add the layout margins of the
    // style a second time.
    QList<QList<QWidget *>> rows;
    int columns = 1;
    for (QWidget *control : controls) {
        QList<QWidget *> row;
        if (control->property(kInlineFieldProperty).toBool()) {
            QLayout *inner = control->layout();
            for (int i = 0; i < inner->count(); ++i) {
                if (QWidget *w = inner->itemAt(i)->widget())
                    row.append(w);
            }
            for (QWidget *w : std::as_const(row)) {
                inner->removeWidget(w);
                w->setParent(nullptr); // the form becomes the parent; the inline field goes
            }
        } else {
            row.append(control);
        }
        columns = std::max(columns, int(row.size()));
        rows.append(row);
    }

    auto *layout = new FieldGroupLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    for (int r = 0; r < rows.size(); ++r) {
        const QList<QWidget *> &row = rows.at(r);
        if (row.size() == 1) {
            layout->addWidget(row.first(), r, 0, 1, columns + 1);
            continue;
        }
        for (int c = 0; c < row.size(); ++c)
            layout->addWidget(row.at(c), r, c);
    }
    layout->setColumnStretch(columns, 1);
    for (QWidget *control : controls) {
        if (control->property(kInlineFieldProperty).toBool())
            delete control; // its widgets are in the grid now
    }
    return layout;
}

QWidget *makeInlineField(QWidget *first, const QList<QWidget *> &more)
{
    auto *field = new QWidget;
    field->setProperty(kInlineFieldProperty, true);
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
                // The label is wider than its text: the text takes the horizontal alignment of
                // the label column (right on macOS, ui-design 6.1 rule 3).
                if (auto *text = qobject_cast<QLabel *>(label))
                    text->setAlignment((form->labelAlignment() & Qt::AlignHorizontal_Mask) |
                                       (text->alignment() & Qt::AlignVertical_Mask));
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
    , m_button(new TertiaryButton(parent))
{
    m_button->setObjectName(QStringLiteral("moreOptions"));
    m_button->setText(text);
    m_button->setAccessibleName(text);
    // A text button has the font of the form, not the small tool button font of macOS (M19).
    m_button->setFont(typeStyle(TypeToken::Body, parent->font()));
    parent->installEventFilter(this);
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

bool MoreOptions::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FontChange)
        m_button->setFont(typeStyle(TypeToken::Body, static_cast<QWidget *>(watched)->font()));
    return QObject::eventFilter(watched, event);
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
