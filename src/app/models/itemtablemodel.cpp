// SPDX-License-Identifier: GPL-3.0-or-later
#include "models/itemtablemodel.h"

#include "fs/itemsource.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QIODevice>
#include <QLocale>
#include <QMimeData>

#include <algorithm>
#include <numeric>

namespace ctrm::app {

namespace {

bool isExistsCode(DiagCode c)
{
    return c == DiagCode::ExistsInFolder || c == DiagCode::ExistsIgnoringCase ||
           c == DiagCode::ExistsNormalization;
}

ItemPreview unchangedPreview(const Item &item)
{
    ItemPreview p;
    p.newName = item.name;
    p.included = item.included;
    return p;
}

} // namespace

ItemTableModel::ItemTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{}

QList<Item> ItemTableModel::items() const
{
    return m_items;
}

int ItemTableModel::addItems(QList<Item> items)
{
    QList<Item> fresh;
    int duplicates = 0;
    for (Item &item : items) {
        const QString canonical = canonicalItemPath(item.path());
        if (m_paths.contains(canonical)) {
            ++duplicates;
            continue;
        }
        m_paths.insert(canonical);
        item.id = m_nextId++;
        m_canonical.insert(item.id, canonical);
        const QFileInfo info(item.path());
        FileInfo fi;
        if (!item.isFolder && info.exists())
            fi.size = info.size();
        if (info.exists())
            fi.modified = info.lastModified();
        m_fileInfo.insert(item.id, fi);
        fresh.append(item);
    }
    if (fresh.isEmpty())
        return duplicates;

    const int first = int(m_items.size());
    beginInsertRows({}, first, first + int(fresh.size()) - 1);
    for (const Item &item : std::as_const(fresh)) {
        m_items.append(item);
        m_preview.items.append(unchangedPreview(item));
    }
    endInsertRows();
    m_sortedColumn = -1;
    listWasChanged();
    return duplicates;
}

void ItemTableModel::removeItems(const QList<ItemId> &ids)
{
    const QSet<ItemId> remove(ids.cbegin(), ids.cend());
    // Ranges of adjacent rows to remove, from the end.
    QList<std::pair<int, int>> ranges;
    for (int row = int(m_items.size()) - 1; row >= 0; --row) {
        if (!remove.contains(m_items[row].id))
            continue;
        if (!ranges.isEmpty() && ranges.last().first == row + 1)
            ranges.last().first = row;
        else
            ranges.append({row, row});
    }
    if (ranges.isEmpty())
        return;

    auto forget = [this](int row) {
        const ItemId id = m_items[row].id;
        m_paths.remove(m_canonical.take(id));
        m_fileInfo.remove(id);
        m_postRun.remove(id);
    };
    // Many ranges: one reset is faster than many row removals in the views.
    constexpr int kMaxRanges = 64;
    if (ranges.size() > kMaxRanges) {
        beginResetModel();
        QList<Item> items;
        QList<ItemPreview> previews;
        for (int row = 0; row < m_items.size(); ++row) {
            if (remove.contains(m_items[row].id)) {
                forget(row);
            } else {
                items.append(m_items[row]);
                previews.append(m_preview.items[row]);
            }
        }
        m_items = items;
        m_preview.items = previews;
        endResetModel();
    } else {
        for (const auto &[first, last] : std::as_const(ranges)) {
            beginRemoveRows({}, first, last);
            for (int r = first; r <= last; ++r)
                forget(r);
            m_items.remove(first, last - first + 1);
            m_preview.items.remove(first, last - first + 1);
            endRemoveRows();
        }
    }
    listWasChanged();
}

void ItemTableModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    m_preview = {};
    m_fileInfo.clear();
    m_postRun.clear();
    m_paths.clear();
    m_canonical.clear();
    m_sortedColumn = -1;
    endResetModel();
    listWasChanged();
}

void ItemTableModel::setIncluded(const QList<ItemId> &ids, bool included)
{
    const QSet<ItemId> set(ids.cbegin(), ids.cend());
    int firstRow = -1;
    int lastRow = -1;
    for (int row = 0; row < m_items.size(); ++row) {
        Item &item = m_items[row];
        if (!set.contains(item.id) || item.included == included)
            continue;
        item.included = included;
        m_preview.items[row].included = included;
        if (firstRow < 0)
            firstRow = row;
        lastRow = row;
    }
    if (firstRow < 0)
        return;
    listWasChanged();
    emit dataChanged(index(firstRow, 0), index(lastRow, ColumnCount - 1));
}

void ItemTableModel::moveRows(const QList<int> &rows, int destinationRow)
{
    const int count = int(m_items.size());
    QList<int> moving;
    for (int r : rows) {
        if (r >= 0 && r < count && !moving.contains(r))
            moving.append(r);
    }
    if (moving.isEmpty())
        return;
    std::sort(moving.begin(), moving.end());
    destinationRow = std::clamp(destinationRow, 0, count);

    QList<int> order;
    order.reserve(count);
    const QSet<int> movingSet(moving.cbegin(), moving.cend());
    for (int r = 0; r <= count; ++r) {
        if (r == destinationRow)
            order += moving;
        if (r < count && !movingSet.contains(r))
            order.append(r);
    }
    QList<int> identity(count);
    std::iota(identity.begin(), identity.end(), 0);
    if (order == identity)
        return;
    m_sortedColumn = -1;
    applyOrder(order);
    listWasChanged();
}

void ItemTableModel::sortOnce(Column column, Qt::SortOrder order)
{
    const int count = int(m_items.size());
    QList<int> rows(count);
    std::iota(rows.begin(), rows.end(), 0);
    auto less = [&](int a, int b) {
        if (column == Size || column == Modified) {
            const FileInfo &fa = m_fileInfo[m_items[a].id];
            const FileInfo &fb = m_fileInfo[m_items[b].id];
            return column == Size ? fa.size < fb.size : fa.modified < fb.modified;
        }
        if (column == Include)
            return m_items[a].included < m_items[b].included;
        return naturalLess(sortText(a, column), sortText(b, column));
    };
    if (order == Qt::AscendingOrder)
        std::stable_sort(rows.begin(), rows.end(), less);
    else
        std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) { return less(b, a); });
    m_sortedColumn = column;
    m_sortedOrder = order;
    applyOrder(rows);
    // A sort is a change of the list order, also when no row moves.
    ++m_listVersion;
    m_postRun.clear();
    emit listChanged();
}

int ItemTableModel::sortedColumn() const
{
    return m_sortedColumn;
}

Qt::SortOrder ItemTableModel::sortedOrder() const
{
    return m_sortedOrder;
}

void ItemTableModel::setPreview(const PreviewResult &preview)
{
    if (preview.cancelled || preview.items.size() != m_items.size())
        return;
    m_preview = preview;
    m_blockedPaths.reset();
    if (!m_items.isEmpty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
}

const PreviewResult &ItemTableModel::preview() const
{
    return m_preview;
}

void ItemTableModel::setRuleNames(const QStringList &names)
{
    m_ruleNames = names;
    if (!m_items.isEmpty())
        emit dataChanged(index(0, Message), index(rowCount() - 1, Message));
}

void ItemTableModel::setPortableNames(bool portable)
{
    m_portable = portable;
    if (!m_items.isEmpty())
        emit dataChanged(index(0, Message), index(rowCount() - 1, Message));
}

void ItemTableModel::setPostRun(const QHash<ItemId, PostRunState> &states)
{
    m_postRun = states;
    if (!m_items.isEmpty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
}

quint64 ItemTableModel::listVersion() const
{
    return m_listVersion;
}

int ItemTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

int ItemTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

const ItemPreview &ItemTableModel::previewAt(int row) const
{
    return m_preview.items[row];
}

bool ItemTableModel::isBlockedRow(int row) const
{
    const ItemPreview &p = previewAt(row);
    return m_items[row].included && !p.skipped && isBlocking(p.status);
}

QString ItemTableModel::blockingOldName(int row) const
{
    const ItemPreview &p = previewAt(row);
    if (p.status != Status::Conflict || p.problems.isEmpty())
        return {};
    if (!m_blockedPaths) {
        m_blockedPaths.emplace();
        for (int r = 0; r < m_items.size(); ++r) {
            if (isBlockedRow(r))
                m_blockedPaths->insert(m_items[r].path());
        }
    }
    // The diagnostic text is the exact name on disk, so an exact compare finds the item.
    // The row's own old name is free for it, so a conflict is never with the row itself.
    const Item &item = m_items[row];
    QString first;
    for (const Diagnostic &d : p.problems) {
        if (!isExistsCode(d.code) || d.text == item.name ||
            !m_blockedPaths->contains(item.folder + QLatin1Char('/') + d.text))
            return {};
        if (first.isEmpty())
            first = d.text;
    }
    return first;
}

QString ItemTableModel::messageText(int row) const
{
    const auto post = m_postRun.constFind(m_items[row].id);
    if (post != m_postRun.cend() && post->state != PostRun::None)
        return postRunText(*post);
    const ItemPreview &p = previewAt(row);
    if (!m_items[row].included)
        return statusText(p, m_portable);
    if (isBlocking(p.status) && !p.problems.isEmpty()) {
        const QString blocker = blockingOldName(row);
        if (!blocker.isEmpty())
            return blockedOldNameText(blocker);
        return diagnosticText(p.problems.first(), m_portable, m_ruleNames);
    }
    return statusText(p, m_portable);
}

QString ItemTableModel::toolTip(int row) const
{
    const ItemPreview &p = previewAt(row);
    QStringList lines = {messageText(row)};
    if (m_items[row].included) {
        for (int i = 1; i < p.problems.size(); ++i)
            lines.append(diagnosticText(p.problems[i], m_portable, m_ruleNames));
        for (const Diagnostic &d : p.warnings) {
            const QString t = diagnosticText(d, m_portable, m_ruleNames);
            if (!lines.contains(t))
                lines.append(t);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString ItemTableModel::sortText(int row, Column column) const
{
    const Item &item = m_items[row];
    switch (column) {
    case OldName:
        return item.name;
    case NewName:
        return item.included ? previewAt(row).newName : item.name;
    case Message:
    case State:
        return messageText(row);
    case Folder:
        return item.folder;
    default:
        return {};
    }
}

QVariant ItemTableModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const int row = index.row();
    const Item &item = m_items[row];
    const ItemPreview &p = previewAt(row);

    switch (role) {
    case StatusRole:
        return int(p.status);
    case ItemIdRole:
        return QVariant::fromValue<qulonglong>(item.id);
    case IsBlockingRole:
        return isBlockedRow(row);
    case HasWarningRole:
        return item.included && !p.skipped && !isBlocking(p.status) && !p.warnings.isEmpty();
    case PostRunRole:
        return int(m_postRun.value(item.id).state);
    case IsExcludedRole:
        return !item.included;
    default:
        break;
    }

    const auto column = Column(index.column());
    if (role == Qt::CheckStateRole && column == Include)
        return item.included ? Qt::Checked : Qt::Unchecked;
    if (role == Qt::ToolTipRole) {
        switch (column) {
        case OldName:
            return item.name + QLatin1Char('\n') + toolTip(row);
        case NewName:
            return sortText(row, NewName) + QLatin1Char('\n') + toolTip(row);
        case Folder:
            return QDir::toNativeSeparators(item.folder);
        default:
            return toolTip(row);
        }
    }
    if (role == Qt::FontRole && column == OldName && item.included && p.status == Status::Missing) {
        QFont f;
        f.setItalic(true);
        return f;
    }
    if (role != Qt::DisplayRole)
        return {};

    switch (column) {
    case OldName:
    case NewName:
    case Message:
        return sortText(row, column);
    case Folder:
        return QDir::toNativeSeparators(item.folder);
    case Size: {
        const qint64 size = m_fileInfo.value(item.id).size;
        return size >= 0 ? QLocale().formattedDataSize(size) : QString();
    }
    case Modified: {
        const QDateTime t = m_fileInfo.value(item.id).modified;
        return t.isValid() ? QLocale().toString(t, QLocale::ShortFormat) : QString();
    }
    default:
        return {};
    }
}

bool ItemTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid) ||
        index.column() != Include || role != Qt::CheckStateRole)
        return false;
    const bool included = value.value<Qt::CheckState>() == Qt::Checked;
    setIncluded({m_items[index.row()].id}, included);
    return true;
}

QVariant ItemTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
        return {};
    switch (Column(section)) {
    case Include:
        return role == Qt::ToolTipRole ? tr("Include") : QString();
    case State:
        return role == Qt::ToolTipRole ? tr("State") : QString();
    case OldName:
        return tr("Old name");
    case NewName:
        return tr("New name");
    case Message:
        return tr("Message");
    case Folder:
        return tr("Folder");
    case Size:
        return tr("Size");
    case Modified:
        return tr("Date modified");
    case ColumnCount:
        break;
    }
    return {};
}

Qt::ItemFlags ItemTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled |
                      Qt::ItemIsDropEnabled | Qt::ItemNeverHasChildren;
    if (index.column() == Include)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

Qt::DropActions ItemTableModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

Qt::DropActions ItemTableModel::supportedDragActions() const
{
    return Qt::MoveAction;
}

QStringList ItemTableModel::mimeTypes() const
{
    return {QString::fromLatin1(kRowsMimeType)};
}

QMimeData *ItemTableModel::mimeData(const QModelIndexList &indexes) const
{
    QList<int> rows;
    for (const QModelIndex &i : indexes) {
        if (i.isValid() && !rows.contains(i.row()))
            rows.append(i.row());
    }
    std::sort(rows.begin(), rows.end());
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << m_listVersion << rows;
    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(kRowsMimeType), bytes);
    return mime;
}

bool ItemTableModel::canDropMimeData(const QMimeData *data, Qt::DropAction action, int, int,
                                     const QModelIndex &) const
{
    return action == Qt::MoveAction && data && data->hasFormat(QString::fromLatin1(kRowsMimeType));
}

bool ItemTableModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                                  const QModelIndex &parent)
{
    if (!canDropMimeData(data, action, row, column, parent))
        return false;
    QDataStream in(data->data(QString::fromLatin1(kRowsMimeType)));
    quint64 version = 0;
    QList<int> rows;
    in >> version >> rows;
    // Rows from an older list (or another model) are not valid now.
    if (in.status() != QDataStream::Ok || version != m_listVersion)
        return false;
    int destination = row;
    if (destination < 0)
        destination = parent.isValid() ? parent.row() : rowCount();
    moveRows(rows, destination);
    // The model did the move: the view must not remove the source rows.
    return false;
}

void ItemTableModel::applyOrder(const QList<int> &newOrder)
{
    const int count = int(m_items.size());
    Q_ASSERT(newOrder.size() == count);
    emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);

    QList<int> newRowOf(count);
    for (int n = 0; n < count; ++n)
        newRowOf[newOrder[n]] = n;
    QList<Item> items;
    QList<ItemPreview> previews;
    items.reserve(count);
    previews.reserve(count);
    for (int n = 0; n < count; ++n) {
        items.append(m_items[newOrder[n]]);
        previews.append(m_preview.items[newOrder[n]]);
    }
    m_items = items;
    m_preview.items = previews;

    const QModelIndexList from = persistentIndexList();
    QModelIndexList to;
    to.reserve(from.size());
    for (const QModelIndex &i : from)
        to.append(index(newRowOf[i.row()], i.column()));
    changePersistentIndexList(from, to);
    emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
}

void ItemTableModel::listWasChanged()
{
    ++m_listVersion;
    m_blockedPaths.reset();
    if (!m_postRun.isEmpty()) {
        m_postRun.clear();
        if (!m_items.isEmpty())
            emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
    }
    emit listChanged();
}

} // namespace ctrm::app
