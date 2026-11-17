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
#include "theme/typography.h"
#include "widgets/buttonstyle.h"
#include "widgets/formutils.h"
#include "widgets/itemviewpaint.h"
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
#include <QStyledItemDelegate>
#include <QToolButton>

#include <algorithm>

namespace ctrm::app {

namespace {

constexpr int kToolIconSize = 16;  // ui-design 5.1: tertiary icons
constexpr int kMinClickSize = 24;  // ui-design 5.1: the click area of a tertiary button
constexpr int kSummarySpacing = 2; // between the two lines of a rule row

// Two lines for each rule: the check box, "<position> <type>" and the error icon; under them,
// the summary in the caption style, dimmed (ui-design 4.4).
class RuleItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(size.height() + kSummarySpacing +
                       QFontMetrics(summaryFont(option)).height());
        return size;
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

        // The background of the full row, then the first line on it.
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, widget);
        QStyleOptionViewItem first = opt;
        const int firstHeight = QStyledItemDelegate::sizeHint(option, index).height();
        first.rect.setHeight(firstHeight);
        first.backgroundBrush = Qt::NoBrush;
        first.palette.setBrush(QPalette::Highlight, Qt::transparent);
        if (index.data(RuleListModel::HasErrorRole).toBool()) {
            first.features |= QStyleOptionViewItem::HasDecoration;
            first.icon = stateIcon(StateIcon::Error, CtrmColors::fromPalette(opt.palette));
            first.decorationSize = QSize(kToolIconSize, kToolIconSize);
        }
        // The selection has the accent only in the active window. On the grey selection of
        // an inactive window, the text keeps its normal colours (ui-design 14: contrast).
        const bool accentSelection = selected && (opt.state & QStyle::State_Active);
        if (selected && !accentSelection)
            first.palette.setColor(QPalette::HighlightedText, opt.palette.color(QPalette::Text));
        if (!on && !accentSelection) // a rule that is off is dimmed (ui-design 4.4)
            first.palette.setColor(selected ? QPalette::HighlightedText : QPalette::Text,
                                   opt.palette.color(QPalette::PlaceholderText));
        paintItemViewItem(painter, first);

        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &first, widget);
        QRect summaryRect(textRect.left(), first.rect.bottom() + 1 + kSummarySpacing,
                          opt.rect.right() - textRect.left(),
                          opt.rect.bottom() - first.rect.bottom() - kSummarySpacing);
        const QFont font = summaryFont(option);
        painter->save();
        painter->setFont(font);
        painter->setPen(opt.palette.color(accentSelection ? QPalette::HighlightedText
                                                          : QPalette::PlaceholderText));
        const QString summary =
                QFontMetrics(font).elidedText(index.data(RuleListModel::SummaryRole).toString(),
                                              Qt::ElideRight, summaryRect.width());
        painter->drawText(summaryRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, summary);
        painter->restore();
    }

private:
    static QFont summaryFont(const QStyleOptionViewItem &option)
    {
        return typeStyle(TypeToken::Caption, option.font);
    }
};

// The rule list: as high as its rows, at least kMinListRows (ui-design 4.4). The editor
// below it gets the free space.
class RuleListView : public QListView
{
public:
    using QListView::QListView;

    QSize sizeHint() const override { return {QListView::sizeHint().width(), heightFor(rows())}; }
    // When the panel is low (the minimum window), the list can shrink to its rules: it keeps
    // no empty rows that the editor needs (M06).
    QSize minimumSizeHint() const override
    {
        return {QListView::minimumSizeHint().width(),
                heightFor(std::clamp(rowCount(), 1, kMinListRows))};
    }

private:
    static constexpr int kMinListRows = 3;

    int rowCount() const { return model() ? model()->rowCount() : 0; }
    int rows() const { return std::max(kMinListRows, rowCount()); }

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

// The editor area scrolls only up and down. Its minimum width is that of the widest editor, so
// the rule panel cannot be narrower than an editor (ui-design 4.4).
class EditorScrollArea : public QScrollArea
{
public:
    EditorScrollArea()
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
        return QScrollArea::viewportEvent(event);
    }
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

// The rule list over the editor (ui-design 4.4). The list is as high as its rows (at least 3)
// and the editor gets the rest. When the editor page does not fit, the list gives its empty rows
// to the editor first (M06). After the user moves the handle, the splitter keeps the sizes.
class RuleSplitter : public PlainSplitter
{
public:
    // editor is a scroll area with pages: QScrollArea keeps an old size hint of its widget, so
    // the splitter asks the pages.
    RuleSplitter(QWidget *list, QScrollArea *editor)
        : PlainSplitter(Qt::Vertical)
        , m_editor(editor)
    {
        setChildrenCollapsible(false);
        addWidget(list);
        addWidget(editor);
        setStretchFactor(0, 0);
        setStretchFactor(1, 1);
        connect(this, &QSplitter::splitterMoved, this, [this] { m_userSizes = true; });
    }

protected:
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
    }

private:
    void fit()
    {
        if (m_userSizes || count() != 2)
            return;
        const int total = height() - handleWidth();
        QWidget *list = widget(0);
        const int listMin = list->minimumSizeHint().height();
        const int listHint = std::max(listMin, list->sizeHint().height());
        const int editorHint = m_editor->widget()->sizeHint().height() + 2 * m_editor->frameWidth();
        const int listHeight = std::clamp(total - editorHint, listMin, listHint);
        const QList<int> wanted{listHeight, std::max(0, total - listHeight)};
        if (sizes() != wanted)
            setSizes(wanted);
    }

    QScrollArea *m_editor;
    bool m_userSizes = false;
};

} // namespace

RulePanel::RulePanel(RuleListModel &model, const Actions &actions, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_actions(actions)
    , m_addRule(new QPushButton)
    , m_emptyPage(new QWidget)
    , m_emptyText(new QLabel)
    , m_list(new RuleListView)
    , m_editors(new EditorStack)
    , m_noRulePage(new QWidget)
    , m_unsupportedPage(new QWidget)
    , m_chainOptions(new ChainOptionsWidget(nullptr)) // the layout adds it last: tab order

    , m_labelSync(new LabelColumnSync(this))
{
    m_addRule->setObjectName(QStringLiteral("addRuleButton"));
    m_emptyText->setObjectName(QStringLiteral("emptyText"));
    m_list->setObjectName(QStringLiteral("ruleList"));

    // Header: "Rules", "top to bottom", Add Rule.
    auto *title = new QLabel(tr("Rules"));
    title->setFont(typeStyle(TypeToken::LabelStrong, font()));
    auto *hint = new QLabel(tr("top to bottom"));
    makeDimCaption(hint, this);
    auto *header = new QWidget;
    m_headerLayout = new QHBoxLayout(header);
    m_headerLayout->setContentsMargins(0, 0, 0, 0);
    m_headerLayout->addWidget(title, 0, Qt::AlignBaseline);
    m_headerLayout->addWidget(hint, 0, Qt::AlignBaseline);
    m_headerLayout->addStretch();

    m_addRule->setText(tr("Add Rule"));
    m_addRule->setAutoDefault(false);
    m_addRule->setMenu(actions.addRuleMenu);
    m_addRule->setToolTip(actions.addRuleMenu->menuAction()->toolTip());

    // The empty rule list (ui-design 5.4).
    m_emptyText->setWordWrap(true);
    m_emptyText->setAlignment(Qt::AlignHCenter);
    // S06: no Presets button here; the toolbar has it.
    m_emptyButtons = new QVBoxLayout;
    auto *emptyLayout = new QVBoxLayout(m_emptyPage);
    emptyLayout->addStretch();
    emptyLayout->addWidget(m_emptyText);
    emptyLayout->addLayout(m_emptyButtons);
    emptyLayout->addStretch(2);

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
    const QList<std::pair<const char *, QAction *>> buttons{
            {"moveUpButton", actions.moveRuleUp},
            {"moveDownButton", actions.moveRuleDown},
            {"duplicateButton", actions.duplicateRule},
            {"deleteButton", actions.deleteRule}};
    for (const auto &[name, action] : buttons) {
        auto *button = new TertiaryButton;
        button->setObjectName(QString::fromLatin1(name));
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(kToolIconSize, kToolIconSize));
        button->setMinimumSize(kMinClickSize, kMinClickSize);
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
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->addWidget(m_list);
    listLayout->addLayout(buttonRow);

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
    m_labelSync->addForm(m_chainOptions->form());
    m_labelSync->sync();
    if (auto *replace =
                qobject_cast<ReplaceEditor *>(m_editorByType.value(QStringLiteral("replace"))))
        connect(replace, &ReplaceEditor::syntaxHelpRequested, actions.regexHelp, &QAction::trigger);
    m_scroll = new EditorScrollArea;
    m_scroll->setWidget(m_editors);

    m_splitter = new RuleSplitter(listPage, m_scroll);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(header);
    layout->addWidget(m_emptyPage, 1);
    layout->addWidget(m_splitter, 1);
    layout->addWidget(m_chainOptions); // under the editor, outside the scroll area (ui-design 8.10)

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

    updateIcons();
    updateEmptyState();
    updateAddRuleLook();
    if (m_model.rowCount() > 0)
        selectRow(0);
    showRule(currentRow());
    updateActions();
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
}

void RulePanel::fillAddRuleMenu()
{
    // ui-design 8.1: the sections and their order.
    const QList<std::pair<QString, QStringList>> sections{
            {tr("Text"), {"replace", "insert", "remove", "cleanup"}},
            {tr("Format"), {"changeCase", "extension"}},
            {tr("Numbers"), {"numbering"}}};
    QMenu *menu = m_actions.addRuleMenu;
    for (const auto &[section, types] : sections) {
        menu->addSection(section);
        for (const QString &type : types) {
            auto *action = new QAction(tr("%1...").arg(ruleDisplayName(type)), menu);
            action->setObjectName(QStringLiteral("addRule_") + type);
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

void RulePanel::focusFirstField(RuleEditor *editor)
{
    QFormLayout *form = editor->form();
    for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem *item = form->itemAt(row, QFormLayout::FieldRole);
        QWidget *field = item ? item->widget() : nullptr;
        if (!field || !form->isRowVisible(row))
            continue;
        QList<QWidget *> candidates{field};
        candidates += field->findChildren<QWidget *>();
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
            target->setFocus(Qt::OtherFocusReason);
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
    // One Add Rule button: in the header, or in the centre of the empty list.
    QBoxLayout *target = empty ? m_emptyButtons : m_headerLayout;
    if (m_addRule->parentWidget() == nullptr || target->indexOf(m_addRule) < 0) {
        m_headerLayout->removeWidget(m_addRule);
        m_emptyButtons->removeWidget(m_addRule);
        if (empty)
            m_emptyButtons->insertWidget(0, m_addRule, 0, Qt::AlignHCenter);
        else
            m_headerLayout->addWidget(m_addRule);
        m_addRule->show();
    }
    m_emptyPage->setVisible(empty);
    m_splitter->setVisible(!empty);
    m_chainOptions->setVisible(!empty);
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
