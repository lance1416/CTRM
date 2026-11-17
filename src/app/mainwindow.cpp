// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include "appcontext.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMetaMethod>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

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
    return slot;
}

} // namespace

MainWindow::MainWindow(AppContext &ctx, QWidget *parent)
    : QMainWindow(parent)
    , m_ctx(ctx)
    , m_actions(createActions(this))
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
    buildCentralWidget();
    statusBar();

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

    restoreLayout();
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
    QMenu *columns = view->addMenu(tr("Show Columns"));
    columns->setObjectName(QStringLiteral("showColumnsMenu"));
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
    m_toolBar->setContextMenuPolicy(Qt::PreventContextMenu);
    m_toolBar->toggleViewAction()->setVisible(false);
    addToolBar(Qt::TopToolBarArea, m_toolBar);

    // Secondary: text beside the icon.
    QAction *presets = m_presetsMenu->menuAction();
    presets->setToolTip(tr("Presets"));
    for (QAction *action : {a.addFiles, a.addFolder, presets}) {
        m_toolBar->addAction(action);
        auto *button = qobject_cast<QToolButton *>(m_toolBar->widgetForAction(action));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        if (action == presets)
            button->setPopupMode(QToolButton::InstantPopup);
    }

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
