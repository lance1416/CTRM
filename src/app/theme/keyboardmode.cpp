// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme/keyboardmode.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QFocusEvent>
#include <QFocusFrame>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QShortcutEvent>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTextEdit>
#include <QTimer>
#include <QTreeView>

namespace ctrm::app {

namespace {

QPointer<KeyboardMode> g_instance;

// Repaints what shows the focus of widget: the widget, its viewport, its spin box or combo box,
// and the macOS focus frames of its window.
void repaintFocus(QWidget *widget)
{
    if (!widget)
        return;
    widget->update();
    if (auto *area = qobject_cast<QAbstractScrollArea *>(widget))
        area->viewport()->update();
    if (QWidget *parent = widget->parentWidget();
        qobject_cast<QAbstractSpinBox *>(parent) || qobject_cast<QComboBox *>(parent))
        parent->update();
    for (QFocusFrame *frame : widget->window()->findChildren<QFocusFrame *>())
        frame->update();
}

// The first and the last visible column of view in visual order.
std::pair<int, int> visibleColumnEnds(const QHeaderView *header)
{
    int first = -1;
    int last = -1;
    for (int visual = 0; visual < header->count(); ++visual) {
        if (header->isSectionHidden(header->logicalIndex(visual)))
            continue;
        if (first < 0)
            first = visual;
        last = visual;
    }
    return {first, last};
}

// A text edit that types a Tab character: the Tab key does not move the focus.
bool takesTab(const QObject *watched)
{
    if (const auto *edit = qobject_cast<const QPlainTextEdit *>(watched))
        return !edit->tabChangesFocus() && !edit->isReadOnly();
    if (const auto *edit = qobject_cast<const QTextEdit *>(watched))
        return !edit->tabChangesFocus() && !edit->isReadOnly();
    return false;
}

// A key that moves the focus in a button group.
bool isArrowKey(int key)
{
    return key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
}

} // namespace

KeyboardMode::KeyboardMode(QObject *parent)
    : QObject(parent)
{}

KeyboardMode *KeyboardMode::instance()
{
    if (!g_instance) {
        Q_ASSERT(qApp);
        g_instance = new KeyboardMode(qApp);
        qApp->installEventFilter(g_instance);
    }
    return g_instance;
}

bool KeyboardMode::isOn()
{
    return g_instance && g_instance->m_on;
}

void KeyboardMode::setOn(bool on)
{
    if (m_on == on)
        return;
    m_on = on;
    for (QWidget *window : QApplication::topLevelWidgets())
        window->setAttribute(Qt::WA_KeyboardFocusChange, on);
    repaintFocus(QApplication::focusWidget());
    emit changed(on);
}

bool KeyboardMode::isTextEntry(const QWidget *widget)
{
    if (qobject_cast<const QLineEdit *>(widget) || qobject_cast<const QAbstractSpinBox *>(widget))
        return true;
    const auto *combo = qobject_cast<const QComboBox *>(widget);
    return combo && combo->isEditable();
}

bool KeyboardMode::showsRing(const QWidget *widget)
{
    if (isTextEntry(widget))
        return true;
    if (qobject_cast<const QAbstractItemView *>(widget) ||
        (widget && qobject_cast<const QAbstractItemView *>(widget->parentWidget()) &&
         static_cast<const QAbstractItemView *>(widget->parentWidget())->viewport() == widget))
        return false;
    return isOn();
}

bool KeyboardMode::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress: {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if ((key == Qt::Key_Tab || key == Qt::Key_Backtab) && !takesTab(watched)) {
            setOn(true);
            startFocusKey();
        } else if (isArrowKey(key)) {
            startFocusKey();
        }
        break;
    }
    case QEvent::Shortcut:
        // A shortcut key: the action can move the focus (Find, Next Problem, Go to Rules).
        if (!static_cast<QShortcutEvent *>(event)->isAmbiguous())
            startFocusKey();
        break;
    case QEvent::FocusIn: {
        // Arrow keys in a button group and focus shortcuts move the focus with these reasons.
        // Qt uses the Tab reason also with no key: when the focus widget hides (R8-01), and
        // in the synthetic FocusIn of a QDialog that shows. So the reason counts only during a
        // focus key, and only for the real focus widget.
        const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
        if ((reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
             reason == Qt::ShortcutFocusReason) &&
            m_focusKey && watched == QApplication::focusWidget())
            setOn(true);
        // Qt clears the attribute of a window when it shows. Keep it the same as the mode.
        if (auto *widget = qobject_cast<QWidget *>(watched))
            widget->window()->setAttribute(Qt::WA_KeyboardFocusChange, m_on);
        break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::TouchBegin:
    case QEvent::TabletPress:
        m_focusKey = false;
        setOn(false);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void KeyboardMode::startFocusKey()
{
    m_focusKey = true;
    if (m_focusKeyEndQueued)
        return;
    // The key event and the focus changes it causes are done when the event loop runs again.
    m_focusKeyEndQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_focusKey = false;
        m_focusKeyEndQueued = false;
    });
}

void drawCurrentRowOutline(QPainter *painter, const QStyleOptionViewItem &option)
{
    if (!KeyboardMode::isOn() || !option.index.isValid())
        return;
    const auto *view = qobject_cast<const QAbstractItemView *>(option.widget);
    if (!view || !(view->hasFocus() || view->viewport()->hasFocus()))
        return;
    const QModelIndex current = view->currentIndex();
    if (!current.isValid() || current.row() != option.index.row() ||
        current.parent() != option.index.parent())
        return;
    // A row with a selected cell has no outline (the selection shows it).
    if ((option.state & QStyle::State_Selected) ||
        (view->selectionModel() &&
         view->selectionModel()->rowIntersectsSelection(current.row(), current.parent())))
        return;

    // The outline goes around the row: the cell gets its top and bottom edges, and the side
    // edges when it is the first or the last visible column.
    bool first = true;
    bool last = true;
    const QHeaderView *header = nullptr;
    if (const auto *table = qobject_cast<const QTableView *>(view))
        header = table->horizontalHeader();
    else if (const auto *tree = qobject_cast<const QTreeView *>(view))
        header = tree->header();
    if (header) {
        const int visual = header->visualIndex(option.index.column());
        const auto [firstVisual, lastVisual] = visibleColumnEnds(header);
        first = visual == firstVisual;
        last = visual == lastVisual;
    }
    const bool rtl = option.direction == Qt::RightToLeft;
    const QRect r = option.rect;
    const QColor colour = option.palette.color(QPalette::Accent);
    painter->fillRect(QRect(r.left(), r.top(), r.width(), 1), colour);
    painter->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), colour);
    if (rtl ? last : first)
        painter->fillRect(QRect(r.left(), r.top(), 1, r.height()), colour);
    if (rtl ? first : last)
        painter->fillRect(QRect(r.right(), r.top(), 1, r.height()), colour);
}

} // namespace ctrm::app
