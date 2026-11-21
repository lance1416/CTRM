// SPDX-License-Identifier: GPL-3.0-or-later
#include "formutils.h"

#include "tertiarybutton.h"
#include "theme/tokens.h"
#include "theme/typography.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QProxyStyle>
#include <QPushButton>
#include <QRadioButton>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionFocusRect>
#include <QStylePainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <utility>

namespace ctrm::app {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ctrm::app::FormUtils", text);
}

// The visible height of a check box or a radio button in a form: a group row pitch of 24 px
// with space-1 (ui-design 6.1 rule 15).
constexpr int kGroupControlH = tokens::controlH - tokens::space1;

// Set on the widget of makeInlineField, so that makeFieldGroup can take its widgets.
constexpr const char *kInlineFieldProperty = "ctrmInlineField";

bool isFontOrStyleChange(const QEvent *event)
{
    return event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange;
}

// The style margins of the layout item of w (the macOS focus ring space): the widget size less
// the size that a layout sees. Also for a hidden widget. w must have a parent: QWidgetItem
// sees a window as empty, so a window has no margins here.
QSize layoutItemMargins(QWidget *w)
{
    if (w->isWindow())
        return {};
    const QSizePolicy policy = w->sizePolicy();
    if (w->isHidden() && !policy.retainSizeWhenHidden()) {
        QSizePolicy keep = policy; // QWidgetItem gives 0 for a hidden widget
        keep.setRetainSizeWhenHidden(true);
        w->setSizePolicy(keep);
    }
    // As QWidgetItem::sizeHint, before it takes away the margins.
    const QSize raw = w->sizeHint()
                              .expandedTo(w->minimumSizeHint())
                              .boundedTo(w->maximumSize())
                              .expandedTo(w->minimumSize());
    const QSize item = QWidgetItem(w).sizeHint();
    if (w->sizePolicy() != policy)
        w->setSizePolicy(policy);
    return {policy.horizontalPolicy() == QSizePolicy::Ignored ? 0 : raw.width() - item.width(),
            raw.height() - item.height()};
}

bool isToggle(const QWidget *w)
{
    return qobject_cast<const QCheckBox *>(w) || qobject_cast<const QRadioButton *>(w);
}

// The layout item of a check box or a radio button. QWidgetItem adds the layout margins of the
// style around these widgets (macOS: 2 px left, approx. 3 px top and bottom), but Cocoa draws
// the box at the left edge of the widget and centres the box and the text in the widget height.
// So the widget gets the layout rect itself, with its own height, centred in the rect: the box
// is on the field edge (C04) and the box and the text have one centre (R5-02).
class ToggleItem : public QWidgetItem
{
public:
    // rowHeight: the height of the layout row; the widget is centred in it. 0: the widget height.
    // padBelow: the part of rowHeight under the widget area (R6-05): the widget is centred in
    // the rest.
    explicit ToggleItem(QWidget *w, int rowHeight = 0, int padBelow = 0)
        : QWidgetItem(w)
        , m_rowHeight(rowHeight)
        , m_padBelow(padBelow)
    {}

    int padBelow() const { return m_padBelow; }

    QSize sizeHint() const override
    {
        if (isEmpty())
            return {0, 0}; // as QWidgetItem
        const QWidget *w = widget();
        QSize hint = w->sizeHint()
                             .expandedTo(w->minimumSizeHint())
                             .boundedTo(w->maximumSize())
                             .expandedTo(w->minimumSize());
        hint.setHeight(std::max(hint.height(), m_rowHeight));
        return hint;
    }
    QSize minimumSize() const override
    {
        if (isEmpty())
            return {0, 0}; // as QWidgetItem
        const QWidget *w = widget();
        const QSizePolicy policy = w->sizePolicy();
        QSize min = w->minimumSizeHint();
        if (!(policy.horizontalPolicy() & QSizePolicy::ShrinkFlag))
            min.setWidth(sizeHint().width());
        if (policy.horizontalPolicy() == QSizePolicy::Ignored)
            min.setWidth(0);
        if (!(policy.verticalPolicy() & QSizePolicy::ShrinkFlag))
            min.setHeight(sizeHint().height());
        min = min.expandedTo(w->minimumSize()).boundedTo(w->maximumSize());
        min.setHeight(std::max(min.height(), m_rowHeight));
        return min;
    }
    QSize maximumSize() const override
    {
        if (isEmpty())
            return {0, 0}; // as QWidgetItem
        const QWidget *w = widget();
        const QSizePolicy policy = w->sizePolicy();
        QSize max = w->maximumSize();
        if (!(policy.horizontalPolicy() & QSizePolicy::GrowFlag) &&
            !(alignment() & Qt::AlignHorizontal_Mask))
            max.setWidth(std::min(max.width(), sizeHint().width()));
        if (!(policy.verticalPolicy() & QSizePolicy::GrowFlag) &&
            !(alignment() & Qt::AlignVertical_Mask))
            max.setHeight(std::min(max.height(), sizeHint().height()));
        if (alignment() & Qt::AlignHorizontal_Mask)
            max.setWidth(QLAYOUTSIZE_MAX);
        if (alignment() & Qt::AlignVertical_Mask)
            max.setHeight(QLAYOUTSIZE_MAX);
        max.setHeight(std::max(max.height(), m_rowHeight));
        return max;
    }
    bool hasHeightForWidth() const override { return !isEmpty() && widget()->hasHeightForWidth(); }
    int heightForWidth(int width) const override { return widget()->heightForWidth(width); }

    void setGeometry(const QRect &rect) override
    {
        if (isEmpty())
            return;
        const QRect r = rect.adjusted(0, 0, 0, -std::min(m_padBelow, rect.height()));
        QWidget *w = widget();
        const QSize hint = sizeHint();
        const Qt::Alignment aligned = QStyle::visualAlignment(w->layoutDirection(), alignment());
        int width = std::min(r.width(), w->maximumWidth());
        if (aligned & Qt::AlignHorizontal_Mask)
            width = std::min(width, hint.width());
        width = std::max(width, std::min(r.width(), w->minimumWidth()));
        int height = w->hasHeightForWidth() ? w->heightForWidth(width) : hint.height();
        if (!(aligned & Qt::AlignVertical_Mask) &&
            (w->sizePolicy().verticalPolicy() & QSizePolicy::GrowFlag))
            height = r.height();
        height = std::clamp(height, w->minimumHeight(),
                            std::max(w->minimumHeight(), std::min(r.height(), w->maximumHeight())));
        int x = r.x();
        if (aligned & Qt::AlignRight)
            x = r.right() + 1 - width;
        else if (aligned & Qt::AlignHCenter)
            x = r.x() + (r.width() - width) / 2;
        int y = r.y() + (r.height() - height) / 2; // centred, as a form row (6.1 rule 15)
        if (aligned & Qt::AlignTop)
            y = r.y();
        else if (aligned & Qt::AlignBottom)
            y = r.bottom() + 1 - height;
        w->setGeometry(x, y, width, height);
    }
    QRect geometry() const override { return widget()->geometry(); } // also hidden: QWidgetItem

private:
    int m_rowHeight;
    int m_padBelow;
};

} // namespace

QLayoutItem *makeToggleItem(QWidget *w)
{
    return new ToggleItem(w);
}

namespace {

// Puts a check box or a radio button of the form in a ToggleItem, once, in its row and role (a
// spanning row stays spanning).
void useToggleItem(QFormLayout *form, QWidget *w)
{
    const int index = form->indexOf(w);
    if (index < 0 || !isToggle(w) || dynamic_cast<ToggleItem *>(form->itemAt(index)))
        return;
    int row = 0;
    QFormLayout::ItemRole role = QFormLayout::FieldRole;
    form->getItemPosition(index, &row, &role);
    delete form->takeAt(index);
    form->setItem(row, role, new ToggleItem(w));
}

// The height that a layout sees for w.
int visibleHeight(QWidget *w)
{
    return w->sizeHint()
                   .expandedTo(w->minimumSizeHint())
                   .boundedTo(w->maximumSize())
                   .expandedTo(w->minimumSize())
                   .height() -
           layoutItemMargins(w).height();
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
        // A check box or a radio button of a group is centred in a control-h row (R5-04).
        const int height = isToggle(m_control)
                                   ? std::max(int(tokens::controlH),
                                              m_control->sizeHint()
                                                      .boundedTo(m_control->maximumSize())
                                                      .expandedTo(m_control->minimumSize())
                                                      .height())
                                   : visibleHeight(m_control);
        m_label->setMinimumHeight(height);
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

namespace {

// On macOS QFormLayout puts the field of the first row under the top of the form by the layout
// margin of the style (the focus ring space of a combo box): its visible top is then under the
// top of its label. This form puts the visible top of the first field on the top of the form,
// as the other rows (C03).
//
// QFormLayout keeps one set of row heights for height-for-width and the width it is for. A
// heightForWidth at the size hint width (QWidget::sizeHint asks it) writes the set but does not
// change that width. setGeometry at the old width then uses the rows of the size hint width: a
// wrapped note gets the height of more lines, or is cut. A query at another width first makes
// setGeometry calculate the rows for its own width. At the size hint width itself Qt uses the
// set as it is: then it is for a width 1 px larger.
class FormLayout : public QFormLayout
{
public:
    using QFormLayout::QFormLayout;

    void setGeometry(const QRect &rect) override
    {
        if (hasHeightForWidth()) {
            int other = rect.width() + 1;
            if (other == sizeHint().width())
                ++other;
            heightForWidth(other);
        }
        QFormLayout::setGeometry(rect);
#ifdef Q_OS_MACOS
        for (int row = 0; row < rowCount(); ++row) {
            if (!isRowVisible(row))
                continue;
            QLayoutItem *field = itemAt(row, FieldRole);
            if (!field)
                field = itemAt(row, SpanningRole);
            if (!field)
                return;
            QLayoutItem *label = itemAt(row, LabelRole);
            const int top = label && label->widget() && !label->widget()->isHidden()
                                    ? label->geometry().top()
                                    : contentsRect().top();
            const QRect g = field->geometry();
            if (QWidget *w = field->widget(); w && g.top() > top) // a group corrects itself
                w->move(w->x(), w->y() - (g.top() - top));
            return; // the first visible row only
        }
#endif
    }
};

} // namespace

QFormLayout *makeForm(QWidget *parent)
{
    auto *form = new FormLayout(parent);
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
        // The full height of QGridLayout (with the margins of the style), so that it does not
        // make a row lower than its controls.
        QGridLayout::setGeometry(r.adjusted(0, 0, 0, outerHeight()));
#ifdef Q_OS_MACOS
        // QGridLayout starts the first row under rect by the layout margin of the style. The
        // controls move up, so the first row starts at the top of rect (as the size hint). A
        // QWidgetItem keeps its widget in the parent, so move the widgets directly.
        const int visibleTop = rowCount() > 0 ? cellRect(0, 0).top() : INT_MAX;
        if (visibleTop != INT_MAX && visibleTop > rect.top()) {
            for (int i = 0; i < count(); ++i) {
                if (QWidget *w = itemAt(i)->widget())
                    w->move(w->x(), w->y() - (visibleTop - rect.top()));
            }
        }
#endif
    }

    // QGridLayout can give a maximum height below its minimum height when a style has layout
    // margins (macOS: a line edit or spin box next to a radio button). A form uses the maximum,
    // so the fields were squashed (M05). The minimum is the lower limit of the maximum.
    QSize maximumSize() const override
    {
        const QSize max = QGridLayout::maximumSize().expandedTo(QGridLayout::minimumSize());
        return {max.width(),
                max.height() >= QLAYOUTSIZE_MAX ? max.height() : max.height() - outerHeight()};
    }

    // On macOS QGridLayout also adds the layout margins of the controls in the first and the
    // last row above and under the grid. The visible top of the first control is then under
    // the label baseline, and the gaps to the other form rows are not space-2 (C03). The grid
    // here starts and ends on the visible controls.
    QSize sizeHint() const override { return QGridLayout::sizeHint() - QSize(0, outerHeight()); }
    QSize minimumSize() const override
    {
        return QGridLayout::minimumSize() - QSize(0, outerHeight());
    }

private:
    // The extra space that QGridLayout puts above and under the visible controls.
    std::pair<int, int> outerMargins() const
    {
        int top = 0;
        int bottom = 0;
        int firstRow = INT_MAX;
        int lastRow = -1;
        for (int i = 0; i < count(); ++i) {
            int row = 0;
            int column = 0;
            int rowSpan = 0;
            int columnSpan = 0;
            getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
            firstRow = std::min(firstRow, row);
            lastRow = std::max(lastRow, row + rowSpan - 1);
        }
        for (int i = 0; i < count(); ++i) {
            QLayoutItem *item = itemAt(i);
            QWidget *w = item->widget();
            if (!w)
                continue;
            int row = 0;
            int column = 0;
            int rowSpan = 0;
            int columnSpan = 0;
            getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
            const QRect visible = item->geometry();
            const QRect widget = w->geometry();
            if (row == firstRow)
                top = std::max(top, visible.top() - widget.top());
            if (row + rowSpan - 1 == lastRow)
                bottom = std::max(bottom, widget.bottom() - visible.bottom());
        }
#ifdef Q_OS_MACOS
        return {top, bottom};
#else
        return {0, 0}; // QGridLayout adds them on macOS only
#endif
    }
    int outerHeight() const
    {
        const auto [top, bottom] = outerMargins();
        return top + bottom;
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

    // A group with an inline field: all rows have space-1 under them (the last row: the form
    // gap), so the pitch is even and a field never touches a row (R6-05, R7-01, 6.1 rule 15).
    const bool hasInline =
            std::any_of(rows.cbegin(), rows.cend(), [](const auto &row) { return row.size() > 1; });
    auto *layout = new FieldGroupLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    for (int r = 0; r < rows.size(); ++r) {
        const QList<QWidget *> &row = rows.at(r);
        for (int c = 0; c < row.size(); ++c) {
            QWidget *w = row.at(c);
            const int span = row.size() == 1 ? columns + 1 : 1;
            const bool padded = hasInline && r + 1 < rows.size();
            if (isToggle(w)) {
                // One group row pitch of control-h (R5-04, R5-02). The form becomes the parent
                // with the grid: addItem does not take the widget.
                const int pad = padded ? int(tokens::space1) : 0;
                layout->addItem(new ToggleItem(w, tokens::controlH + pad, pad), r, c, 1, span);
            } else {
                layout->addWidget(w, r, c, 1, span);
            }
        }
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

WrappingCheckBox::WrappingCheckBox(const QString &text, QWidget *parent)
    : QCheckBox(text, parent)
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

QStyleOptionButton WrappingCheckBox::boxOption(int width) const
{
    QStyleOptionButton opt;
    initStyleOption(&opt);
    opt.rect = QRect(0, 0, width, formControlHeight(this)); // the native one-line height
    return opt;
}

QRect WrappingCheckBox::textRect(int width) const
{
    const QStyleOptionButton opt = boxOption(width);
    const QRect contents = style()->subElementRect(QStyle::SE_CheckBoxContents, &opt, this);
    // The first line is centred in the box row, as the native one-line label (R5-02).
    const int top = contents.top() + (contents.height() - fontMetrics().height()) / 2;
    if (isRightToLeft())
        return {0, top, contents.right() + 1, QWIDGETSIZE_MAX / 2};
    return {contents.left(), top, std::max(1, width - contents.left()), QWIDGETSIZE_MAX / 2};
}

int WrappingCheckBox::heightForWidth(int width) const
{
    const QRect r = textRect(width);
    const QRect text =
            fontMetrics().boundingRect(QRect(0, 0, r.width(), r.height()),
                                       Qt::TextWordWrap | Qt::TextShowMnemonic, this->text());
    return std::max(formControlHeight(this), r.top() + text.height());
}

bool WrappingCheckBox::hasHeightForWidth() const
{
    return true;
}

QSize WrappingCheckBox::sizeHint() const
{
    const int width = QCheckBox::sizeHint().width();
    return {width, heightForWidth(width)};
}

QSize WrappingCheckBox::minimumSizeHint() const
{
    // The box and a few characters: a long text never makes its column wider (ui-design 4.1,
    // 8.10). The box width is the part of a wide rect that is not text, in both directions.
    const QStyleOptionButton opt = boxOption(QWIDGETSIZE_MAX / 2);
    const int box = opt.rect.width() -
                    style()->subElementRect(QStyle::SE_CheckBoxContents, &opt, this).width();
    const int width = box + 8 * fontMetrics().averageCharWidth();
    return {width, formControlHeight(this)};
}

void WrappingCheckBox::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionButton opt = boxOption(width());
    opt.text.clear();
    painter.drawControl(QStyle::CE_CheckBox, opt);
    const int align = int(QStyle::visualAlignment(layoutDirection(), Qt::AlignLeft)) |
                      Qt::AlignTop | Qt::TextWordWrap | Qt::TextShowMnemonic;
    const QRect text = textRect(width());
    painter.drawItemText(text, align, palette(), isEnabled(), this->text(), QPalette::WindowText);
    if (opt.state & QStyle::State_HasFocus) {
        // The style framed only the box (the label was empty): frame the text, as a style that
        // frames the label of a one-line check box does. QMacStyle draws no frame here.
        QStyleOptionFocusRect focus;
        focus.initFrom(this);
        focus.rect = fontMetrics()
                             .boundingRect(text, align, this->text())
                             .adjusted(-1, 0, 1, 0)
                             .intersected(rect());
        painter.drawPrimitive(QStyle::PE_FrameFocusRect, focus);
    }
}

bool WrappingCheckBox::hitButton(const QPoint &pos) const
{
    return rect().contains(pos);
}

NoteLabel::NoteLabel(const QString &text, QWidget *parent)
    : WrappedField<QLabel>(text, parent)
{
    setWordWrap(true);
}

QString joinLastWords(const QString &text)
{
    const qsizetype last = text.trimmed().lastIndexOf(QLatin1Char(' '));
    if (last < 0)
        return text;
    const qsizetype at = text.indexOf(text.trimmed()) + last;
    QString joined = text;
    joined[at] = QChar(QChar::Nbsp);
    return joined;
}

int formControlHeight(const QWidget *w)
{
    if (qobject_cast<const QCheckBox *>(w) || qobject_cast<const QRadioButton *>(w))
        return kGroupControlH;
    if (qobject_cast<const QLineEdit *>(w) || qobject_cast<const QComboBox *>(w) ||
        qobject_cast<const QAbstractSpinBox *>(w) || qobject_cast<const QAbstractButton *>(w))
        return tokens::controlH;
    return 0;
}

namespace {

// Gives w the visible height of its control type. Returns that height, or 0.
int fixControlHeight(QWidget *w)
{
    const int height = formControlHeight(w);
    if (isToggle(w))
        w->setFixedHeight(height); // in a ToggleItem: no layout margins (R5-02)
    else if (height > 0)
        w->setFixedHeight(height + layoutItemMargins(w).height());
    return height;
}

// A text field or a combo box alone on its row fills the field column up to field-w-max. A field
// with a fixed width (a few characters) keeps it.
void fillFieldColumn(QWidget *w)
{
    // A hint wraps at the panel edge, not at the width of its size hint (R5-08).
    // Only the horizontal policy changes: a new QSizePolicy has no height-for-width, and
    // QFormLayout then gives the row only the size hint height (one line for a NoteLabel).
    if (auto *label = qobject_cast<QLabel *>(w); label && label->wordWrap()) {
        QSizePolicy policy = label->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Expanding);
        label->setSizePolicy(policy);
        return;
    }
    if (!qobject_cast<QLineEdit *>(w) && !qobject_cast<QComboBox *>(w))
        return;
    if (w->minimumWidth() > 0 && w->minimumWidth() == w->maximumWidth())
        return;
    w->setMinimumWidth(0);
    w->setMaximumWidth(tokens::fieldWMax + layoutItemMargins(w).width());
    w->setSizePolicy(QSizePolicy::Expanding, w->sizePolicy().verticalPolicy());
}

// The rows of a field group: each control gets its height, centred on its grid row. Returns the
// visible height of the first grid row.
int applyGroupRhythm(QGridLayout *grid)
{
    // One row pitch of control-h: a check box or a radio button is centred in a control-h row
    // (its ToggleItem); in a group with an inline field, a row with a row under it is
    // control-h + space-1 (R5-04, R6-05, R7-01, 6.1 rule 15).
    grid->setVerticalSpacing(0);
    grid->setHorizontalSpacing(tokens::space2);
    int firstRow = 0;
    for (int i = 0; i < grid->count(); ++i) {
        QWidget *w = grid->itemAt(i)->widget();
        if (!w)
            continue;
        int row = 0;
        int column = 0;
        int rowSpan = 0;
        int columnSpan = 0;
        grid->getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
        const int height = fixControlHeight(w);
        if (row == 0)
            firstRow = std::max(firstRow, height);
        // In a row with space-1 under it, the field is at the top, on its toggle (R6-05).
        const auto *toggle = dynamic_cast<const ToggleItem *>(grid->itemAtPosition(row, 0));
        const bool padded = toggle && toggle->padBelow() > 0 && !isToggle(w);
        grid->setAlignment(w, padded ? Qt::AlignTop : Qt::AlignVCenter);
    }
    return grid->count() > 0 ? std::max(firstRow, int(tokens::controlH)) : firstRow;
}

void applyRhythmNow(QFormLayout *form)
{
    form->setVerticalSpacing(tokens::space2);
    form->setHorizontalSpacing(tokens::space2);
    for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem *field = form->itemAt(row, QFormLayout::FieldRole);
        if (!field)
            field = form->itemAt(row, QFormLayout::SpanningRole);
        if (!field)
            continue;
        int height = 0;
        if (field->widget()) {
            useToggleItem(form, field->widget());
            field = form->itemAt(row, QFormLayout::FieldRole);
            if (!field)
                field = form->itemAt(row, QFormLayout::SpanningRole);
        }
        if (QWidget *w = field->widget()) {
            height = fixControlHeight(w);
            fillFieldColumn(w);
        } else if (auto *grid = qobject_cast<QGridLayout *>(field->layout())) {
            height = applyGroupRhythm(grid);
        }
        QLayoutItem *labelItem = form->itemAt(row, QFormLayout::LabelRole);
        auto *label = qobject_cast<QLabel *>(labelItem ? labelItem->widget() : nullptr);
        if (label && height > 0) {
            // The label text is centred on the first control: one baseline (6.2).
            label->setSizePolicy(label->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
            label->setFixedHeight(height);
            label->setAlignment((label->alignment() & Qt::AlignHorizontal_Mask) | Qt::AlignVCenter);
        }
    }
}

// Applies the rhythm again when the font or the style of the form changes.
class FormRhythm : public QObject
{
public:
    FormRhythm(QFormLayout *form, QWidget *owner)
        : QObject(owner)
        , m_form(form)
    {
        setObjectName(QStringLiteral("ctrmFormRhythm"));
        owner->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (isFontOrStyleChange(event) && !m_pending) {
            m_pending = true;
            // The controls get the change after the form widget.
            QTimer::singleShot(0, this, [this] {
                m_pending = false;
                if (m_form)
                    applyRhythmNow(m_form);
            });
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QFormLayout> m_form;
    bool m_pending = false;
};

} // namespace

void applyFormRhythm(QFormLayout *form)
{
    applyRhythmNow(form);
    QWidget *owner = form->parentWidget();
    if (!owner)
        return;
    for (QObject *child : owner->children()) {
        if (child->objectName() == QStringLiteral("ctrmFormRhythm"))
            return; // one for each form widget
    }
    new FormRhythm(form, owner);
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
