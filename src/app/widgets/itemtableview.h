// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/item.h"

#include <QSet>
#include <QTableView>

class QIdentityProxyModel;
class QMenu;

namespace ctrm::app {

class ItemDelegate;
class ItemFilterProxy;
class ItemTableModel;

// The item table (ui-design 7). It shows proxy (a filter of model) through a small proxy that
// adds the screen reader text of each row.
class ItemTableView : public QTableView
{
    Q_OBJECT
public:
    ItemTableView(ItemTableModel &model, ItemFilterProxy &proxy, QWidget *parent);
    void selectNextProblem(bool forward); // F8 / Shift+F8, wraps
    QList<ctrm::ItemId> selectedIds() const;
    void setHighlightChanged(bool on);
    // The text shown while the user drags files over the table; empty when not.
    QString dropText() const;

signals:
    void pathsDropped(const QStringList &paths); // external file drop
    // The view opened the folder of the item in the file manager.
    void showInFileManager(ctrm::ItemId id);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void fitNameColumns();
    void updateColors();
    void updateRowHeight();
    void updateAutoColumns();
    void updateSortIndicator();
    void syncColumnActions();
    void sortByColumn(int column);
    void setDropCount(int count);
    QModelIndexList selectedRowIndexes() const;
    void copyNames(int column);

    ItemTableModel &m_model;
    ItemFilterProxy &m_proxy;
    QIdentityProxyModel *m_rowText;
    ItemDelegate *m_delegate;
    QMenu *m_columnsMenu;
    QSet<int> m_userColumns; // columns the user showed or hid: no automatic rule
    int m_dropCount = 0;
    bool m_namesSetByUser = false; // the user changed the width of a name column
    bool m_fittingNames = false;
    bool m_headerPressed = false; // a mouse button is down on the header
};

} // namespace ctrm::app
