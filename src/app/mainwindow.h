// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "actions.h"

#include <QMainWindow>

class QSplitter;
class QToolBar;

namespace ctrm::app {

class AppContext;

// The main window shell: menus, toolbar and empty slots for the panels. Task 13 puts the
// real panels into the slots.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(AppContext &ctx, QWidget *parent = nullptr);

    const Actions &actions() const;
    QWidget *flowStripSlot() const;  // placeholder, replaced in Task 13
    QWidget *rulePanelSlot() const;  // left side of the horizontal splitter
    QWidget *tableSlot() const;      // right side
    QWidget *actionBarSlot() const;  // bottom
    QWidget *messageBarSlot() const; // above the table
    QSplitter *mainSplitter() const;

protected:
    void closeEvent(QCloseEvent *e) override; // saves geometry, state, splitters

private:
    void buildMenus();
    void buildToolBar();
    void buildCentralWidget();
    void restoreLayout();
    QAction *makeEditAction(const QString &text, QKeySequence::StandardKey key, const char *slot);

    AppContext &m_ctx;
    Actions m_actions;
    QMenu *m_presetsMenu = nullptr;
    QToolBar *m_toolBar = nullptr;
    QWidget *m_flowStripSlot = nullptr;
    QWidget *m_rulePanelSlot = nullptr;
    QWidget *m_tableSlot = nullptr;
    QWidget *m_actionBarSlot = nullptr;
    QWidget *m_messageBarSlot = nullptr;
    QSplitter *m_mainSplitter = nullptr;
};

} // namespace ctrm::app
