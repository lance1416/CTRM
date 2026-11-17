// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rulechain.h"

#include <QAbstractListModel>

#include <memory>

namespace ctrm::app {

// The rules of the chain, one row for each rule. DisplayRole: "<position> <display name>";
// SummaryRole: the second line; CheckStateRole: on or off. Rows move with drag and drop.
class RuleListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { SummaryRole = Qt::UserRole + 1, TypeRole, HasErrorRole, PositionRole };

    explicit RuleListModel(QObject *parent = nullptr);

    void setChain(const ctrm::RuleChain &chain);
    const ctrm::RuleChain &chain() const;
    ctrm::RuleChain &chain(); // editors write into rules; call ruleChanged(row) after
    // row is clamped to 0..rowCount(). Returns the row of the new rule.
    int insertRule(int row, std::unique_ptr<ctrm::Rule> rule);
    void removeRule(int row);
    void duplicateRule(int row);     // the copy goes to row + 1
    void moveRule(int from, int to); // to is the final row
    void setEnabled(int row, bool enabled);
    void setChainOptions(const ctrm::ChainOptions &o);
    void ruleChanged(int row);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override;

    Qt::DropActions supportedDropActions() const override;
    Qt::DropActions supportedDragActions() const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    // Moves the row and returns false, so the view does not remove the source row.
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

signals:
    void chainChanged();

private:
    bool isValidRow(int row) const;
    void positionsChanged(int first);
    int draggedRow(const QMimeData *data) const;

    ctrm::RuleChain m_chain;
};

} // namespace ctrm::app
