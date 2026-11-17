// SPDX-License-Identifier: GPL-3.0-or-later
#include "rulelistmodel.h"

#include "rules/rulesummary.h"

#include <QMimeData>

#include <algorithm>

namespace ctrm::app {

namespace {

const QString kRowMimeType = QStringLiteral("application/x-ctrm-rule-row");

} // namespace

RuleListModel::RuleListModel(QObject *parent)
    : QAbstractListModel(parent)
{}

void RuleListModel::setChain(const RuleChain &chain)
{
    beginResetModel();
    m_chain = chain;
    endResetModel();
    emit chainChanged();
}

const RuleChain &RuleListModel::chain() const
{
    return m_chain;
}

RuleChain &RuleListModel::chain()
{
    return m_chain;
}

int RuleListModel::insertRule(int row, std::unique_ptr<Rule> rule)
{
    Q_ASSERT(rule);
    row = std::clamp(row, 0, m_chain.size());
    beginInsertRows({}, row, row);
    m_chain.insert(row, std::move(rule));
    endInsertRows();
    positionsChanged(row + 1);
    emit chainChanged();
    return row;
}

void RuleListModel::removeRule(int row)
{
    if (!isValidRow(row))
        return;
    beginRemoveRows({}, row, row);
    m_chain.take(row);
    endRemoveRows();
    positionsChanged(row);
    emit chainChanged();
}

void RuleListModel::duplicateRule(int row)
{
    if (!isValidRow(row))
        return;
    beginInsertRows({}, row + 1, row + 1);
    m_chain.duplicate(row);
    endInsertRows();
    positionsChanged(row + 2);
    emit chainChanged();
}

void RuleListModel::moveRule(int from, int to)
{
    if (!isValidRow(from) || !isValidRow(to) || from == to)
        return;
    // Qt gives the destination as the row before which the rule goes, in the old order.
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_chain.move(from, to);
    endMoveRows();
    const int first = std::min(from, to);
    const int last = std::max(from, to);
    emit dataChanged(index(first), index(last));
    emit chainChanged();
}

void RuleListModel::setEnabled(int row, bool enabled)
{
    if (!isValidRow(row) || m_chain.at(row)->enabled == enabled)
        return;
    m_chain.at(row)->enabled = enabled;
    emit dataChanged(index(row), index(row));
    emit chainChanged();
}

void RuleListModel::setChainOptions(const ChainOptions &o)
{
    if (m_chain.options == o)
        return;
    m_chain.options = o;
    emit chainChanged();
}

void RuleListModel::ruleChanged(int row)
{
    if (!isValidRow(row))
        return;
    emit dataChanged(index(row), index(row));
    emit chainChanged();
}

void RuleListModel::setNumberedCount(int n)
{
    n = std::max(1, n);
    if (n == m_numberedCount)
        return;
    m_numberedCount = n;
    if (rowCount() > 0)
        emit dataChanged(index(0), index(rowCount() - 1), {SummaryRole});
}

int RuleListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_chain.size();
}

QVariant RuleListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Rule &rule = *m_chain.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return QStringLiteral("%1 %2").arg(index.row() + 1).arg(ruleDisplayName(rule.type()));
    case Qt::CheckStateRole:
        return rule.enabled ? Qt::Checked : Qt::Unchecked;
    case SummaryRole:
        return ruleSummary(rule, m_numberedCount);
    case TypeRole:
        return rule.type();
    case HasErrorRole:
        return rule.problem().has_value();
    case PositionRole:
        return index.row() + 1;
    default:
        return {};
    }
}

bool RuleListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::CheckStateRole ||
        !checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return false;
    setEnabled(index.row(), value.value<Qt::CheckState>() == Qt::Checked);
    return true;
}

Qt::ItemFlags RuleListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable |
           Qt::ItemIsDragEnabled | Qt::ItemNeverHasChildren;
}

QHash<int, QByteArray> RuleListModel::roleNames() const
{
    QHash<int, QByteArray> names = QAbstractListModel::roleNames();
    names.insert(SummaryRole, "summary");
    names.insert(TypeRole, "type");
    names.insert(HasErrorRole, "hasError");
    names.insert(PositionRole, "position");
    return names;
}

bool RuleListModel::moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                             const QModelIndex &destinationParent, int destinationChild)
{
    if (sourceParent.isValid() || destinationParent.isValid() || count != 1 ||
        !isValidRow(sourceRow) || destinationChild < 0 || destinationChild > m_chain.size())
        return false;
    // destinationChild is the row before which the rule goes, in the old order.
    const int to = destinationChild > sourceRow ? destinationChild - 1 : destinationChild;
    if (to == sourceRow)
        return false;
    moveRule(sourceRow, to);
    return true;
}

Qt::DropActions RuleListModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

Qt::DropActions RuleListModel::supportedDragActions() const
{
    return Qt::MoveAction;
}

QStringList RuleListModel::mimeTypes() const
{
    return {kRowMimeType};
}

QMimeData *RuleListModel::mimeData(const QModelIndexList &indexes) const
{
    if (indexes.size() != 1 || !isValidRow(indexes.first().row()))
        return nullptr;
    auto *data = new QMimeData;
    data->setData(kRowMimeType, QByteArray::number(indexes.first().row()));
    return data;
}

bool RuleListModel::canDropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                                    int column, const QModelIndex &parent) const
{
    Q_UNUSED(column);
    Q_UNUSED(parent);
    return action == Qt::MoveAction && draggedRow(data) >= 0 && row <= m_chain.size();
}

bool RuleListModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                                 const QModelIndex &parent)
{
    if (!canDropMimeData(data, action, row, column, parent))
        return false;
    // A drop on an item puts the rule before it; a drop after the end gives row -1.
    int destination = row;
    if (destination < 0)
        destination = parent.isValid() ? parent.row() : m_chain.size();
    moveRows({}, draggedRow(data), 1, {}, destination);
    return false;
}

bool RuleListModel::isValidRow(int row) const
{
    return row >= 0 && row < m_chain.size();
}

void RuleListModel::positionsChanged(int first)
{
    if (first < m_chain.size())
        emit dataChanged(index(first), index(m_chain.size() - 1));
}

int RuleListModel::draggedRow(const QMimeData *data) const
{
    if (!data || !data->hasFormat(kRowMimeType))
        return -1;
    bool ok = false;
    const int row = data->data(kRowMimeType).toInt(&ok);
    return ok && isValidRow(row) ? row : -1;
}

} // namespace ctrm::app
