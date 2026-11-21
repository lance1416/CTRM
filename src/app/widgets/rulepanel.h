// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rulechain.h"

#include <QHash>
#include <QRect>
#include <QWidget>

class QBoxLayout;
class QLabel;
class QListView;
class QModelIndex;
class QScrollArea;
class QStackedWidget;
class QStyleOptionViewItem;
class QPushButton;
class QToolButton;

namespace ctrm::app {

struct Actions;
class ChainOptionsWidget;
class LabelColumnSync;
class RuleEditor;
class RuleListModel;

// The parts of a rule list row (ui-design 4.4, audit R6-02), in the coordinates of option.rect:
// the check box centred on the row; the title and under it the summary, at one x, space-2 after
// the check box; with hasError, the error icon at the end of the title line.
struct RuleRowLayout
{
    QRect check;
    QRect title;
    QRect summary;
    QRect icon;
};
RuleRowLayout ruleRowLayout(const QStyleOptionViewItem &option, bool hasError = false);

// The rule panel (ui-design 4.4): the header with Add Rule, the rule list and its tool buttons,
// the editor of the selected rule, and the chain options. It connects the rule actions.
class RulePanel : public QWidget
{
    Q_OBJECT
public:
    RulePanel(RuleListModel &model, const Actions &actions, QWidget *parent);
    QListView *ruleList() const;
    QWidget *currentEditor() const;
    QPushButton *addRuleButton() const;
    void setPrimaryAddRule(bool primary); // empty-with-items state (ui-design 5.4)
    void setNumberedCount(int n);         // the numbering editor and summary
    // Cmd+3 (ui-design 12): the focus goes to the first field of the editor. reason:
    // Qt::ShortcutFocusReason only when a key press starts the move; that reason turns keyboard
    // mode on (ui-design 14 rule 5, R8-01).
    void focusEditor(Qt::FocusReason reason = Qt::OtherFocusReason);
    // max(360 px, the widest editor page): computed at the start and on a font, style or scale
    // change only, never from the current page, the rules or Chain options (O08, ui-design 4.4).
    QSize minimumSizeHint() const override;

signals:
    void chainOptionsChanged(const ctrm::ChainOptions &o);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool event(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void fillAddRuleMenu();
    void addRule(const QString &type);
    void showRule(int row);
    void detachEditor();
    void focusFirstField(RuleEditor *editor, Qt::FocusReason reason = Qt::OtherFocusReason);
    void onEditorEdited(RuleEditor *editor);
    void applyChainOptions(const ctrm::ChainOptions &o);
    void updateActions();
    void updateEmptyState();
    void updateAddRuleLook();
    void updateEmptyLook();
    void openAddRuleMenu();
    void updateIcons();
    void setListShortcuts(bool on);
    void updateMinimumWidth();
    void scheduleMinimumWidth();
    int currentRow() const;
    void selectRow(int row);

    RuleListModel &m_model;
    const Actions &m_actions;
    QPushButton *m_addRule;
    QBoxLayout *m_headerLayout;
    QBoxLayout *m_emptyButtons;
    QWidget *m_emptyPage;
    QLabel *m_emptyTitle;
    QLabel *m_emptyText;
    QWidget *m_splitter;
    QListView *m_list;
    QList<std::pair<QToolButton *, QAction *>> m_toolButtons;
    QStackedWidget *m_editors;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_noRulePage;
    QWidget *m_unsupportedPage;
    QHash<QString, RuleEditor *> m_editorByType;
    RuleEditor *m_shownEditor = nullptr; // the editor that holds a rule of the model
    ctrm::Rule *m_shownRule = nullptr;
    bool m_ignoreEdits = false;
    ChainOptionsWidget *m_chainOptions;
    LabelColumnSync *m_labelSync;
    bool m_primary = false;
    QWidget *m_header = nullptr;
    int m_minWidth = 0;
    bool m_minWidthPending = false;
};

} // namespace ctrm::app
