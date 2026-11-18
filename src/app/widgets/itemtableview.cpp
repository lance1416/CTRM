// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/itemtableview.h"

#include "models/itemfilterproxy.h"
#include "models/itemtablemodel.h"
#include "text/messages.h"
#include "text/plural.h"
#include "theme/colors.h"
#include "widgets/emptystate.h"
#include "widgets/itemdelegate.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QHeaderView>
#include <QIdentityProxyModel>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyleHints>
#include <QUrl>

#include <algorithm>

namespace ctrm::app {

namespace {

// C10: the narrow icon columns (Include, State) draw no section separator, as in Finder. The
// style draws the separator on the right edge of the section rectangle, so these sections paint
// a wider rectangle, clipped to their own.
class ItemHeaderView : public QHeaderView
{
public:
    explicit ItemHeaderView(QWidget *parent)
        : QHeaderView(Qt::Horizontal, parent)
    {}

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override
    {
        if (logicalIndex != ItemTableModel::Include && logicalIndex != ItemTableModel::State) {
            QHeaderView::paintSection(painter, rect, logicalIndex);
            return;
        }
        constexpr int kSeparatorRoom = 4; // more than the separator at each scale
        painter->save();
        painter->setClipRect(rect, Qt::IntersectClip);
        QHeaderView::paintSection(painter, rect.adjusted(0, 0, kSeparatorRoom, 0), logicalIndex);
        painter->restore();
    }
};

// Adds the screen reader text of the row (ui-design 14): state, old name, "will become", new
// name, message.
class RowTextModel : public QIdentityProxyModel
{
public:
    using QIdentityProxyModel::QIdentityProxyModel;

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role != Qt::AccessibleTextRole || index.column() == ItemTableModel::Include)
            return QIdentityProxyModel::data(index, role);
        return ItemTableView::tr("%1, %2, will become %3, %4")
                .arg(ItemDelegate::rowStateName(index),
                     index.siblingAtColumn(ItemTableModel::OldName).data().toString(),
                     index.siblingAtColumn(ItemTableModel::NewName).data().toString(),
                     index.siblingAtColumn(ItemTableModel::Message)
                             .data(ItemTableModel::MessageRole)
                             .toString());
    }
};

bool isProblemRow(const QModelIndex &index)
{
    return index.data(ItemTableModel::IsBlockingRole).toBool() ||
           PostRun(index.data(ItemTableModel::PostRunRole).toInt()) == PostRun::Failed;
}

constexpr int kNameWidth = 240;  // the width of Old name and New name before the first resize
constexpr int kNamePercent = 30; // the share of the table of each name column (mockup)

constexpr int kOptionalColumns[] = {ItemTableModel::Message, ItemTableModel::Folder,
                                    ItemTableModel::Size, ItemTableModel::Modified};

} // namespace

ItemTableView::ItemTableView(ItemTableModel &model, ItemFilterProxy &proxy, QWidget *parent)
    : QTableView(parent)
    , m_model(model)
    , m_proxy(proxy)
    , m_rowText(new RowTextModel(this))
    , m_delegate(new ItemDelegate(this))
    , m_columnsMenu(new QMenu(this))
{
    setObjectName(QStringLiteral("itemTable"));
    if (m_proxy.sourceModel() != &m_model)
        m_proxy.setSourceModel(&m_model);
    m_rowText->setSourceModel(&m_proxy);
    setModel(m_rowText);
    setItemDelegate(m_delegate);

    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setTextElideMode(Qt::ElideMiddle);
    setWordWrap(false);
    setShowGrid(false);
    setSortingEnabled(false);
    setTabKeyNavigation(false);

    // Rows move by drag (model mime type); files from outside are added (pathsDropped).
    setDragEnabled(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDragDropOverwriteMode(false);
    setDropIndicatorShown(true);

    QHeaderView *vertical = verticalHeader();
    vertical->hide();
    vertical->setSectionResizeMode(QHeaderView::Fixed);
    updateRowHeight();

    setHorizontalHeader(new ItemHeaderView(this));
    QHeaderView *header = horizontalHeader();
    header->setSectionsClickable(true);
    header->setHighlightSections(false);
    header->setStretchLastSection(true);
    header->setSectionResizeMode(QHeaderView::Interactive);
    // Same alignment as the cells, semibold (mockup ".items th", audit M14).
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont headerFont = header->font();
    headerFont.setWeight(QFont::DemiBold);
    header->setFont(headerFont);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    const int iconColumn = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this) +
                           2 * style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, this) +
                           8;
    header->resizeSection(ItemTableModel::Include, iconColumn);
    header->resizeSection(ItemTableModel::State, iconColumn);
    header->resizeSection(ItemTableModel::OldName, kNameWidth);
    header->resizeSection(ItemTableModel::NewName, kNameWidth);
    // Only a drag or a double click on the header is a change by the user. Qt also resizes a
    // name column when it shows or hides another column.
    header->viewport()->installEventFilter(this);
    connect(header, &QHeaderView::sectionResized, this, [this, header](int column) {
        if (m_fittingNames || !m_headerPressed ||
            (column != ItemTableModel::OldName && column != ItemTableModel::NewName))
            return;
        // The header stretches the last visible column; that is not a change by the user.
        int last = header->count() - 1;
        while (last >= 0 && header->isSectionHidden(header->logicalIndex(last)))
            --last;
        if (header->stretchLastSection() && last >= 0 && header->logicalIndex(last) == column)
            return;
        m_namesSetByUser = true;
    });
    connect(header, &QHeaderView::sectionClicked, this, &ItemTableView::sortByColumn);
    connect(header, &QHeaderView::customContextMenuRequested, this,
            [this, header](const QPoint &pos) {
                syncColumnActions();
                m_columnsMenu->popup(header->viewport()->mapToGlobal(pos));
            });

    // Right-click the header to show or hide columns (ui-design 7.1).
    m_columnsMenu->setObjectName(QStringLiteral("columnsMenu"));
    for (const int column : kOptionalColumns) {
        QAction *action = m_columnsMenu->addAction(
                m_model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
        action->setCheckable(true);
        action->setData(column);
        connect(action, &QAction::toggled, this, [this, column](bool shown) {
            m_userColumns.insert(column);
            setColumnHidden(column, !shown);
            fitNameColumns();
        });
    }
    setColumnHidden(ItemTableModel::Size, true);
    setColumnHidden(ItemTableModel::Modified, true);

    connect(&m_model, &QAbstractItemModel::rowsInserted, this, &ItemTableView::updateAutoColumns);
    connect(&m_model, &QAbstractItemModel::rowsRemoved, this, &ItemTableView::updateAutoColumns);
    connect(&m_model, &QAbstractItemModel::modelReset, this, &ItemTableView::updateAutoColumns);
    connect(&m_model, &QAbstractItemModel::dataChanged, this, &ItemTableView::updateAutoColumns);
    connect(&m_model, &QAbstractItemModel::layoutChanged, this, &ItemTableView::updateAutoColumns);
    connect(&m_model, &ItemTableModel::listChanged, this, &ItemTableView::updateSortIndicator);
    connect(&m_model, &QAbstractItemModel::layoutChanged, this,
            &ItemTableView::updateSortIndicator);
    connect(&m_model, &QAbstractItemModel::modelReset, this, &ItemTableView::updateSortIndicator);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            &ItemTableView::updateColors);

    updateColors();
    updateAutoColumns();
    updateSortIndicator();
}

QList<ctrm::ItemId> ItemTableView::selectedIds() const
{
    QList<ctrm::ItemId> ids;
    for (const QModelIndex &index : selectedRowIndexes())
        ids.append(index.data(ItemTableModel::ItemIdRole).toULongLong());
    return ids;
}

QModelIndexList ItemTableView::selectedRowIndexes() const
{
    QModelIndexList rows = selectionModel()->selectedRows(ItemTableModel::OldName);
    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    return rows;
}

void ItemTableView::setHighlightChanged(bool on)
{
    m_delegate->setHighlightChanged(on);
    viewport()->update();
}

QString ItemTableView::dropText() const
{
    if (m_dropCount <= 0)
        return {};
    return numerus(tr("Drop to add %n item(s)", nullptr, m_dropCount), m_dropCount);
}

void ItemTableView::selectNextProblem(bool forward)
{
    QAbstractItemModel *m = model();
    const int count = m->rowCount();
    if (count == 0)
        return;
    const int current = currentIndex().isValid() ? currentIndex().row() : (forward ? -1 : count);
    for (int step = 1; step <= count; ++step) {
        const int row = ((forward ? current + step : current - step) % count + count) % count;
        const QModelIndex index = m->index(row, ItemTableModel::OldName);
        if (!isProblemRow(index))
            continue;
        selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect |
                                                         QItemSelectionModel::Rows);
        scrollTo(index);
        return;
    }
}

void ItemTableView::sortByColumn(int column)
{
    // A click sorts the list one time (ui-design 7.1). Include and State have no sort.
    // The header moves its own arrow on each click, so set it from the model again.
    if (column == ItemTableModel::Include || column == ItemTableModel::State) {
        updateSortIndicator();
        return;
    }
    const bool again =
            m_model.sortedColumn() == column && m_model.sortedOrder() == Qt::AscendingOrder;
    m_model.sortOnce(ItemTableModel::Column(column),
                     again ? Qt::DescendingOrder : Qt::AscendingOrder);
    updateSortIndicator();
}

void ItemTableView::updateSortIndicator()
{
    QHeaderView *header = horizontalHeader();
    const int column = m_model.sortedColumn();
    header->setSortIndicatorShown(column >= 0);
    if (column >= 0)
        header->setSortIndicator(column, m_model.sortedOrder());
}

void ItemTableView::updateAutoColumns()
{
    const QList<ctrm::Item> items = m_model.items();
    if (!m_userColumns.contains(ItemTableModel::Folder)) {
        bool manyFolders = false;
        for (const ctrm::Item &item : items) {
            if (item.folder != items.first().folder) {
                manyFolders = true;
                break;
            }
        }
        setColumnHidden(ItemTableModel::Folder, !manyFolders);
    }
    // The Message column shows when the list has items, so the table does not jump on each edit
    // (audit S02). Only the rows that need text have it.
    if (!m_userColumns.contains(ItemTableModel::Message))
        setColumnHidden(ItemTableModel::Message, items.isEmpty());
    syncColumnActions();
    fitNameColumns();
}

void ItemTableView::syncColumnActions()
{
    for (QAction *action : m_columnsMenu->actions()) {
        const QSignalBlocker blocker(action);
        action->setChecked(!isColumnHidden(action->data().toInt()));
    }
}

void ItemTableView::updateColors()
{
    m_delegate->setColors(CtrmColors::fromPalette(palette()));
    viewport()->update();
}

void ItemTableView::updateRowHeight()
{
    // The row-height token: one text line + 8 (design-tokens).
    const int height = fontMetrics().height() + 8;
    QHeaderView *vertical = verticalHeader();
    vertical->setMinimumSectionSize(qMin(vertical->minimumSectionSize(), height));
    vertical->setDefaultSectionSize(height);
}

bool ItemTableView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == horizontalHeader()->viewport()) {
        // Only the left button resizes. A right click opens the columns menu, which can take
        // the release, so it does not set the flag.
        if ((event->type() == QEvent::MouseButtonPress ||
             event->type() == QEvent::MouseButtonDblClick) &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
            m_headerPressed = true;
        else if (event->type() == QEvent::MouseButtonRelease)
            m_headerPressed = false;
    }
    return QTableView::eventFilter(watched, event);
}

void ItemTableView::resizeEvent(QResizeEvent *event)
{
    QTableView::resizeEvent(event);
    fitNameColumns();
}

// Old name and New name have the same width (audit M14): 30% of the table each (mockup), and
// the last column gets the rest. In a narrow table (minimum window, wide rule panel) they share
// the width after the columns before them, so both still show (ui-design 4.4). When a name
// column is the last visible column, the names share all of that width. A width that the user
// sets stays.
void ItemTableView::fitNameColumns()
{
    if (m_namesSetByUser || viewport()->width() <= 0)
        return;
    QHeaderView *header = horizontalHeader();
    const int before = std::min(header->sectionPosition(ItemTableModel::OldName),
                                header->sectionPosition(ItemTableModel::NewName));
    int last = header->count() - 1;
    while (last >= 0 && header->isSectionHidden(header->logicalIndex(last)))
        --last;
    const int lastColumn = last >= 0 ? header->logicalIndex(last) : -1;
    const bool nameIsLast =
            lastColumn == ItemTableModel::OldName || lastColumn == ItemTableModel::NewName;
    const int share = (viewport()->width() - before) / 2;
    const int width = std::max(
            header->minimumSectionSize(),
            nameIsLast ? share : std::min(share, viewport()->width() * kNamePercent / 100));
    m_fittingNames = true;
    header->resizeSection(ItemTableModel::OldName, width);
    header->resizeSection(ItemTableModel::NewName, width);
    m_fittingNames = false;
}

void ItemTableView::changeEvent(QEvent *event)
{
    QTableView::changeEvent(event);
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
        updateColors();
        break;
    case QEvent::FontChange:
        updateRowHeight();
        break;
    default:
        break;
    }
}

void ItemTableView::keyPressEvent(QKeyEvent *event)
{
    // Space toggles Include on the selected rows (ui-design 7.5).
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier) {
        const QModelIndexList rows = selectedRowIndexes();
        if (!rows.isEmpty()) {
            QModelIndex lead = currentIndex();
            if (!lead.isValid() || !selectionModel()->isRowSelected(lead.row()))
                lead = rows.first();
            const bool include = lead.data(ItemTableModel::IsExcludedRole).toBool();
            m_model.setIncluded(selectedIds(), include);
            event->accept();
            return;
        }
    }
    QTableView::keyPressEvent(event);
}

void ItemTableView::copyNames(int column)
{
    QStringList names;
    for (const QModelIndex &index : selectedRowIndexes())
        names.append(index.siblingAtColumn(column).data().toString());
    QGuiApplication::clipboard()->setText(names.join(QLatin1Char('\n')));
}

void ItemTableView::contextMenuEvent(QContextMenuEvent *event)
{
    const QList<ctrm::ItemId> ids = selectedIds();
    if (ids.isEmpty())
        return;
    QMenu menu(this);
    menu.addAction(tr("Include"), this, [this, ids] { m_model.setIncluded(ids, true); });
    menu.addAction(tr("Exclude"), this, [this, ids] { m_model.setIncluded(ids, false); });
    menu.addAction(tr("Remove from List"), this, [this, ids] { m_model.removeItems(ids); });
    menu.addSeparator();
    // The folder of the item under the mouse, or of the first selected item.
    QModelIndex target = indexAt(viewport()->mapFromGlobal(event->globalPos()));
    if (!target.isValid() || !selectionModel()->isRowSelected(target.row()))
        target = selectedRowIndexes().first();
    const ctrm::ItemId id = target.data(ItemTableModel::ItemIdRole).toULongLong();
    menu.addAction(showInFileManagerText(), this, [this, id] {
        for (const ctrm::Item &item : m_model.items()) {
            if (item.id == id) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(item.folder));
                emit showInFileManager(id);
                return;
            }
        }
    });
    menu.addSeparator();
    menu.addAction(tr("Copy Old Name"), this, [this] { copyNames(ItemTableModel::OldName); });
    menu.addAction(tr("Copy New Name"), this, [this] { copyNames(ItemTableModel::NewName); });
    menu.exec(event->globalPos());
}

void ItemTableView::setDropCount(int count)
{
    if (m_dropCount == count)
        return;
    m_dropCount = count;
    viewport()->update();
}

void ItemTableView::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime->hasFormat(QString::fromLatin1(ItemTableModel::kRowsMimeType))) {
        const QStringList paths = localDropPaths(mime);
        if (!paths.isEmpty()) {
            setDropCount(int(paths.size()));
            event->setDropAction(Qt::CopyAction);
            event->accept();
            return;
        }
    }
    QTableView::dragEnterEvent(event);
}

void ItemTableView::dragMoveEvent(QDragMoveEvent *event)
{
    if (m_dropCount > 0) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    QTableView::dragMoveEvent(event);
}

void ItemTableView::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDropCount(0);
    QTableView::dragLeaveEvent(event);
}

void ItemTableView::dropEvent(QDropEvent *event)
{
    if (m_dropCount > 0) {
        setDropCount(0);
        const QStringList paths = localDropPaths(event->mimeData());
        event->setDropAction(Qt::CopyAction);
        event->accept();
        if (!paths.isEmpty())
            emit pathsDropped(paths);
        return;
    }
    QTableView::dropEvent(event);
}

void ItemTableView::paintEvent(QPaintEvent *event)
{
    QTableView::paintEvent(event);
    if (m_dropCount <= 0)
        return;
    // Drag-over state (ui-design 9.2): dashed Highlight border and the count.
    QPainter painter(viewport());
    paintDropFrame(painter, viewport()->rect(), palette(), dropText());
}

} // namespace ctrm::app
