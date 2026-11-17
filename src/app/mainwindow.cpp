// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include "appcontext.h"
#include "controllers/itemadder.h"
#include "fs/itemsource.h"
#include "text/plural.h"
#include "theme/icons.h"
#include "widgets/actionbar.h"
#include "widgets/emptystate.h"
#include "widgets/flowstrip.h"
#include "widgets/itemtableview.h"
#include "widgets/menutoolbutton.h"
#include "widgets/messagebar.h"
#include "widgets/rulepanel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMetaMethod>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace ctrm::app {

namespace {

constexpr int kMinimumWidth = 900;
constexpr int kMinimumHeight = 560;
constexpr int kDefaultWidth = 1200;
constexpr int kDefaultHeight = 760;
// The rule panel and the table have the ratio 1 : 2 (ui-design 4.1).
constexpr int kRuleStretch = 1;
constexpr int kTableStretch = 2;
const QString kMainSplitterKey = QStringLiteral("main");

QWidget *makeSlot(const char *objectName, QWidget *parent)
{
    auto *slot = new QWidget(parent);
    slot->setObjectName(QLatin1StringView(objectName));
    auto *layout = new QVBoxLayout(slot);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    return slot;
}

// The first widget in the focus chain of parent that takes focus with Tab.
QWidget *firstFocusChild(QWidget *parent, QWidget *window)
{
    for (QWidget *w = parent->nextInFocusChain(); w && parent->isAncestorOf(w);
         w = w->nextInFocusChain()) {
        if ((w->focusPolicy() & Qt::TabFocus) && w->isEnabled() && w->isVisibleTo(window))
            return w;
    }
    return nullptr;
}

void focus(QWidget *w)
{
    if (w)
        w->setFocus(Qt::ShortcutFocusReason);
}

} // namespace

MainWindow::MainWindow(AppContext &ctx, QWidget *parent)
    : QMainWindow(parent)
    , m_ctx(ctx)
    , m_actions(createActions(this))
    , m_folderCache(ctx.fileSystem())
    , m_itemAdder(new ItemAdder(ctx.fileSystem(), m_itemModel, ctx.settings(), this))
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("CTRM"));
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    resize(kDefaultWidth, kDefaultHeight);
#ifdef Q_OS_MACOS
    setUnifiedTitleAndToolBarOnMac(true);
#endif
    // Actions are window-wide, also when the menu bar is native.
    addActions(findChildren<QAction *>(Qt::FindDirectChildrenOnly));
    addAction(m_actions.addRuleMenu->menuAction());

    buildMenus();
    buildToolBar();
    updateToolIcons();
    buildCentralWidget();
    buildPanels();
    // M21: the action bar is the last row. The status bar shows only with a message.
    statusBar()->hide();
    connect(statusBar(), &QStatusBar::messageChanged, this,
            [this](const QString &text) { statusBar()->setVisible(!text.isEmpty()); });
    // M04: View > Show Columns has the column items of the table header.
    m_showColumnsMenu->addActions(
            m_table->findChild<QMenu *>(QStringLiteral("columnsMenu"))->actions());

    connect(m_actions.closeWindow, &QAction::triggered, this, &QWidget::close);
    connect(m_actions.quit, &QAction::triggered, qApp, &QApplication::closeAllWindows,
            Qt::QueuedConnection);
    connect(m_actions.showToolbar, &QAction::toggled, m_toolBar, &QWidget::setVisible);

    Settings &settings = m_ctx.settings();
    m_actions.showSteps->setChecked(settings.showSteps());
    m_actions.highlightChanged->setChecked(settings.highlightChanged());
    connect(m_actions.showSteps, &QAction::toggled, this,
            [&settings](bool on) { settings.setShowSteps(on); });
    connect(m_actions.highlightChanged, &QAction::toggled, this,
            [&settings](bool on) { settings.setHighlightChanged(on); });

    connectActions();
    updateListActions();
    restoreLayout();
    m_flowStripSlot->setVisible(m_actions.showSteps->isChecked());
    m_table->setHighlightChanged(m_actions.highlightChanged->isChecked());
    m_itemModel.setPortableNames(m_ruleModel.chain().options.portableNames);
    requestPreview();
}

MainWindow::~MainWindow()
{
    // The panels use the models, which are members: delete the panels first.
    delete takeCentralWidget();
}

const Actions &MainWindow::actions() const
{
    return m_actions;
}

QWidget *MainWindow::flowStripSlot() const
{
    return m_flowStripSlot;
}

QWidget *MainWindow::rulePanelSlot() const
{
    return m_rulePanelSlot;
}

QWidget *MainWindow::tableSlot() const
{
    return m_tableSlot;
}

QWidget *MainWindow::actionBarSlot() const
{
    return m_actionBarSlot;
}

QWidget *MainWindow::messageBarSlot() const
{
    return m_messageBarSlot;
}

QSplitter *MainWindow::mainSplitter() const
{
    return m_mainSplitter;
}

ItemTableModel &MainWindow::itemModel()
{
    return m_itemModel;
}

RuleListModel &MainWindow::ruleModel()
{
    return m_ruleModel;
}

ItemTableView *MainWindow::tableView()
{
    return m_table;
}

RulePanel *MainWindow::rulePanel()
{
    return m_rulePanel;
}

ActionBar *MainWindow::actionBar()
{
    return m_actionBar;
}

MessageBar *MainWindow::messageBar()
{
    return m_messageBar;
}

ItemAdder &MainWindow::itemAdder()
{
    return *m_itemAdder;
}

PreviewScheduler &MainWindow::previewScheduler()
{
    return m_scheduler;
}

FolderCache &MainWindow::folderCache()
{
    return m_folderCache;
}

ctrm::PreviewInput MainWindow::currentPreviewInput() const
{
    ctrm::PreviewInput input;
    input.items = m_itemModel.items();
    input.chain = m_ruleModel.chain();
    input.folders = m_folderCache.folders(input.items);
    return input;
}

const ctrm::PreviewResult &MainWindow::currentPreview() const
{
    return m_preview;
}

bool MainWindow::previewIsCurrent() const
{
    return m_previewGeneration == m_scheduler.currentGeneration() && !m_scheduler.isBusy();
}

void MainWindow::requestPreview()
{
    m_scheduler.request(currentPreviewInput());
    // A large list runs on the worker: until its result comes, the shown state is old.
    if (!previewIsCurrent())
        updateWindowState();
}

void MainWindow::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    // ui-design 9.5: rules, table and toolbar are read-only during the batch.
    const QList<QAction *> editActions{
            m_actions.addFiles,       m_actions.addFolder,
            m_actions.removeFromList, m_actions.clearList,
            m_actions.refresh,        m_actions.include,
            m_actions.exclude,        m_actions.addRuleMenu->menuAction(),
            m_actions.duplicateRule,  m_actions.deleteRule,
            m_actions.moveRuleUp,     m_actions.moveRuleDown,
            m_actions.toggleRule,     m_actions.clearRules};
    if (running) {
        m_enabledBeforeRun.clear();
        for (QAction *action : editActions) {
            m_enabledBeforeRun.insert(action, action->isEnabled());
            action->setEnabled(false);
        }
    } else {
        for (auto it = m_enabledBeforeRun.cbegin(); it != m_enabledBeforeRun.cend(); ++it)
            it.key()->setEnabled(it.value());
        m_enabledBeforeRun.clear();
    }
    m_toolBar->setEnabled(!running);
    m_rulePanel->setEnabled(!running);
    m_tablePages->setEnabled(!running);
    updateWindowState();
    if (!running)
        updateListActions();
}

void MainWindow::setResultShown(bool shown, int renamedCount)
{
    if (shown == m_resultShown && renamedCount == m_renamedCount)
        return;
    m_resultShown = shown;
    m_renamedCount = shown ? renamedCount : 0;
    if (!shown)
        m_messageBar->clear();
    updateWindowState();
}

void MainWindow::setPresetTitle(const QString &name, bool modified)
{
    if (name.isEmpty()) {
        setWindowModified(false);
        setWindowTitle(QStringLiteral("CTRM"));
        return;
    }
    setWindowTitle(tr("CTRM - %1").arg(name) + QStringLiteral("[*]"));
    setWindowModified(modified);
}

void MainWindow::updateToolIcons()
{
    // The icons have the WindowText colour, so they follow the palette (ui-design 4.3).
    const QPalette p = palette();
    const Actions &a = m_actions;
    const QList<std::pair<QAction *, ToolIcon>> icons{
            {a.addFiles, ToolIcon::AddFiles},     {a.addFolder, ToolIcon::AddFolder},
            {a.removeFromList, ToolIcon::Remove}, {a.clearList, ToolIcon::Clear},
            {a.refresh, ToolIcon::Refresh},       {a.undoRename, ToolIcon::Undo},
            {a.history, ToolIcon::History}};
    for (const auto &[action, icon] : icons)
        action->setIcon(toolIcon(icon, p));
    m_toolBarPresets->setIcon(toolIcon(ToolIcon::Presets, p));
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange)
        updateToolIcons();
    if (e->type() != QEvent::ActivationChange || !isActiveWindow() || m_running ||
        m_itemModel.rowCount() == 0)
        return;
    m_folderCache.invalidate();
    // A window change while a preview is applied (a message bar shows) must not start a new
    // preview in the middle of it: that preview applies again (M01, stack overflow).
    if (m_applyingPreview) {
        m_previewAfterApply = true;
        return;
    }
    requestPreview();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    Settings &settings = m_ctx.settings();
    settings.setWindowGeometry(saveGeometry());
    settings.setWindowState(saveState());
    settings.setSplitterState(kMainSplitterKey, m_mainSplitter->saveState());
    QMainWindow::closeEvent(e);
}

QAction *MainWindow::makeEditAction(const QString &text, QKeySequence::StandardKey key,
                                    const char *slot)
{
    // Cut, Copy and Paste go to the focus widget. Text fields handle the keys themselves.
    auto *action = new QAction(text, this);
    action->setShortcuts(key);
    action->setMenuRole(QAction::NoRole);
    connect(action, &QAction::triggered, this, [slot] {
        QWidget *target = QApplication::focusWidget();
        if (!target)
            return;
        const int index = target->metaObject()->indexOfSlot(slot);
        if (index >= 0)
            target->metaObject()->method(index).invoke(target);
    });
    return action;
}

void MainWindow::buildMenus()
{
    const Actions &a = m_actions;
    QMenuBar *bar = menuBar();

    QMenu *file = bar->addMenu(tr("&File"));
    file->addActions({a.addFiles, a.addFolder, a.removeFromList, a.clearList, a.refresh});
    file->addSeparator();
    file->addAction(a.closeWindow);
    file->addAction(a.quit); // QuitRole: macOS moves it to the application menu.

    QMenu *edit = bar->addMenu(tr("&Edit"));
    edit->addAction(a.undoRename);
    edit->addSeparator();
    edit->addAction(makeEditAction(tr("Cut"), QKeySequence::Cut, "cut()"));
    edit->addAction(makeEditAction(tr("Copy"), QKeySequence::Copy, "copy()"));
    edit->addAction(makeEditAction(tr("Paste"), QKeySequence::Paste, "paste()"));
    edit->addAction(a.selectAll);
    edit->addSeparator();
    edit->addActions({a.include, a.exclude});
    edit->addSeparator();
    edit->addAction(a.find);
    // ui-design 13 has no Tools menu. macOS moves Preferences to the application menu.
    edit->addSeparator();
    edit->addAction(a.preferences);

    QMenu *rules = bar->addMenu(tr("&Rules"));
    rules->addMenu(a.addRuleMenu);
    rules->addActions({a.duplicateRule, a.deleteRule, a.moveRuleUp, a.moveRuleDown, a.toggleRule});
    rules->addSeparator();
    rules->addAction(a.clearRules);

    m_presetsMenu = bar->addMenu(tr("&Presets"));
    m_presetsMenu->setObjectName(QStringLiteral("presetsMenu"));
    m_presetsMenu->addActions(
            {a.saveAsPreset, a.updatePreset, a.managePresets, a.importPreset, a.exportRules});

    QMenu *view = bar->addMenu(tr("&View"));
    view->addAction(a.showSteps);
    m_showColumnsMenu = view->addMenu(tr("Show Columns"));
    m_showColumnsMenu->setObjectName(QStringLiteral("showColumnsMenu"));
    view->addAction(a.highlightChanged);
    view->addSeparator();
    view->addActions({a.showAllItems, a.showChangedOnly, a.showProblemsOnly});
    view->addSeparator();
    view->addAction(a.showToolbar);

    QMenu *renameMenu = bar->addMenu(tr("Re&name"));
    renameMenu->addActions({a.rename, a.skipAndRename, a.nextProblem, a.previousProblem});
    renameMenu->addSeparator();
    renameMenu->addAction(a.history);

    QMenu *help = bar->addMenu(tr("&Help"));
    help->addActions({a.help, a.regexHelp, a.reportProblem, a.about});
}

void MainWindow::buildToolBar()
{
    const Actions &a = m_actions;
    m_toolBar = new QToolBar(tr("Toolbar"), this);
    m_toolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setIconSize(QSize(16, 16)); // design-tokens: line icons at 16 px
    m_toolBar->setContextMenuPolicy(Qt::PreventContextMenu);
    m_toolBar->toggleViewAction()->setVisible(false);
    addToolBar(Qt::TopToolBarArea, m_toolBar);

    // Secondary: text beside the icon.
    for (QAction *action : {a.addFiles, a.addFolder}) {
        m_toolBar->addAction(action);
        auto *button = qobject_cast<QToolButton *>(m_toolBar->widgetForAction(action));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        useBodyFont(button); // M17
    }
    // A button of its own: an icon on the menu action would replace the menu bar title.
    m_toolBarPresets = new MenuToolButton(m_toolBar); // M17: the arrow on the text line
    m_toolBarPresets->setObjectName(QStringLiteral("toolBarPresets"));
    m_toolBarPresets->setText(tr("Presets"));
    m_toolBarPresets->setToolTip(tr("Presets"));
    m_toolBarPresets->setMenu(m_presetsMenu);
    useBodyFont(m_toolBarPresets);
    m_toolBar->addWidget(m_toolBarPresets);

    auto *stretch = new QWidget(m_toolBar);
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(stretch);

    // Tertiary: icon only, with tooltip. The toolbar never holds Rename (ui-design 4.3).
    const QList<QAction *> right{a.removeFromList, a.clearList,  a.refresh,
                                 nullptr,          a.undoRename, a.history};
    for (QAction *action : right) {
        if (!action) {
            m_toolBar->addSeparator();
            continue;
        }
        m_toolBar->addAction(action);
        auto *button = qobject_cast<QToolButton *>(m_toolBar->widgetForAction(action));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        QString name = action->text();
        name.remove(QLatin1Char('&'));
        if (name.endsWith(QLatin1StringView("...")))
            name.chop(3);
        button->setAccessibleName(name);
        button->setAccessibleDescription(action->toolTip());
    }
}

void MainWindow::buildCentralWidget()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_flowStripSlot = makeSlot("flowStripSlot", central);
    layout->addWidget(m_flowStripSlot);

    m_mainSplitter = new QSplitter(Qt::Horizontal, central);
    m_mainSplitter->setObjectName(QStringLiteral("mainSplitter"));
    m_mainSplitter->setChildrenCollapsible(false);
    m_rulePanelSlot = makeSlot("rulePanelSlot", m_mainSplitter);

    auto *right = new QWidget(m_mainSplitter);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    m_messageBarSlot = makeSlot("messageBarSlot", right);
    m_tableSlot = makeSlot("tableSlot", right);
    rightLayout->addWidget(m_messageBarSlot);
    rightLayout->addWidget(m_tableSlot, 1);

    m_mainSplitter->addWidget(m_rulePanelSlot);
    m_mainSplitter->addWidget(right);
    m_mainSplitter->setStretchFactor(0, kRuleStretch);
    m_mainSplitter->setStretchFactor(1, kTableStretch);
    m_mainSplitter->setSizes({kRuleStretch * kDefaultWidth, kTableStretch * kDefaultWidth});
    layout->addWidget(m_mainSplitter, 1);

    m_actionBarSlot = makeSlot("actionBarSlot", central);
    layout->addWidget(m_actionBarSlot);

    setCentralWidget(central);
}

void MainWindow::buildPanels()
{
    // Made in the tab order of ui-design 14: flow strip, rule list, rule editor, chain options,
    // filter, table, action bar.
    m_flowStrip = new FlowStrip(m_flowStripSlot);
    m_flowStripSlot->layout()->addWidget(m_flowStrip);
    // Mockup .flow (M22): a separator under the strip.
    auto *flowSeparator = new QFrame(m_flowStripSlot);
    flowSeparator->setObjectName(QStringLiteral("flowStripSeparator"));
    flowSeparator->setFrameShape(QFrame::HLine);
    flowSeparator->setFrameShadow(QFrame::Plain);
    flowSeparator->setForegroundRole(QPalette::Mid);
    flowSeparator->setFixedHeight(1);
    m_flowStripSlot->layout()->addWidget(flowSeparator);

    m_rulePanel = new RulePanel(m_ruleModel, m_actions, m_rulePanelSlot);
    m_rulePanelSlot->layout()->addWidget(m_rulePanel);

    m_messageBar = new MessageBar(m_messageBarSlot);
    m_messageBarSlot->layout()->addWidget(m_messageBar);

    m_tablePages = new QStackedWidget(m_tableSlot);
    m_tablePages->setObjectName(QStringLiteral("tablePages"));
    m_emptyState = new EmptyState(m_actions, m_tablePages);
    m_tablePage = buildTablePage();
    m_tablePages->addWidget(m_emptyState);
    m_tablePages->addWidget(m_tablePage);
    m_tableSlot->layout()->addWidget(m_tablePages);

    m_actionBar = new ActionBar(m_actions, m_actionBarSlot);
    m_actionBarSlot->layout()->addWidget(m_actionBar);
}

QWidget *MainWindow::buildTablePage()
{
    auto *page = new QWidget(m_tablePages);
    page->setObjectName(QStringLiteral("tablePage"));

    // Filter bar (ui-design 4.1, 7.6).
    auto *filterBar = new QWidget(page);
    auto *filterLabel = new QLabel(tr("&Filter:"), filterBar);
    m_filter = new QLineEdit(filterBar);
    m_filter->setObjectName(QStringLiteral("filterField"));
    m_filter->setPlaceholderText(tr("Search names"));
    m_filter->setClearButtonEnabled(true);
    filterLabel->setBuddy(m_filter);
    auto *showLabel = new QLabel(tr("&Show:"), filterBar);
    m_show = new QComboBox(filterBar);
    m_show->setObjectName(QStringLiteral("showCombo"));
    m_show->addItem(tr("All items"), int(ShowMode::All));
    m_show->addItem(tr("Changed items"), int(ShowMode::Changed));
    m_show->addItem(tr("Problems"), int(ShowMode::Problems));
    m_show->addItem(tr("Warnings"), int(ShowMode::Warnings));
    showLabel->setBuddy(m_show);
    // The field has the visible height of the Show control (audit M18). On macOS the combo box
    // widget is taller than the frame it draws (focus ring), so use its layout item rect.
    {
        QStyleOptionComboBox option;
        option.initFrom(m_show);
        option.rect = QRect(QPoint(), m_show->sizeHint());
        QRect visible =
                m_show->style()->subElementRect(QStyle::SE_ComboBoxLayoutItem, &option, m_show);
        if (!visible.isValid())
            visible = option.rect;
        m_filter->setFixedHeight(std::max(m_filter->sizeHint().height(), visible.height()));
    }
    auto *filterLayout = new QHBoxLayout(filterBar);
    filterLayout->addWidget(filterLabel);
    filterLayout->addWidget(m_filter, 1);
    filterLayout->addSpacing(filterLayout->spacing() * 2);
    filterLayout->addWidget(showLabel);
    filterLayout->addWidget(m_show);

    // The table, or the text when the filter hides all rows (ui-design 5.4).
    m_tableOrNoMatch = new QStackedWidget(page);
    m_table = new ItemTableView(m_itemModel, m_proxy, m_tableOrNoMatch);
    m_table->setObjectName(QStringLiteral("itemTable"));
    m_noMatch = new QWidget(m_tableOrNoMatch);
    m_noMatch->setObjectName(QStringLiteral("noMatchPage"));
    auto *noMatchText = new QLabel(tr("No items match the filter."), m_noMatch);
    noMatchText->setObjectName(QStringLiteral("noMatchText"));
    noMatchText->setAlignment(Qt::AlignHCenter);
    const QString clearText = tr("Clear Filter");
    auto *clearLink = new QLabel(
            QStringLiteral("<a href=\"clear\">%1</a>").arg(clearText.toHtmlEscaped()), m_noMatch);
    clearLink->setObjectName(QStringLiteral("clearFilterLink"));
    clearLink->setAlignment(Qt::AlignHCenter);
    clearLink->setTextFormat(Qt::RichText);
    clearLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
    clearLink->setFocusPolicy(Qt::TabFocus);
    clearLink->setAccessibleName(clearText);
    connect(clearLink, &QLabel::linkActivated, this, [this] { clearFilter(); });
    auto *noMatchLayout = new QVBoxLayout(m_noMatch);
    noMatchLayout->addStretch(1);
    noMatchLayout->addWidget(noMatchText);
    noMatchLayout->addWidget(clearLink);
    noMatchLayout->addStretch(2);
    m_tableOrNoMatch->addWidget(m_table);
    m_tableOrNoMatch->addWidget(m_noMatch);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(filterBar);
    layout->addWidget(m_tableOrNoMatch, 1);
    return page;
}

void MainWindow::connectActions()
{
    const Actions &a = m_actions;

    // The preview follows the list, the rules and the chain options.
    // New items can be in a folder that changed after it was read: read the folders again
    // before the preview of listChanged. The batch and undo plans always read the disk.
    connect(&m_itemModel, &QAbstractItemModel::rowsInserted, this,
            [this] { m_folderCache.invalidate(); });
    connect(&m_itemModel, &ItemTableModel::listChanged, this, &MainWindow::contentChanged);
    connect(&m_ruleModel, &RuleListModel::chainChanged, this, [this] {
        const ctrm::RuleChain &chain = m_ruleModel.chain();
        QStringList names;
        for (int i = 0; i < chain.size(); ++i)
            names.append(m_ruleModel.index(i).data(Qt::DisplayRole).toString());
        m_itemModel.setRuleNames(names);
        m_itemModel.setPortableNames(chain.options.portableNames);
        m_itemModel.setPostRun({});
        contentChanged();
    });
    connect(&m_scheduler, &PreviewScheduler::previewReady, this, &MainWindow::previewReady);

    // Adding items.
    connect(a.addFiles, &QAction::triggered, this,
            [this] { m_itemAdder->addFilesViaDialog(this); });
    connect(a.addFolder, &QAction::triggered, this,
            [this] { m_itemAdder->addFolderViaDialog(this); });
    connect(m_emptyState, &EmptyState::pathsDropped, m_itemAdder, &ItemAdder::addPaths);
    connect(m_table, &ItemTableView::pathsDropped, m_itemAdder, &ItemAdder::addPaths);
    connect(m_itemAdder, &ItemAdder::statusMessage, this,
            [this](const QString &text) { statusBar()->showMessage(text); });

    // The list.
    connect(a.removeFromList, &QAction::triggered, this,
            [this] { m_itemModel.removeItems(m_table->selectedIds()); });
    connect(a.clearList, &QAction::triggered, this, &MainWindow::clearList);
    connect(a.refresh, &QAction::triggered, this, &MainWindow::refresh);
    connect(a.include, &QAction::triggered, this,
            [this] { m_itemModel.setIncluded(m_table->selectedIds(), true); });
    connect(a.exclude, &QAction::triggered, this,
            [this] { m_itemModel.setIncluded(m_table->selectedIds(), false); });
    connect(a.selectAll, &QAction::triggered, this, [this] {
        // Text fields and lists select their own contents; else the table.
        QWidget *target = QApplication::focusWidget();
        if (target && target->window() == this &&
            target->metaObject()->indexOfSlot("selectAll()") >= 0) {
            QMetaObject::invokeMethod(target, "selectAll");
            return;
        }
        m_table->selectAll();
    });
    connect(a.clearRules, &QAction::triggered, this, [this] {
        ctrm::RuleChain empty;
        empty.options = m_ruleModel.chain().options;
        m_ruleModel.setChain(empty);
    });

    // Filter.
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_proxy.setText(text);
        updateTablePage();
    });
    connect(m_show, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto mode = ShowMode(m_show->itemData(index).toInt());
        m_proxy.setShowMode(mode);
        // The menu has no Warnings command: then no menu command is checked.
        QActionGroup *group = m_actions.showAllItems->actionGroup();
        group->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
        m_actions.showAllItems->setChecked(mode == ShowMode::All);
        m_actions.showChangedOnly->setChecked(mode == ShowMode::Changed);
        m_actions.showProblemsOnly->setChecked(mode == ShowMode::Problems);
        group->setExclusionPolicy(QActionGroup::ExclusionPolicy::Exclusive);
        updateTablePage();
    });
    connect(a.find, &QAction::triggered, this, [this] {
        focus(m_filter);
        m_filter->selectAll();
    });
    connect(a.showAllItems, &QAction::triggered, this, [this] { setShowMode(ShowMode::All); });
    connect(a.showChangedOnly, &QAction::triggered, this,
            [this] { setShowMode(ShowMode::Changed); });
    connect(a.showProblemsOnly, &QAction::triggered, this,
            [this] { setShowMode(ShowMode::Problems); });
    connect(m_actionBar, &ActionBar::problemLinkClicked, this,
            [this] { setShowMode(ShowMode::Problems); });
    connect(m_actionBar, &ActionBar::warningsLinkClicked, this,
            [this] { setShowMode(ShowMode::Warnings); });
    for (auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(&m_proxy, signal, this, &MainWindow::updateTablePage);
    connect(&m_proxy, &QAbstractItemModel::modelReset, this, &MainWindow::updateTablePage);
    connect(&m_proxy, &QAbstractItemModel::layoutChanged, this, &MainWindow::updateTablePage);

    // Problems and focus.
    connect(a.nextProblem, &QAction::triggered, this, [this] {
        m_table->selectNextProblem(true);
        focus(m_table);
    });
    connect(a.previousProblem, &QAction::triggered, this, [this] {
        m_table->selectNextProblem(false);
        focus(m_table);
    });
    connect(a.focusRules, &QAction::triggered, this, &MainWindow::focusRules);
    connect(a.focusTable, &QAction::triggered, this, &MainWindow::focusTable);
    connect(a.focusEditor, &QAction::triggered, this, &MainWindow::focusEditor);
    connect(m_flowStrip, &FlowStrip::stepClicked, this, &MainWindow::focusStep);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::updateListActions);

    // View.
    connect(a.showSteps, &QAction::toggled, m_flowStripSlot, &QWidget::setVisible);
    connect(a.highlightChanged, &QAction::toggled, m_table, &ItemTableView::setHighlightChanged);
    connect(m_actionBar, &ActionBar::addNumberingClicked, this, [this] {
        if (auto *add = m_actions.addRuleMenu->findChild<QAction *>(
                    QStringLiteral("addRule_numbering")))
            add->trigger();
    });
}

void MainWindow::clearList()
{
    // S07: no question; Undo in the message bar puts the same items back (same order, same
    // Include states). The bar closes at the next list change.
    const QList<ctrm::Item> cleared = m_itemModel.items();
    if (cleared.isEmpty())
        return;
    m_itemModel.clear();
    auto *undo = new QPushButton(tr("Undo"));
    undo->setObjectName(QStringLiteral("clearListUndo"));
    connect(undo, &QPushButton::clicked, this, [this, cleared] {
        if (m_itemModel.rowCount() == 0)
            m_itemModel.addItems(cleared);
    });
    const int n = int(cleared.size());
    m_messageBar->showMessage(StateIcon::Done, numerus(tr("Cleared %n item(s).", nullptr, n), n),
                              {undo});
    m_clearMessageShown = true;
}

void MainWindow::contentChanged()
{
    if (std::exchange(m_clearMessageShown, false))
        m_messageBar->clear();
    setResultShown(false);
    requestPreview();
    updateListActions();
}

void MainWindow::previewReady(const ctrm::PreviewResult &result, quint64 generation)
{
    const QScopedValueRollback applying(m_applyingPreview, true);
    m_preview = result;
    m_previewGeneration = generation;
    m_itemModel.setPreview(result);
    int numbered = 0;
    for (const ctrm::ItemPreview &p : result.items)
        numbered += p.included && !p.skipped ? 1 : 0;
    m_rulePanel->setNumberedCount(numbered);
    updateWindowState();
    updateConflictTip();
    updateTablePage();
    if (std::exchange(m_previewAfterApply, false))
        QMetaObject::invokeMethod(this, &MainWindow::requestPreview, Qt::QueuedConnection);
}

void MainWindow::updateWindowState()
{
    const QList<ctrm::Item> items = m_itemModel.items();
    WindowStateInput in;
    in.itemCount = int(items.size());
    const ctrm::RuleChain &chain = m_ruleModel.chain();
    for (int i = 0; i < chain.size(); ++i)
        in.enabledRuleCount += chain.at(i)->enabled ? 1 : 0;
    in.summary = m_preview.summary;
    // "Files" or "Folders" in the Rename text: from the items that will change, when the
    // preview is for this list.
    const bool previewFits = previewIsCurrent() && m_preview.items.size() == items.size();
    int files = 0;
    int folders = 0;
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (previewFits) {
            const ctrm::Status status = m_preview.items.at(i).status;
            if (status != ctrm::Status::Changed && status != ctrm::Status::CaseOnly)
                continue;
        }
        (items.at(i).isFolder ? folders : files) += 1;
    }
    in.allFiles = folders == 0;
    in.allFolders = files == 0 && folders > 0;
    in.running = m_running;
    in.resultShown = m_resultShown;
    in.renamedCount = m_renamedCount;
    m_state = computeWindowState(in);
    if (!previewIsCurrent() && !m_running && in.itemCount > 0 && in.enabledRuleCount > 0) {
        // The counts are from the old preview: no rename until the new preview is ready.
        m_state.renameEnabled = false;
        m_state.reason = tr("Checking the new names...");
        m_state.skipVisible = false;
        if (m_state.primary == PrimaryTarget::Rename)
            m_state.primary = PrimaryTarget::None;
    }
    // M27: Next and Previous Problem only when there are problems.
    const bool problems = !m_running && in.itemCount > 0 && m_preview.summary.blocked() > 0;
    m_actions.nextProblem->setEnabled(problems);
    m_actions.previousProblem->setEnabled(problems);
    m_flowStrip->setState(m_state);
    m_actionBar->setState(m_state, m_preview.summary);
    m_rulePanel->setPrimaryAddRule(m_state.primary == PrimaryTarget::AddRule);
}

void MainWindow::updateConflictTip()
{
    // ui-design 9.3: only when every conflict is a duplicate name in the batch.
    bool onlyDuplicates = m_preview.summary.conflicts > 0 && !m_running;
    for (const ctrm::ItemPreview &p : std::as_const(m_preview.items)) {
        if (!onlyDuplicates)
            break;
        if (p.status != ctrm::Status::Conflict)
            continue;
        for (const ctrm::Diagnostic &d : p.problems) {
            if (d.code != ctrm::DiagCode::DuplicateInBatch)
                onlyDuplicates = false;
        }
    }
    m_actionBar->setConflictTip(onlyDuplicates);
}

void MainWindow::updateTablePage()
{
    const bool empty = m_itemModel.rowCount() == 0;
    m_tablePages->setCurrentWidget(empty ? static_cast<QWidget *>(m_emptyState) : m_tablePage);
    const bool noMatch = !empty && m_proxy.rowCount() == 0;
    const bool hadFocus = m_table->hasFocus();
    m_tableOrNoMatch->setCurrentWidget(noMatch ? m_noMatch : static_cast<QWidget *>(m_table));
    if (noMatch && hadFocus)
        focus(m_filter);
}

void MainWindow::updateListActions()
{
    if (m_running)
        return;
    const bool hasItems = m_itemModel.rowCount() > 0;
    const bool hasSelection = m_table->selectionModel()->hasSelection();
    m_actions.removeFromList->setEnabled(hasSelection);
    m_actions.include->setEnabled(hasSelection);
    m_actions.exclude->setEnabled(hasSelection);
    m_actions.clearList->setEnabled(hasItems);
    m_actions.refresh->setEnabled(hasItems);
    m_actions.clearRules->setEnabled(m_ruleModel.rowCount() > 0);
}

void MainWindow::setShowMode(ShowMode mode)
{
    m_show->setCurrentIndex(m_show->findData(int(mode)));
}

void MainWindow::clearFilter()
{
    m_filter->clear();
    setShowMode(ShowMode::All);
    focus(m_table);
}

void MainWindow::refresh()
{
    // Reads the folders and the identities again.
    m_folderCache.invalidate();
    QList<ctrm::Item> fresh;
    for (const ctrm::Item &item : m_itemModel.items()) {
        QString error;
        if (std::optional<ctrm::Item> made =
                    ctrm::makeItem(m_ctx.fileSystem(), item.path(), &error)) {
            made->id = item.id;
            fresh.append(*made);
        }
    }
    m_itemModel.replaceIdentities(fresh);
    requestPreview();
}

void MainWindow::focusStep(int step)
{
    // ui-design 5.3 (S01).
    switch (step) {
    case 0:
        focusTable();
        break;
    case 1:
        focusRules();
        break;
    case 2:
        focus(m_actionBar->renameButton());
        break;
    default:
        break;
    }
}

void MainWindow::focusRules()
{
    if (m_ruleModel.rowCount() > 0)
        focus(m_rulePanel->ruleList());
    else
        focus(m_rulePanel->addRuleButton());
}

void MainWindow::focusTable()
{
    if (m_itemModel.rowCount() == 0)
        focus(m_emptyState->findChild<QPushButton *>(QStringLiteral("emptyAddFiles")));
    else if (m_tableOrNoMatch->currentWidget() == m_table)
        focus(m_table);
    else
        focus(m_filter);
}

void MainWindow::focusEditor()
{
    if (QWidget *editor = m_rulePanel->currentEditor())
        focus(firstFocusChild(editor, this));
}

void MainWindow::restoreLayout()
{
    const Settings &settings = m_ctx.settings();
    const QByteArray geometry = settings.windowGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    const QByteArray state = settings.windowState();
    if (!state.isEmpty())
        restoreState(state);
    const QByteArray splitter = settings.splitterState(kMainSplitterKey);
    if (!splitter.isEmpty())
        m_mainSplitter->restoreState(splitter);
    m_actions.showToolbar->setChecked(!m_toolBar->isHidden());
}

} // namespace ctrm::app
