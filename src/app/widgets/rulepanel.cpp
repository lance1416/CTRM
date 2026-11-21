// SPDX-License-Identifier: GPL-3.0-or-later
#include "rulepanel.h"

#include "actions.h"
#include "models/rulelistmodel.h"
#include "rules/chainoptionswidget.h"
#include "rules/cleanupeditor.h"
#include "rules/editorhelpers.h"
#include "rules/extensioneditor.h"
#include "rules/numberingeditor.h"
#include "rules/replaceeditor.h"
#include "rules/ruleeditor.h"
#include "rules/ruleeditorfactory.h"
#include "rules/ruleregistry.h"
#include "rules/rulesummary.h"
#include "rules/unsupportedrule.h"
#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/keyboardmode.h"
#include "theme/surface.h"
#include "theme/tokens.h"
#include "theme/typography.h"
#include "widgets/buttonstyle.h"
#include "widgets/formutils.h"
#include "widgets/plainsplitter.h"
#include "widgets/tertiarybutton.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QFocusEvent>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <functional>
#include <utility>

namespace ctrm::app {

namespace {

constexpr int kToolIconSize = tokens::iconS;     // the error icon in a rule row
constexpr int kEmptyTextChars = 40;              // the line length of an empty-state text (4.6)
constexpr int kRuleHandleWidth = tokens::space2; // the drag area between the list and the editor
constexpr int kSummarySpacing = 2;               // between the two lines of a rule row

constexpr int kListRows = 3;    // the list shows at least 3 rows when it can (ui-design 4.4)
constexpr int kLowListRows = 2; // a low panel: still 2 rows, so the list shows a chain (R6-06)

QFont summaryFont(const QFont &font)
{
    return typeStyle(TypeToken::Caption, font);
}

// Two lines for each rule (ui-design 4.4, R6-02): the check box centred on the row; the title
// "<position> <type>" with the error icon; under it, the summary in the caption style, dimmed.
// The row is row-h-2 high, so the space between two rows is larger than between two lines.
class RuleItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int lines = QFontMetrics(option.font).height() + kSummarySpacing +
                          QFontMetrics(summaryFont(option.font)).height();
        return {QStyledItemDelegate::sizeHint(option, index).width(),
                std::max(int(tokens::rowH2), lines + 2 * tokens::space1)};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        const bool on = index.data(Qt::CheckStateRole).value<Qt::CheckState>() == Qt::Checked;
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hasError = index.data(RuleListModel::HasErrorRole).toBool();
        const RuleRowLayout row = ruleRowLayout(opt, hasError);

        // The background of the full row.
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, widget);

        QStyleOptionViewItem indicator = opt;
        indicator.rect = QRect(QPoint(0, 0), row.check.size());
        indicator.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange);
        indicator.state |= on ? QStyle::State_On : QStyle::State_Off;
        painter->save();
        painter->translate(row.check.topLeft()); // as paintItemViewItem: QMacStyle needs it
        style->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &indicator, painter, widget);
        painter->restore();

        // The selection has the accent only in the active window. On the grey selection of
        // an inactive window, the text keeps its normal colours (ui-design 14: contrast).
        const bool accentSelection = selected && (opt.state & QStyle::State_Active);
        QColor title =
                opt.palette.color(accentSelection ? QPalette::HighlightedText : QPalette::Text);
        if (!on && !accentSelection) // a rule that is off is dimmed (ui-design 4.4)
            title = opt.palette.color(QPalette::PlaceholderText);
        const QColor summary = opt.palette.color(accentSelection ? QPalette::HighlightedText
                                                                 : QPalette::PlaceholderText);
        const int align = int(QStyle::visualAlignment(opt.direction, Qt::AlignLeft)) |
                          Qt::AlignVCenter | Qt::TextSingleLine;
        painter->save();
        painter->setFont(opt.font);
        painter->setPen(title);
        painter->drawText(
                row.title, align,
                QFontMetrics(opt.font).elidedText(opt.text, Qt::ElideRight, row.title.width()));
        const QFont font = summaryFont(opt.font);
        painter->setFont(font);
        painter->setPen(summary);
        painter->drawText(
                row.summary, align,
                QFontMetrics(font).elidedText(index.data(RuleListModel::SummaryRole).toString(),
                                              Qt::ElideRight, row.summary.width()));
        painter->restore();
        if (hasError)
            stateIcon(StateIcon::Error, CtrmColors::fromPalette(opt.palette))
                    .paint(painter, row.icon);
        // A list shows no focus ring; in keyboard mode the current row has an outline (O11).
        drawCurrentRowOutline(painter, opt);
    }
};

// The rule list: as high as its rows, at least kListRows (ui-design 4.4). The editor below it
// gets the free space. When the panel is low, it can shrink to kLowListRows (R6-06).
// A fade at the bottom edge of a scroll area viewport while its content is cut there
// (ui-design 4.4; M06, R8-03): from transparent to the colour under it. It takes no mouse events.
class BottomFade : public QWidget
{
public:
    BottomFade(QAbstractScrollArea *area, const QString &name, int height,
               QPalette::ColorRole colour)
        : QWidget(area)
        , m_area(area)
        , m_height(height)
        , m_colour(colour)
    {
        setObjectName(name);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        hide();
        QScrollBar *bar = area->verticalScrollBar();
        connect(bar, &QScrollBar::valueChanged, this, &BottomFade::place);
        connect(bar, &QScrollBar::rangeChanged, this, &BottomFade::place);
    }

    void setFadeHeight(int height)
    {
        m_height = height;
        place();
    }

    // Call it when the viewport changes its size.
    void place()
    {
        const QRect viewport = m_area->viewport()->geometry();
        setGeometry(viewport.left(), viewport.bottom() + 1 - m_height, viewport.width(), m_height);
        const QScrollBar *bar = m_area->verticalScrollBar();
        setVisible(bar->value() < bar->maximum());
        raise();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QColor end = palette().color(m_colour);
        QColor start = end;
        start.setAlpha(0);
        QLinearGradient gradient(0, 0, 0, height());
        gradient.setColorAt(0, start);
        gradient.setColorAt(1, end);
        painter.fillRect(rect(), gradient);
    }

private:
    QAbstractScrollArea *m_area;
    int m_height;
    QPalette::ColorRole m_colour;
};

class RuleListView : public QListView
{
public:
    RuleListView()
        : m_fade(new BottomFade(this, QStringLiteral("ruleListFade"), 0, QPalette::Base))
    {}

    QSize sizeHint() const override { return {QListView::sizeHint().width(), heightFor(rows())}; }
    // When the panel is low (the minimum window), the list can shrink to 2 rows: the current
    // rule stays visible, and the list still shows a chain (M06, R1-01, R6-06).
    QSize minimumSizeHint() const override
    {
        return {QListView::minimumSizeHint().width(), heightFor(kLowListRows)};
    }

protected:
    bool viewportEvent(QEvent *event) override
    {
        // Not all rules fit: the fade shows that the list continues (R8-03). Rows are never
        // cut (M06), so a part of the next row is not the hint. The fade covers half a row:
        // the summary line of the last row fades. Its bottom padding alone shows no fade.
        if (event->type() == QEvent::Resize)
            m_fade->setFadeHeight((heightFor(1) - 2 * frameWidth()) / 2);
        return QListView::viewportEvent(event);
    }

private:
    BottomFade *m_fade;

    int rowCount() const { return model() ? model()->rowCount() : 0; }
    int rows() const { return std::max(kListRows, rowCount()); }

    int heightFor(int rows) const
    {
        int row = model() && model()->rowCount() > 0 ? sizeHintForRow(0) : 0;
        if (row <= 0) {
            QStyleOptionViewItem option;
            initViewItemOption(&option);
            row = itemDelegate()->sizeHint(option, {}).height();
        }
        return rows * row + 2 * frameWidth();
    }
};

QString plainText(const QAction *action)
{
    QString text = action->text();
    text.remove(QLatin1Char('&'));
    return text;
}

bool isDeleteKey(const QKeyEvent *event)
{
    const auto mods = event->modifiers() & ~Qt::KeypadModifier;
    if (event->key() == Qt::Key_Delete && mods == Qt::NoModifier)
        return true;
#ifdef Q_OS_MACOS
    // Cmd+Backspace (ui-design 12). Qt gives Cmd as ControlModifier.
    if (event->key() == Qt::Key_Backspace && mods == Qt::ControlModifier)
        return true;
#endif
    return false;
}

// The empty-state column: the title, the text and Add Rule. On macOS, a box layout puts a push
// button lower by the top margin of its style; this layout keeps the visible gap from the text
// to the button at space-3 (design-tokens 4.6).
class EmptyStateLayout : public QVBoxLayout
{
public:
    EmptyStateLayout(QWidget *parent, QWidget *text, QWidget *button)
        : QVBoxLayout(parent)
        , m_text(text)
        , m_button(button)
    {}

    void setGeometry(const QRect &rect) override
    {
        QVBoxLayout::setGeometry(rect);
#ifdef Q_OS_MACOS
        const int index = indexOf(m_button);
        if (index < 0 || m_button->isHidden())
            return;
        const int wanted = m_text->geometry().bottom() + 1 + tokens::space3;
        const int visibleTop = itemAt(index)->geometry().top();
        if (visibleTop > wanted)
            m_button->move(m_button->x(), m_button->y() - (visibleTop - wanted));
#endif
    }

private:
    QWidget *m_text;
    QWidget *m_button;
};

// The editor area scrolls only up and down. Its minimum width is that of the widest editor, so
// the rule panel cannot be narrower than an editor (ui-design 4.4).
class EditorScrollArea : public QScrollArea
{
public:
    EditorScrollArea()
        : m_fade(new BottomFade(this, QStringLiteral("editorFade"), kFadeHeight, QPalette::Window))
    {
        setObjectName(QStringLiteral("editorScroll"));
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // No focus stop of its own: it shows no focus ring. Its fields take the focus and
        // ensureWidgetVisible scrolls to them (M09, ui-design 14).
        setFocusPolicy(Qt::NoFocus);
    }

    QSize minimumSizeHint() const override
    {
        QSize size = QScrollArea::minimumSizeHint();
        if (widget()) {
            // EditorStack gives the minimum width of its widest page.
            int width = widget()->minimumSizeHint().width() + 2 * frameWidth();
            if (!style()->styleHint(QStyle::SH_ScrollBar_Transient, nullptr, this))
                width += verticalScrollBar()->sizeHint().width();
            size.setWidth(std::max(size.width(), width));
        }
        return size;
    }

protected:
    bool viewportEvent(QEvent *event) override
    {
        // An editor changed its size (font, style, label column): the minimum width changes.
        if (event->type() == QEvent::LayoutRequest)
            updateGeometry();
        if (event->type() == QEvent::Resize)
            m_fade->place();
        return QScrollArea::viewportEvent(event);
    }

private:
    static constexpr int kFadeHeight = 16;
    BottomFade *m_fade;
};

// The editor pages. The width is that of the widest page, so the panel width does not change
// with the rule type (ui-design 4.4). The height is that of the current page: a short page does
// not scroll over the empty space of a tall page (M06).
class EditorStack : public QStackedWidget
{
public:
    EditorStack()
    {
        // QStackedLayout would set the minimum size of the largest page on this widget.
        layout()->setSizeConstraint(QLayout::SetNoConstraint);
        connect(this, &QStackedWidget::currentChanged, this, [this] { updateGeometry(); });
    }

    QSize sizeHint() const override
    {
        return pageSize([](const QWidget *w) { return w->sizeHint(); });
    }
    QSize minimumSizeHint() const override
    {
        return pageSize([](const QWidget *w) { return w->minimumSizeHint(); });
    }

    // The height for width of the current page. QStackedLayout gives the largest of all pages:
    // a short page would then have the empty space of a tall page under it (O13).
    bool hasHeightForWidth() const override
    {
        const QWidget *current = currentWidget();
        return current && current->hasHeightForWidth();
    }
    int heightForWidth(int width) const override
    {
        const QWidget *current = currentWidget();
        if (!current || !current->hasHeightForWidth())
            return -1;
        return std::max(current->heightForWidth(width), current->minimumSize().height());
    }

private:
    template <typename Hint>
    QSize pageSize(Hint hint) const
    {
        int width = 0;
        for (int i = 0; i < count(); ++i) {
            const QWidget *page = widget(i);
            width = std::max(width, hint(page).expandedTo(page->minimumSize()).width());
        }
        const QWidget *current = currentWidget();
        const int height = current ? hint(current).expandedTo(current->minimumSize()).height() : 0;
        return {width, height};
    }
};

// The layout item of the editor pages. QWidgetItem asks the layout of a widget for its height
// for width, and QStackedLayout gives the largest of all pages: this item asks EditorStack, which
// gives the current page.
class EditorStackItem : public QWidgetItem
{
public:
    using QWidgetItem::QWidgetItem;

    int heightForWidth(int width) const override
    {
        return isEmpty() ? -1 : widget()->heightForWidth(width);
    }
};

// The rule list over the editor (ui-design 4.4). The list is as high as its rows (at least 3)
// and the editor gets the rest. When the editor page does not fit, the list gives its empty rows
// to the editor first (M06). After the user moves the handle, the splitter keeps the sizes.
class RuleSplitter : public PlainSplitter
{
public:
    // editor is a scroll area with the pages and Chain options: QScrollArea keeps an old size
    // hint of its widget, so the splitter asks the pages.
    // underPages: the height of the closed Chain options under the page. Open Chain options do
    // not count: the editor scrolls, and the list keeps its height (C06, O13).
    RuleSplitter(QWidget *listPage, QListView *list, QScrollArea *editor, QStackedWidget *pages,
                 std::function<int()> underPages)
        : PlainSplitter(Qt::Vertical)
        , m_list(list)
        , m_editor(editor)
        , m_pages(pages)
        , m_underPages(std::move(underPages))
    {
        setChildrenCollapsible(false);
        setHandleWidth(kRuleHandleWidth);
        addWidget(listPage);
        addWidget(editor);
        setStretchFactor(0, 0);
        setStretchFactor(1, 1);
        connect(this, &QSplitter::splitterMoved, this, [this] {
            m_userSizes = true;
            m_list->scrollTo(m_list->currentIndex());
        });
        handle(1)->installEventFilter(this);
    }

protected:
    // A double-click on the handle turns the automatic list height on again (ui-design 4.4).
    bool eventFilter(QObject *watched, QEvent *e) override
    {
        if (watched == handle(1) && e->type() == QEvent::MouseButtonDblClick) {
            m_userSizes = false;
            fit();
            return true;
        }
        return PlainSplitter::eventFilter(watched, e);
    }

    bool event(QEvent *e) override
    {
        const bool result = PlainSplitter::event(e);
        if (e->type() == QEvent::LayoutRequest)
            fit(); // a hint changed: rows added or removed, another editor page
        return result;
    }

    void resizeEvent(QResizeEvent *e) override
    {
        PlainSplitter::resizeEvent(e);
        fit();
        if (m_userSizes) // the user sizes change in proportion: keep the current rule visible
            m_list->scrollTo(m_list->currentIndex());
    }

private:
    void fit()
    {
        if (m_userSizes || count() != 2)
            return;
        const int total = height() - handleWidth();
        const int planned = total;
        QWidget *list = widget(0);
        const int listMin = list->minimumSizeHint().height();
        const int listHint = std::max(listMin, list->sizeHint().height());
        // The tallest page, not the current one: the list height does not change when the
        // user selects another rule (R1-01).
        const int editorHint =
                tallestPage() + (m_underPages ? m_underPages() : 0) + 2 * m_editor->frameWidth();
        int listHeight = wholeRows(planned - editorHint, listMin, listHint, editorHint, planned);
        // The editor keeps its minimum: then the list gives up height, in whole rows.
        const int editorMin = m_editor->minimumSizeHint().height();
        const int row = m_list->sizeHintForRow(0);
        while (listHeight > listMin && total - listHeight < editorMin)
            listHeight = std::max(listMin, listHeight - std::max(1, row));
        const QList<int> wanted{listHeight, std::max(0, total - listHeight)};
        if (sizes() != wanted) {
            const int before = sizes().value(0);
            setSizes(wanted); // QSplitter can clamp the sizes: compare the result
            if (sizes().value(0) != before)
                m_list->scrollTo(m_list->currentIndex());
        }
    }

    // The list height for the space that the tallest page leaves: whole rows only, so no row
    // is cut. 3 rows stay when the page is then cut by about one field row or less; the fade
    // and the scroll show the rest (R2-02).
    int wholeRows(int space, int listMin, int listHint, int editorHint, int total) const
    {
        const int row = m_list->sizeHintForRow(0);
        if (row <= 0)
            return std::clamp(space, listMin, listHint);
        // The list page has the list and its tool buttons: the list part is whole rows.
        const int listOnly = m_list->sizeHint().height();
        const int overhead = listHint - listOnly;
        const int frames = listOnly - (listOnly - 2 * m_list->frameWidth()) / row * row;
        const int maxRows = (listOnly - frames) / row;
        const auto height = [&](int rows) {
            return std::max(listMin, overhead + frames + rows * row);
        };
        const int rows = std::clamp((space - overhead - frames) / row,
                                    std::min(kLowListRows, maxRows), maxRows);
        const int keep = std::min(kListRows, maxRows);
        if (rows < keep && editorHint - (total - height(keep)) <= kMaxCut)
            return height(keep);
        return height(rows);
    }

    static constexpr int kMaxCut = 40; // about one field row (ui-design 4.4)

    int tallestPage() const
    {
        QStackedWidget *pages = m_pages;
        int tallest = 0;
        for (int i = 0; i < pages->count(); ++i)
            tallest = std::max(tallest, pages->widget(i)
                                                ->sizeHint()
                                                .expandedTo(pages->widget(i)->minimumSize())
                                                .height()); // as EditorStack::pageSize
        return tallest;
    }

    QListView *m_list;
    QScrollArea *m_editor;
    QStackedWidget *m_pages;
    std::function<int()> m_underPages;
    bool m_userSizes = false;
};

} // namespace

RuleRowLayout ruleRowLayout(const QStyleOptionViewItem &option, bool hasError)
{
    const QWidget *widget = option.widget;
    const QStyle *style = widget ? widget->style() : QApplication::style();
    // Left to right in the row; mirrored at the end.
    QStyleOptionViewItem ltr = option;
    ltr.direction = Qt::LeftToRight;
    ltr.features |= QStyleOptionViewItem::HasCheckIndicator;
    const QRect row = option.rect;
    RuleRowLayout layout;
    layout.check = style->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &ltr, widget);
    layout.check.moveTop(row.top() + (row.height() - layout.check.height()) / 2);
    const int titleHeight = QFontMetrics(option.font).height();
    const int lines =
            titleHeight + kSummarySpacing + QFontMetrics(summaryFont(option.font)).height();
    const int top = row.top() + std::max(int(tokens::space1), (row.height() - lines) / 2);
    const int left = layout.check.right() + 1 + tokens::space2;
    const int right = row.right() - tokens::space2;
    int titleRight = right;
    if (hasError) {
        layout.icon = QRect(right + 1 - kToolIconSize, top + (titleHeight - kToolIconSize) / 2,
                            kToolIconSize, kToolIconSize);
        titleRight = layout.icon.left() - tokens::space1 - 1;
    }
    layout.title = QRect(left, top, std::max(0, titleRight + 1 - left), titleHeight);
    layout.summary = QRect(left, layout.title.bottom() + 1 + kSummarySpacing,
                           std::max(0, right + 1 - left), lines - titleHeight - kSummarySpacing);
    for (QRect *r : {&layout.check, &layout.title, &layout.summary, &layout.icon}) {
        if (r->isValid())
            *r = QStyle::visualRect(option.direction, row, *r);
    }
    return layout;
}

RulePanel::RulePanel(RuleListModel &model, const Actions &actions, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_actions(actions)
    , m_addRule(new QPushButton)
    , m_emptyPage(new QWidget)
    , m_emptyTitle(new QLabel)
    , m_emptyText(new QLabel)
    , m_list(new RuleListView)
    , m_editors(new EditorStack)
    , m_noRulePage(new QWidget)
    , m_unsupportedPage(new QWidget)
    , m_chainOptions(new ChainOptionsWidget(nullptr)) // the scroll content adds it: tab order

    , m_labelSync(new LabelColumnSync(this))
{
    m_addRule->setObjectName(QStringLiteral("addRuleButton"));
    m_emptyText->setObjectName(QStringLiteral("emptyText"));
    m_list->setObjectName(QStringLiteral("ruleList"));
    // R5-09: no frame; in high contrast a line around the list (design-tokens 2.1).
    setSurfaceEdges(m_list, Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge);

    // Header: "Rules", "top to bottom", Add Rule.
    auto *title = new QLabel(tr("Rules"));
    title->setFont(typeStyle(TypeToken::LabelStrong, font()));
    auto *hint = new QLabel(tr("top to bottom"));
    makeDimCaption(hint, this);
    auto *header = new QWidget;
    m_header = header;
    m_headerLayout = new QHBoxLayout(header);
    m_headerLayout->setContentsMargins(0, 0, 0, 0);
    // C08: a row of header-h, as the filter row over the table, so that the rule list and the
    // table start at one y.
    header->setFixedHeight(tokens::headerH);
    m_headerLayout->addWidget(title, 0, Qt::AlignBaseline);
    m_headerLayout->addWidget(hint, 0, Qt::AlignBaseline);
    m_headerLayout->addStretch();

    m_addRule->setText(tr("Add Rule"));
    m_addRule->setAutoDefault(false);
    m_addRule->setMenu(actions.addRuleMenu);
    m_addRule->setToolTip(actions.addRuleMenu->menuAction()->toolTip());

    // The empty rule list (ui-design 5.4, design-tokens 4.6): a title, a dim text and Add Rule,
    // left-aligned and top-aligned under the header (O02).
    m_emptyTitle->setObjectName(QStringLiteral("emptyTitle"));
    m_emptyTitle->setText(tr("No rules yet"));
    m_emptyText->setWordWrap(true);
    m_emptyText->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_emptyText->setTextFormat(Qt::PlainText);
    // S06: no Presets button here; the toolbar has it.
    auto *emptyLayout = new EmptyStateLayout(m_emptyPage, m_emptyText, m_addRule);
    // Add Rule goes in this layout itself: a nested layout would add the style margins of
    // the button a second time (macOS).
    m_emptyButtons = emptyLayout;
    // The empty-state title is space-3 under the header row (5.4).
    emptyLayout->setContentsMargins(0, tokens::space3 - tokens::space2, 0, 0);
    emptyLayout->setSpacing(0);
    emptyLayout->addWidget(m_emptyTitle);
    emptyLayout->addSpacing(tokens::space1);
    emptyLayout->addWidget(m_emptyText);
    emptyLayout->addSpacing(tokens::space3);
    emptyLayout->addStretch(); // Add Rule goes before it
    updateEmptyLook();

    // The rule list and its tool buttons.
    m_list->setModel(&m_model);
    for (const auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(&m_model, signal, m_list, [this] { m_list->updateGeometry(); });
    connect(&m_model, &QAbstractItemModel::modelReset, m_list,
            [this] { m_list->updateGeometry(); });
    m_list->setItemDelegate(new RuleItemDelegate(m_list));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setDropIndicatorShown(true);
    m_list->setUniformItemSizes(true);
    m_list->installEventFilter(this);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(tokens::space1); // C07: pitch hit-min + space-1
    const QList<std::pair<const char *, QAction *>> buttons{
            {"moveUpButton", actions.moveRuleUp},
            {"moveDownButton", actions.moveRuleDown},
            {"duplicateButton", actions.duplicateRule},
            {"deleteButton", actions.deleteRule}};
    for (const auto &[name, action] : buttons) {
        auto *button = new TertiaryButton;
        button->setObjectName(QString::fromLatin1(name));
        button->setAutoRaise(true);
        button->setIconButton(true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(tokens::iconS, tokens::iconS));
        button->setFixedSize(tokens::hitMin, tokens::hitMin); // C07
        button->setAccessibleName(plainText(action));
        QString tip = action->toolTip();
        if (action == actions.deleteRule) // Delete works in the rule list only (ui-design 12)
            tip = QStringLiteral("%1 (%2)").arg(
                    plainText(action),
                    QKeySequence(Qt::Key_Delete).toString(QKeySequence::NativeText));
        button->setToolTip(tip);
        button->setEnabled(action->isEnabled());
        connect(button, &QToolButton::clicked, action, &QAction::trigger);
        connect(action, &QAction::changed, button,
                [button, action] { button->setEnabled(action->isEnabled()); });
        buttonRow->addWidget(button);
        m_toolButtons.append({button, action});
    }
    buttonRow->addStretch();
    auto *listPage = new QWidget;
    auto *listLayout = new QVBoxLayout(listPage);
    // With the splitter handle, space-4 from the rule tools to the editor title (O06b).
    listLayout->setContentsMargins(0, 0, 0, tokens::space4 - kRuleHandleWidth);
    listLayout->addWidget(m_list);
    // The tool buttons keep their height (the macOS style gives a lower minimum), so the list
    // gets the rest of the page in whole rows (R2-02).
    auto *buttonBar = new QWidget;
    buttonBar->setLayout(buttonRow);
    buttonBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    listLayout->addWidget(buttonBar);

    // The editor pages, one for each rule type, in a scroll area.
    m_editors->addWidget(m_noRulePage);
    auto *unsupportedText = new QLabel(
            tr("This rule type is not supported by this CTRM version. Remove it or turn it off."));
    unsupportedText->setObjectName(QStringLiteral("unsupportedText"));
    unsupportedText->setWordWrap(true);
    auto *unsupportedLayout = new QVBoxLayout(m_unsupportedPage);
    unsupportedLayout->setContentsMargins(0, unsupportedLayout->contentsMargins().top(), 0,
                                          unsupportedLayout->contentsMargins().bottom());
    unsupportedLayout->addWidget(unsupportedText);
    unsupportedLayout->addStretch();
    m_editors->addWidget(m_unsupportedPage);
    for (const QString &type : ruleTypes()) {
        RuleEditor *editor = createRuleEditor(type, nullptr);
        if (!editor)
            continue;
        m_editors->addWidget(editor);
        m_editorByType.insert(type, editor);
        m_labelSync->addForm(editor->form());
        connect(editor, &RuleEditor::ruleEdited, this, [this, editor] { onEditorEdited(editor); });
    }
    m_labelSync->sync();
    if (auto *replace =
                qobject_cast<ReplaceEditor *>(m_editorByType.value(QStringLiteral("replace"))))
        connect(replace, &ReplaceEditor::syntaxHelpRequested, actions.regexHelp, &QAction::trigger);
    // The scroll content: the current page, Chain options directly under it, and the free height
    // at the bottom, with nothing in it (O13, ui-design 4.4, 8.10). Chain options is last: tab
    // order.
    m_editors->setObjectName(QStringLiteral("editorPages"));
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("editorContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    m_editors->setParent(content);
    contentLayout->addItem(new EditorStackItem(m_editors));
    contentLayout->addWidget(m_chainOptions);
    contentLayout->addStretch(1);
    m_scroll = new EditorScrollArea;
    m_scroll->setWidget(content);
    // O06b: no box. The editor is on the panel surface (design-tokens 4.6: a scroll area never
    // paints a background of its own). setWidget turns the fill of the widget on.
    content->setAutoFillBackground(false);
    m_scroll->setBackgroundRole(QPalette::Window);
    m_scroll->viewport()->setBackgroundRole(QPalette::Window);
    m_scroll->viewport()->setAutoFillBackground(false);

    m_splitter = new RuleSplitter(listPage, m_list, m_scroll, m_editors,
                                  [this] { return m_chainOptions->closedHeight(); });

    auto *layout = new QVBoxLayout(this);
    // The panel inset space-3 on each side; space-2 under the header row, as under the filter
    // row (O06b, C08, design-tokens 4.2).
    layout->setContentsMargins(tokens::space3, tokens::space3, tokens::space3, tokens::space3);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addSpacing(tokens::space2);
    layout->addWidget(m_emptyPage, 1);
    layout->addWidget(m_splitter, 1);

    fillAddRuleMenu();

    // Rule actions. The panel holds the window shortcuts of the actions that have one.
    addActions({actions.duplicateRule, actions.moveRuleUp, actions.moveRuleDown,
                actions.addRuleMenu->menuAction()});
    connect(actions.addRuleMenu->menuAction(), &QAction::triggered, this,
            &RulePanel::openAddRuleMenu);
    // The primary Add Rule has no menu of its own (see updateAddRuleLook).
    connect(m_addRule, &QPushButton::clicked, this, [this] {
        if (!m_addRule->menu())
            openAddRuleMenu();
    });
    connect(actions.duplicateRule, &QAction::triggered, this, [this] {
        const int row = currentRow();
        if (row < 0)
            return;
        m_model.duplicateRule(row);
        selectRow(row + 1);
    });
    connect(actions.deleteRule, &QAction::triggered, this, [this] {
        const int row = currentRow();
        if (row < 0)
            return;
        m_model.removeRule(row);
        selectRow(std::min(row, m_model.rowCount() - 1));
    });
    const auto move = [this](int by) {
        const int row = currentRow();
        const int to = row + by;
        if (row < 0 || to < 0 || to >= m_model.rowCount())
            return;
        m_model.moveRule(row, to);
        selectRow(to);
    };
    connect(actions.moveRuleUp, &QAction::triggered, this, [move] { move(-1); });
    connect(actions.moveRuleDown, &QAction::triggered, this, [move] { move(1); });
    connect(actions.toggleRule, &QAction::triggered, this, [this] {
        const int row = currentRow();
        if (row >= 0)
            m_model.setEnabled(row, !m_model.chain().at(row)->enabled);
    });

    // Chain options.
    m_chainOptions->setOptions(m_model.chain().options);
    applyChainOptions(m_model.chain().options);
    // Open: the editor scrolls so that the open area shows in full (ui-design 4.4).
    connect(m_chainOptions, &ChainOptionsWidget::openChanged, this, [this](bool open) {
        if (!open)
            return;
        QTimer::singleShot(0, this, [this] {
            // The new content height first, so that the scroll range has the open area.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            m_scroll->ensureWidgetVisible(m_chainOptions->lastOption());
        });
    });
    connect(m_chainOptions, &ChainOptionsWidget::optionsChanged, this,
            [this](const ChainOptions &o) {
                m_model.setChainOptions(o);
                applyChainOptions(o);
                emit chainOptionsChanged(o);
            });

    // The model. An editor must not hold a rule that the model deletes.
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current) {
                showRule(current.row());
                updateActions();
            });
    connect(&m_model, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex &, int first, int last) {
                for (int row = first; row <= last; ++row) {
                    if (m_shownRule && m_model.chain().at(row) == m_shownRule) {
                        m_ignoreEdits = true; // the rule goes away; its edits do not count
                        detachEditor();
                        m_ignoreEdits = false;
                    }
                }
            });
    connect(&m_model, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        m_ignoreEdits = true;
        detachEditor();
        m_ignoreEdits = false;
    });
    connect(&m_model, &QAbstractItemModel::modelReset, this, [this] {
        m_chainOptions->setOptions(m_model.chain().options);
        applyChainOptions(m_model.chain().options);
        updateEmptyState();
        selectRow(m_model.rowCount() > 0 ? 0 : -1);
        showRule(currentRow());
        updateActions();
    });
    // Positions change, so the editor title changes.
    const auto refresh = [this] {
        updateEmptyState();
        showRule(currentRow());
        updateActions();
    };
    connect(&m_model, &QAbstractItemModel::rowsInserted, this, refresh);
    connect(&m_model, &QAbstractItemModel::rowsRemoved, this, refresh);
    connect(&m_model, &QAbstractItemModel::rowsMoved, this, refresh);

    // Chain options and its wrapping labels never make the panel wider (O08).
    m_chainOptions->setSizePolicy(QSizePolicy::Ignored,
                                  m_chainOptions->sizePolicy().verticalPolicy());

    updateIcons();
    updateEmptyState();
    updateAddRuleLook();
    if (m_model.rowCount() > 0)
        selectRow(0);
    showRule(currentRow());
    updateActions();
    updateMinimumWidth();
}

QSize RulePanel::minimumSizeHint() const
{
    return {m_minWidth, QWidget::minimumSizeHint().height()};
}

bool RulePanel::event(QEvent *event)
{
    // The widths of the pages change with the font, the style and the scale only.
    if (event->type() == QEvent::Polish || event->type() == QEvent::DevicePixelRatioChange)
        scheduleMinimumWidth();
    return QWidget::event(event);
}

void RulePanel::scheduleMinimumWidth()
{
    if (m_minWidthPending)
        return;
    m_minWidthPending = true;
    // After the children have the new font or style.
    QTimer::singleShot(0, this, [this] {
        m_minWidthPending = false;
        m_labelSync->sync();
        updateMinimumWidth();
    });
}

void RulePanel::updateMinimumWidth()
{
    constexpr int kMinPanelWidth = 360; // ui-design 4.4
    // The widest page of all, as the scroll area gives it (with the scroll bar), and the header
    // with its Add Rule button.
    for (int i = 0; i < m_editors->count(); ++i)
        m_editors->widget(i)->ensurePolished();
    int content = m_scroll->minimumSizeHint().width();
    int header = m_header->minimumSizeHint().width();
    if (m_headerLayout->indexOf(m_addRule) < 0)
        header += m_headerLayout->spacing() + m_addRule->sizeHint().width();
    content = std::max(content, header);
    const QMargins margins = layout()->contentsMargins();
    const int width = std::max(kMinPanelWidth, content + margins.left() + margins.right());
    if (width != m_minWidth) {
        m_minWidth = width;
        updateGeometry();
    }
}

QListView *RulePanel::ruleList() const
{
    return m_list;
}

QWidget *RulePanel::currentEditor() const
{
    return m_editors->currentWidget();
}

QPushButton *RulePanel::addRuleButton() const
{
    return m_addRule;
}

void RulePanel::setPrimaryAddRule(bool primary)
{
    m_primary = primary;
    updateAddRuleLook();
    updateEmptyState();
}

void RulePanel::setNumberedCount(int n)
{
    m_model.setNumberedCount(n);
    if (auto *numbering =
                qobject_cast<NumberingEditor *>(m_editorByType.value(QStringLiteral("numbering"))))
        numbering->setNumberedCount(n);
}

bool RulePanel::eventFilter(QObject *watched, QEvent *event)
{
    // The rule list takes Space and Delete before window shortcuts such as Remove from List
    // (ui-design 12: in the rule list, Delete deletes the rule).
    if (watched == m_list && event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier) ||
            isDeleteKey(key)) {
            event->accept();
            return true;
        }
    }
    // M28: the menu shows the rule-list keys only while the list has the focus. A fixed
    // shortcut would be a native key equivalent on macOS that works for any focus widget.
    if (watched == m_list && event->type() == QEvent::FocusIn)
        setListShortcuts(true);
    // An open popup menu (not native) takes the focus for a short time: keep the keys.
    if (watched == m_list && event->type() == QEvent::FocusOut &&
        static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason)
        setListShortcuts(false);
    if (watched == m_list && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier) {
            m_actions.toggleRule->trigger();
            return true;
        }
        if (isDeleteKey(key)) {
            m_actions.deleteRule->trigger();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RulePanel::setListShortcuts(bool on)
{
    // The list handles the keys itself (eventFilter): the shortcuts only show in the menu.
    QList<QKeySequence> deleteKeys;
    if (on) {
        deleteKeys.append(QKeySequence(Qt::Key_Delete));
#ifdef Q_OS_MACOS
        deleteKeys.append(QKeySequence(Qt::CTRL | Qt::Key_Backspace));
#endif
    }
    m_actions.deleteRule->setShortcuts(deleteKeys);
    m_actions.toggleRule->setShortcut(on ? QKeySequence(Qt::Key_Space) : QKeySequence());
}

void RulePanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        updateIcons();
        updateAddRuleLook();
    }
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        scheduleMinimumWidth();
    if (event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange)
        updateEmptyLook();
}

void RulePanel::fillAddRuleMenu()
{
    // ui-design 8.1: three groups (text, format, numbers) with separators. No section titles:
    // macOS does not show them. No "...": an item adds the rule at once.
    const QList<QList<std::pair<const char *, QString>>> groups{
            {{"replace", tr("Replace text or a pattern")},
             {"insert", tr("Add text at a position")},
             {"remove", tr("Remove characters, a range or text between marks")},
             {"cleanup", tr("Fix spaces and invalid characters")}},
            {{"changeCase", tr("Change the letter case")},
             {"extension", tr("Change the extension")}},
            {{"numbering", tr("Add a number to each item")}}};
    QMenu *menu = m_actions.addRuleMenu;
    menu->setToolTipsVisible(true);
    for (const auto &group : groups) {
        if (!menu->isEmpty())
            menu->addSeparator();
        for (const auto &[name, tip] : group) {
            const QString type = QString::fromLatin1(name);
            auto *action = new QAction(ruleDisplayName(type), menu);
            action->setObjectName(QStringLiteral("addRule_") + type);
            action->setToolTip(tip);
            action->setStatusTip(tip);
            connect(action, &QAction::triggered, this, [this, type] { addRule(type); });
            menu->addAction(action);
        }
    }
}

void RulePanel::addRule(const QString &type)
{
    std::unique_ptr<Rule> rule = createRule(type);
    if (!rule)
        return;
    // Below the selected rule, or at the end (ui-design 8.1).
    const int current = currentRow();
    const int row =
            m_model.insertRule(current >= 0 ? current + 1 : m_model.rowCount(), std::move(rule));
    selectRow(row);
    if (auto *editor = qobject_cast<RuleEditor *>(currentEditor()))
        focusFirstField(editor);
}

void RulePanel::showRule(int row)
{
    if (row >= 0 && row < m_model.rowCount() && m_shownEditor &&
        m_model.chain().at(row) == m_shownRule) {
        // The same rule at a new position: update the title, and keep the editor enabled so
        // that the focus stays in it.
        m_shownEditor->setRule(m_shownRule, row + 1);
        return;
    }
    detachEditor();
    if (row < 0 || row >= m_model.rowCount()) {
        m_editors->setCurrentWidget(m_noRulePage);
        return;
    }
    Rule *rule = m_model.chain().at(row);
    RuleEditor *editor = m_editorByType.value(rule->type());
    if (!editor || dynamic_cast<UnsupportedRule *>(rule)) {
        m_editors->setCurrentWidget(m_unsupportedPage);
        return;
    }
    editor->setRule(rule, row + 1);
    m_shownEditor = editor;
    m_shownRule = rule;
    m_editors->setCurrentWidget(editor);
}

void RulePanel::detachEditor()
{
    RuleEditor *editor = m_shownEditor;
    if (!editor)
        return;
    editor->setRule(nullptr, 0); // sends a pending text edit of the old rule first
    m_shownEditor = nullptr;
    m_shownRule = nullptr;
}

void RulePanel::focusEditor(Qt::FocusReason reason)
{
    if (auto *editor = qobject_cast<RuleEditor *>(currentEditor())) {
        focusFirstField(editor, reason);
        return;
    }
    if (QWidget *page = currentEditor())
        page->setFocus(reason);
}

void RulePanel::focusFirstField(RuleEditor *editor, Qt::FocusReason reason)
{
    QFormLayout *form = editor->form();
    for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem *item = form->itemAt(row, QFormLayout::FieldRole);
        if (!item || !form->isRowVisible(row))
            continue;
        QList<QWidget *> candidates;
        if (QWidget *field = item->widget()) {
            candidates.append(field);
            candidates += field->findChildren<QWidget *>();
        } else if (QLayout *group = item->layout()) { // a field group (makeFieldGroup)
            for (int i = 0; i < group->count(); ++i) {
                if (QWidget *w = group->itemAt(i)->widget())
                    candidates.append(w);
            }
        }
        QWidget *target = nullptr;
        for (QWidget *w : std::as_const(candidates)) {
            auto *radio = qobject_cast<QRadioButton *>(w);
            if (radio && radio->isChecked()) { // a radio group gets focus on its checked radio
                target = radio;
                break;
            }
        }
        for (QWidget *w : std::as_const(candidates)) {
            if (target)
                break;
            if ((w->focusPolicy() & Qt::TabFocus) && w->isEnabled() && !w->isHidden())
                target = w;
        }
        if (target) {
            target->setFocus(reason);
            m_scroll->ensureWidgetVisible(target);
            return;
        }
    }
}

void RulePanel::onEditorEdited(RuleEditor *editor)
{
    if (m_ignoreEdits || editor != m_shownEditor || !m_shownRule)
        return;
    const RuleChain &chain = m_model.chain();
    for (int row = 0; row < chain.size(); ++row) {
        if (chain.at(row) == m_shownRule) {
            m_model.ruleChanged(row);
            return;
        }
    }
}

void RulePanel::applyChainOptions(const ChainOptions &o)
{
    if (auto *cleanUp =
                qobject_cast<CleanUpEditor *>(m_editorByType.value(QStringLiteral("cleanup"))))
        cleanUp->setPortableNames(o.portableNames);
    if (auto *extension =
                qobject_cast<ExtensionEditor *>(m_editorByType.value(QStringLiteral("extension"))))
        extension->setFolderExtensions(o.folderExtensions);
}

void RulePanel::updateActions()
{
    const int row = currentRow();
    const bool has = row >= 0;
    m_actions.duplicateRule->setEnabled(has);
    m_actions.deleteRule->setEnabled(has);
    m_actions.toggleRule->setEnabled(has);
    m_actions.moveRuleUp->setEnabled(has && row > 0);
    m_actions.moveRuleDown->setEnabled(has && row < m_model.rowCount() - 1);
}

void RulePanel::updateEmptyState()
{
    const bool empty = m_model.rowCount() == 0;
    m_emptyText->setText(
            m_primary ? tr("Add a rule to change the names. The preview shows the result at once.")
                      : tr("Rules come next. Add files first, then add a rule to change their "
                           "names."));
    // One Add Rule button: in the header, or under the empty-state text (O02).
    QBoxLayout *target = empty ? m_emptyButtons : m_headerLayout;
    if (m_addRule->parentWidget() == nullptr || target->indexOf(m_addRule) < 0) {
        m_headerLayout->removeWidget(m_addRule);
        m_emptyButtons->removeWidget(m_addRule);
        if (empty)
            m_emptyButtons->insertWidget(m_emptyButtons->count() - 1, m_addRule, 0,
                                         Qt::AlignLeft | Qt::AlignTop);
        else
            m_headerLayout->addWidget(m_addRule);
        m_addRule->show();
    }
    m_emptyPage->setVisible(empty);
    m_splitter->setVisible(!empty);
    m_chainOptions->setVisible(!empty);
}

void RulePanel::updateEmptyLook()
{
    m_emptyTitle->setFont(typeStyle(TypeToken::LabelStrong, font()));
    QPalette dim = m_emptyText->palette();
    dim.setColor(QPalette::WindowText, palette().color(QPalette::PlaceholderText)); // text-dim
    m_emptyText->setPalette(dim);
    m_emptyText->setMaximumWidth(kEmptyTextChars * m_emptyText->fontMetrics().averageCharWidth());
}

void RulePanel::updateAddRuleLook()
{
    m_addRule->setProperty("primary", m_primary);
    // The macOS style draws a pull-down button with no accent. The primary Add Rule is a plain
    // default button that opens the menu on click (ui-design 5.1, 5.4).
    m_addRule->setMenu(m_primary ? nullptr : m_actions.addRuleMenu);
    setPrimary(m_addRule, m_primary);
}

void RulePanel::openAddRuleMenu()
{
    if (m_addRule->menu()) {
        m_addRule->showMenu();
        return;
    }
    // Below the button, on the same start edge (as QPushButton::showMenu).
    QMenu *menu = m_actions.addRuleMenu;
    QPoint at = m_addRule->rect().bottomLeft();
    if (m_addRule->isRightToLeft())
        at = m_addRule->rect().bottomRight() - QPoint(menu->sizeHint().width() - 1, 0);
    menu->popup(m_addRule->mapToGlobal(at));
}

void RulePanel::updateIcons()
{
    const QPalette p = palette();
    m_addRule->setIcon(toolIcon(ToolIcon::Add, p));
    const QHash<QAction *, ToolIcon> icons{{m_actions.moveRuleUp, ToolIcon::MoveUp},
                                           {m_actions.moveRuleDown, ToolIcon::MoveDown},
                                           {m_actions.duplicateRule, ToolIcon::Duplicate},
                                           {m_actions.deleteRule, ToolIcon::Delete}};
    for (const auto &[button, action] : std::as_const(m_toolButtons))
        button->setIcon(toolIcon(icons.value(action), p));
}

int RulePanel::currentRow() const
{
    const QModelIndex current = m_list->currentIndex();
    return current.isValid() ? current.row() : -1;
}

void RulePanel::selectRow(int row)
{
    if (row >= 0 && row < m_model.rowCount())
        m_list->setCurrentIndex(m_model.index(row));
    else
        m_list->setCurrentIndex({});
}

} // namespace ctrm::app
