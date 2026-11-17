// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/item.h"
#include "preview/previewengine.h"
#include "text/messages.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>

#include <optional>

namespace ctrm::app {

// The list of items and their preview (ui-design 7). The list order is the rename order:
// a sort changes the order one time only (spec 10a.5).
class ItemTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { Include, State, OldName, NewName, Message, Folder, Size, Modified, ColumnCount };
    enum Role {
        StatusRole = Qt::UserRole + 1, // int(ctrm::Status) of the preview
        ItemIdRole,                    // ctrm::ItemId as qulonglong
        IsBlockingRole,                // included and the status blocks
        HasWarningRole,                // included, not blocked, with one or more warnings
        PostRunRole,                   // int(PostRun)
        IsExcludedRole,
    };

    explicit ItemTableModel(QObject *parent = nullptr);

    QList<ctrm::Item> items() const;
    // Assigns ids. Ignores items whose canonical path is already in the list (also twice in
    // the argument). Returns the number of ignored items.
    int addItems(QList<ctrm::Item> items);
    void removeItems(const QList<ctrm::ItemId> &ids);
    void clear();
    void setIncluded(const QList<ctrm::ItemId> &ids, bool included);
    // Moves the rows (any order, need not be adjacent) before destinationRow (a row number
    // before the move; rowCount() for the end). The moved rows keep their relative order.
    void moveRows(const QList<int> &rows, int destinationRow);
    using QAbstractTableModel::moveRows;
    // Reorders the list one time by the column text (natural order, stable).
    void sortOnce(Column column, Qt::SortOrder order);
    int sortedColumn() const; // -1 after any other change of the order
    Qt::SortOrder sortedOrder() const;

    // The preview must be for the current items, in the current order. A preview with another
    // item count is ignored.
    void setPreview(const ctrm::PreviewResult &preview);
    const ctrm::PreviewResult &preview() const;
    // Display names of the rules of the chain, for the rule error text.
    void setRuleNames(const QStringList &names);
    void setPortableNames(bool portable);

    // Post-run states by item. Any change of the list clears them.
    void setPostRun(const QHash<ctrm::ItemId, PostRunState> &states);

    quint64 listVersion() const; // +1 on add, remove, clear, move, sort, include

    // QAbstractItemModel
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    Qt::DropActions supportedDropActions() const override;
    Qt::DropActions supportedDragActions() const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

    static constexpr const char *kRowsMimeType = "application/x-ctrm-rows";

signals:
    void listChanged();

private:
    struct FileInfo
    {
        qint64 size = -1; // -1: folder or not known
        QDateTime modified;
    };

    const ctrm::ItemPreview &previewAt(int row) const;
    QString messageText(int row) const;
    QString toolTip(int row) const;
    QString sortText(int row, Column column) const;
    bool isBlockedRow(int row) const;
    // Old name of a blocked included item in the same folder when the conflicts of row all
    // come from such names, else empty.
    QString blockingOldName(int row) const;
    // Reorders items, preview rows and persistent indexes: newOrder[i] = old row of new row i.
    void applyOrder(const QList<int> &newOrder);
    void listWasChanged();

    QList<ctrm::Item> m_items;
    ctrm::PreviewResult m_preview; // items parallel to m_items
    QHash<ctrm::ItemId, FileInfo> m_fileInfo;
    QHash<ctrm::ItemId, PostRunState> m_postRun;
    QSet<QString> m_paths;                               // canonical paths of the items
    QHash<ctrm::ItemId, QString> m_canonical;            // item -> canonical path
    mutable std::optional<QSet<QString>> m_blockedPaths; // paths of blocked rows (cache)
    QStringList m_ruleNames;
    bool m_portable = true;
    ctrm::ItemId m_nextId = 1;
    quint64 m_listVersion = 0;
    int m_sortedColumn = -1;
    Qt::SortOrder m_sortedOrder = Qt::AscendingOrder;
};

} // namespace ctrm::app
