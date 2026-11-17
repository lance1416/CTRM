// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "actions.h"
#include "ui/windowstate.h"

#include <QElapsedTimer>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;
class QToolButton;

namespace ctrm::app {

// The bar under the table (ui-design 5.2, 5.5, 9.3, 9.5): the count summary on the left, the
// reason, Skip and Rename on the right. During a batch: progress and Cancel.
class ActionBar : public QWidget
{
    Q_OBJECT
public:
    ActionBar(const Actions &actions, QWidget *parent);
    void setState(const WindowStateInfo &info, const ctrm::PreviewSummary &summary);
    void showProgress(int done, int total); // switches to progress + Cancel after 1 s
    void hideProgress();
    // ui-design 9.3 (S11): "Same names: Add Numbering" right of the reason, when all conflicts
    // are duplicate names in the batch. offNumberingRule > 0: the link says "Turn On Rule N".
    void setConflictTip(bool shown, int offNumberingRule = 0);
    QPushButton *renameButton() const;
    QPushButton *skipButton() const;

signals:
    void problemLinkClicked();  // sets filter to Problems
    void warningsLinkClicked(); // sets filter to Warnings
    void addNumberingClicked(); // also for "Turn On Rule N"

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateSummary(const ctrm::PreviewSummary &summary);
    void updateAccent();
    void updateReasonIcon();
    void revealProgress();
    void updateTip();
    void updateLinkColor();
    void announce(const QString &summaryText);

    Actions m_actions;
    WindowStateInfo m_info;
    ctrm::PreviewSummary m_summary;
    QStackedWidget *m_pages;
    QWidget *m_normalPage;
    QWidget *m_progressPage;
    QWidget *m_summaryWidget;
    QHBoxLayout *m_summaryLayout;
    QList<QWidget *> m_summaryParts;
    QToolButton *m_previousProblem;
    QToolButton *m_nextProblem;
    QLabel *m_reasonIcon;
    QLabel *m_reason;
    QLabel *m_tipText;
    QToolButton *m_addNumbering;
    bool m_tipWanted = false;
    QPushButton *m_skip;
    QPushButton *m_rename;
    QLabel *m_progressLabel;
    QProgressBar *m_progressBar;
    QPushButton *m_cancel;
    QTimer *m_progressTimer;
    QTimer *m_announceTimer;
    QElapsedTimer m_progressClock;
};

} // namespace ctrm::app
