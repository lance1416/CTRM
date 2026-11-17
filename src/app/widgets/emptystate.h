// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "actions.h"

#include <QStringList>
#include <QWidget>

class QLabel;
class QMimeData;
class QPainter;
class QPushButton;

namespace ctrm::app {

// The local file paths of a drop; other URLs are ignored.
QStringList localDropPaths(const QMimeData *mime);

// The drag-over look (ui-design 9.2): a dashed Highlight border and, when text is not empty, the
// text in the centre.
void paintDropFrame(QPainter &painter, const QRect &rect, const QPalette &palette,
                    const QString &text);

// The drop area when the list is empty (ui-design 5.4): Add Files... is the primary button.
class EmptyState : public QWidget
{
    Q_OBJECT
public:
    explicit EmptyState(const Actions &actions, QWidget *parent);

signals:
    void pathsDropped(const QStringList &paths);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateLook();
    void setDropCount(int count); // 0 = no drag over the widget

    QLabel *m_icon;
    QLabel *m_title;
    QPushButton *m_addFiles;
    QPushButton *m_addFolder;
    bool m_dragOver = false;
};

} // namespace ctrm::app
