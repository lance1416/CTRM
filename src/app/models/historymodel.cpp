// SPDX-License-Identifier: GPL-3.0-or-later
#include "models/historymodel.h"

#include "text/plural.h"
#include "theme/colors.h"
#include "theme/icons.h"

#include <QApplication>
#include <QDir>
#include <QLocale>

namespace ctrm::app {

HistoryModel::HistoryModel(QObject *parent)
    : QAbstractTableModel(parent)
{}

void HistoryModel::setBatches(const QList<BatchSummary> &batches)
{
    beginResetModel();
    m_batches = batches;
    endResetModel();
}

const BatchSummary &HistoryModel::batch(int row) const
{
    return m_batches.at(row);
}

int HistoryModel::rowOf(const QString &batchId) const
{
    for (int i = 0; i < m_batches.size(); ++i) {
        if (m_batches.at(i).batchId == batchId)
            return i;
    }
    return -1;
}

QString HistoryModel::whenText(const QDateTime &time, const QDateTime &now)
{
    if (!time.isValid())
        return tr("Unknown"); // a damaged journal whose file name has no time
    const QLocale locale = QLocale::system();
    const QDateTime local = time.toLocalTime();
    const QDate today = now.toLocalTime().date();
    const QString clock = locale.toString(local.time(), QLocale::ShortFormat);
    if (local.date() == today)
        return tr("Today %1").arg(clock);
    if (local.date() == today.addDays(-1))
        return tr("Yesterday %1").arg(clock);
    return tr("%1 %2").arg(locale.toString(local.date(), QStringLiteral("d MMM")), clock);
}

QString HistoryModel::displayPath(const QString &path)
{
    QString shown = path;
    // The home folder as "~", also when the path or the home folder has a symbolic link
    // (macOS: /var is /private/var).
    // Read once: the view asks for each cell, and canonicalPath() reads the disk.
    static const QStringList homes{QDir::homePath(), QDir(QDir::homePath()).canonicalPath()};
    for (const QString &home : homes) {
        if (home.isEmpty() || home == u'/')
            continue;
        if (shown == home) {
            shown = QStringLiteral("~");
            break;
        }
        if (shown.startsWith(home + u'/')) {
            shown = u'~' + shown.mid(home.size());
            break;
        }
    }
    return QDir::toNativeSeparators(shown);
}

QString HistoryModel::foldersText(const BatchSummary &s, const QList<BatchSummary> &all)
{
    if (!s.undoOf.isEmpty()) {
        for (const BatchSummary &other : all) {
            if (other.batchId == s.undoOf)
                return tr("Undo of %1").arg(whenText(other.started, QDateTime::currentDateTime()));
        }
        return tr("Undo of %1").arg(s.undoOf);
    }
    if (s.folders.size() == 1)
        return displayPath(s.folders.first());
    if (s.folders.isEmpty())
        return {};
    const int n = int(s.folders.size());
    return numerus(tr("%n folder(s)", nullptr, n), n);
}

QString HistoryModel::stateText(const BatchSummary &s)
{
    if (s.damaged)
        return tr("Damaged");
    QString text;
    switch (s.state) {
    case BatchState::InProgress:
        text = tr("In progress");
        break;
    case BatchState::Complete:
        text = tr("Complete");
        break;
    case BatchState::RolledBack:
        text = tr("Rolled back");
        break;
    case BatchState::Incomplete:
        text = tr("Incomplete");
        break;
    case BatchState::PartlyUndone:
        text = tr("Partly undone");
        break;
    case BatchState::Undone:
        text = tr("Undone");
        break;
    }
    if (s.failedCount > 0)
        text += tr(", %1 failed").arg(s.failedCount);
    return text;
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_batches.size());
}

int HistoryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const BatchSummary &s = m_batches.at(index.row());
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
        switch (index.column()) {
        case When: {
            return whenText(s.started, QDateTime::currentDateTime());
        }
        case Items:
            return s.damaged ? QString() : QLocale().toString(s.itemCount);
        case Folders:
            if (role == Qt::ToolTipRole && s.undoOf.isEmpty()) {
                QStringList shown;
                for (const QString &f : s.folders)
                    shown.append(displayPath(f));
                return shown.join(u'\n');
            }
            return foldersText(s, m_batches);
        case State:
            return stateText(s);
        default:
            return {};
        }
    }
    if (role == Qt::DecorationRole && index.column() == State) {
        const CtrmColors colors = CtrmColors::fromPalette(QApplication::palette());
        if (s.damaged)
            return stateIcon(StateIcon::Error, colors);
        if (s.state == BatchState::InProgress || s.state == BatchState::Incomplete)
            return stateIcon(StateIcon::Warning, colors);
        return {};
    }
    if (role == Qt::TextAlignmentRole && index.column() == Items)
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

QVariant HistoryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case When:
        return tr("When");
    case Items:
        return tr("Items");
    case Folders:
        return tr("Folder(s)");
    case State:
        return tr("State");
    default:
        return {};
    }
}

} // namespace ctrm::app
