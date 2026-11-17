// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "actions.h"
#include "models/itemfilterproxy.h"
#include "models/itemtablemodel.h"
#include "models/rulelistmodel.h"
#include "preview/foldercache.h"
#include "preview/previewscheduler.h"
#include "ui/windowstate.h"

#include <QHash>
#include <QMainWindow>

class QComboBox;
class QLabel;
class QLineEdit;
class QSplitter;
class QStackedWidget;
class QToolBar;
class QToolButton;

namespace ctrm::app {

class ActionBar;
class AppContext;
class EmptyState;
class FlowStrip;
class ItemAdder;
class ItemTableView;
class MessageBar;
class RulePanel;

// The main window: menus, toolbar, flow strip, rule panel, item table and action bar. It keeps
// the preview of the list and the rules current, and shows the window state (ui-design 5, 9).
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(AppContext &ctx, QWidget *parent = nullptr);
    ~MainWindow() override;

    const Actions &actions() const;
    QWidget *flowStripSlot() const;  // holds the FlowStrip
    QWidget *rulePanelSlot() const;  // left side of the horizontal splitter
    QWidget *tableSlot() const;      // right side
    QWidget *actionBarSlot() const;  // bottom
    QWidget *messageBarSlot() const; // above the table
    QSplitter *mainSplitter() const;

    ItemTableModel &itemModel();
    RuleListModel &ruleModel();
    ItemTableView *tableView();
    RulePanel *rulePanel();
    ActionBar *actionBar();
    MessageBar *messageBar(); // the result message above the table (ui-design 9.8)
    ItemAdder &itemAdder();
    PreviewScheduler &previewScheduler();
    FolderCache &folderCache();

    ctrm::PreviewInput currentPreviewInput() const; // items, chain copy, folders, os
    const ctrm::PreviewResult &currentPreview() const;
    bool previewIsCurrent() const; // no request pending
    void requestPreview();         // builds the input, calls the scheduler
    void setRunning(bool running); // read-only rules, table, toolbar (ui-design 9.5)
    void setResultShown(bool shown);
    void setPresetTitle(const QString &name, bool modified); // "CTRM - <name>" + modified mark

protected:
    // Updates the tool icons. Activation also reads the folders again: files can change
    // while CTRM is not active.
    void changeEvent(QEvent *e) override;
    void closeEvent(QCloseEvent *e) override; // saves geometry, state, splitters

private:
    void buildMenus();
    void buildToolBar();
    void updateToolIcons();
    void buildCentralWidget();
    void buildPanels();
    QWidget *buildTablePage();
    void connectActions();
    void restoreLayout();
    QAction *makeEditAction(const QString &text, QKeySequence::StandardKey key, const char *slot);

    void contentChanged(); // the list, the rules or the chain options changed
    void previewReady(const ctrm::PreviewResult &result, quint64 generation);
    void updateWindowState();
    void updateConflictTip();
    void updateTablePage();
    void updateListActions();
    void setShowMode(ShowMode mode);
    void clearFilter();
    void refresh();
    void focusStep(int step);
    void focusRules();
    void focusTable();
    void focusEditor();

    AppContext &m_ctx;
    Actions m_actions;
    ItemTableModel m_itemModel;
    RuleListModel m_ruleModel;
    ItemFilterProxy m_proxy;
    mutable FolderCache m_folderCache; // currentPreviewInput() fills the cache
    PreviewScheduler m_scheduler;
    ItemAdder *m_itemAdder;
    ctrm::PreviewResult m_preview;
    quint64 m_previewGeneration = 0;
    WindowStateInfo m_state;
    bool m_running = false;
    bool m_resultShown = false;
    bool m_applyingPreview = false;   // in previewReady
    bool m_previewAfterApply = false; // an activation came while a preview was applied
    QHash<QAction *, bool> m_enabledBeforeRun;

    QMenu *m_presetsMenu = nullptr;
    QToolBar *m_toolBar = nullptr;
    QToolButton *m_toolBarPresets = nullptr;
    QMenu *m_showColumnsMenu = nullptr;
    QWidget *m_flowStripSlot = nullptr;
    QWidget *m_rulePanelSlot = nullptr;
    QWidget *m_tableSlot = nullptr;
    QWidget *m_actionBarSlot = nullptr;
    QWidget *m_messageBarSlot = nullptr;
    QSplitter *m_mainSplitter = nullptr;

    FlowStrip *m_flowStrip = nullptr;
    RulePanel *m_rulePanel = nullptr;
    MessageBar *m_messageBar = nullptr;
    QStackedWidget *m_tablePages = nullptr; // EmptyState or the table page
    EmptyState *m_emptyState = nullptr;
    QWidget *m_tablePage = nullptr;
    QLineEdit *m_filter = nullptr;
    QComboBox *m_show = nullptr;
    QStackedWidget *m_tableOrNoMatch = nullptr;
    ItemTableView *m_table = nullptr;
    QWidget *m_noMatch = nullptr;
    ActionBar *m_actionBar = nullptr;
};

} // namespace ctrm::app
