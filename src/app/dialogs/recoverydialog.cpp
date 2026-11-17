// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs/recoverydialog.h"

#include "models/historymodel.h"
#include "text/messages.h"
#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/typography.h"
#include "widgets/buttonstyle.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace ctrm::app {

namespace {

constexpr int kTempRole = Qt::UserRole + 1; // the cell has a temporary name
constexpr int kVisibleRows = 5;

QString tempLine()
{
    return RecoveryDialog::tr("temporary name");
}

// A temporary name cell: the name, then "temporary name" in caption style and dimmed.
class OpenStepDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        if (!index.data(kTempRole).toBool()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString name = opt.text.section(u'\n', 0, 0);
        opt.text.clear();
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        const QRect r = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget)
                                .adjusted(2, 0, -2, 0);
        const QFont caption = typeStyle(TypeToken::Caption, opt.font);
        const int free =
                r.height() - QFontMetrics(opt.font).height() - QFontMetrics(caption).height();
        const int top = r.top() + (free / 2);
        painter->save();
        const bool selected = opt.state & QStyle::State_Selected;
        painter->setPen(opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        painter->setFont(opt.font);
        const QFontMetrics fm(opt.font);
        painter->drawText(QRect(r.left(), top, r.width(), fm.height()), Qt::AlignLeft,
                          fm.elidedText(name, Qt::ElideMiddle, r.width()));
        painter->setFont(caption);
        if (!selected)
            painter->setPen(opt.palette.color(QPalette::PlaceholderText));
        painter->drawText(
                QRect(r.left(), top + fm.height(), r.width(), QFontMetrics(caption).height()),
                Qt::AlignLeft, tempLine());
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        if (index.data(kTempRole).toBool()) {
            const QFont caption = typeStyle(TypeToken::Caption, option.font);
            size.setHeight(std::max(size.height(), QFontMetrics(option.font).height() +
                                                           QFontMetrics(caption).height() + 6));
        }
        return size;
    }
};

QString dateText(const QDateTime &time)
{
    return QLocale::system().toString(time.toLocalTime().date(), QStringLiteral("d MMM yyyy"));
}

QString timeText(const QDateTime &time)
{
    return QLocale::system().toString(time.toLocalTime().time(), QLocale::ShortFormat);
}

QStandardItem *nameItem(const QString &path, bool isTemp)
{
    const QString name = QFileInfo(path).fileName();
    auto *item = new QStandardItem(isTemp ? name + u'\n' + tempLine() : name);
    item->setData(isTemp, kTempRole);
    item->setToolTip(HistoryModel::displayPath(path));
    return item;
}

QPushButton *makeButton(const QString &text, const char *name, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(QLatin1StringView(name));
    b->setAutoDefault(false);
    return b;
}

} // namespace

QString RecoveryDialog::headingText()
{
    return tr("The last rename did not finish.");
}

QString RecoveryDialog::bodyText(const RecoveryInfo &info)
{
    if (info.damaged) {
        QString check;
        if (!info.folders.isEmpty()) {
            QStringList shown;
            for (const QString &folder : info.folders)
                shown.append(HistoryModel::displayPath(folder));
            check = tr("Check the item names in %1.").arg(shown.join(QStringLiteral(", ")));
        } else if (info.started.isValid()) {
            check = tr("Check the names of the items that you renamed on %1 at %2.")
                            .arg(dateText(info.started), timeText(info.started));
        } else {
            check = tr("Check the names of the items that you renamed last.");
        }
        return tr("CTRM cannot read the history file of this batch, so it cannot roll back the "
                  "batch.") +
               u' ' + check + u'\n' +
               tr("If you close this dialog, CTRM asks again at the next start.");
    }
    const QLocale locale = QLocale::system();
    return tr("CTRM stopped during a batch on %1 at %2.")
                   .arg(dateText(info.started), timeText(info.started)) +
           u'\n' +
           (info.total == 1
                    ? tr("%1 of 1 operation was done.").arg(info.done)
                    : tr("%1 of %2 operations were done.")
                              .arg(locale.toString(info.done), locale.toString(info.total))) +
           u'\n' +
           (info.doneItems == 1 ? tr("Roll back puts the 1 item back to its old name.")
                                : tr("Roll back puts the %1 items back to their old names.")
                                          .arg(locale.toString(info.doneItems)));
}

QString RecoveryDialog::openStepsLabelText(int count)
{
    if (count == 1)
        return tr("Check this item (1 rename not confirmed):");
    return tr("Check these items (%1 renames not confirmed):").arg(count);
}

QString RecoveryDialog::openStepsNoteText(int count)
{
    if (count == 1)
        return tr("CTRM started this rename, but did not confirm it. The item can have its old "
                  "name or its new name. Check the item in the folder before you click Mark as "
                  "Complete.");
    return tr("CTRM started these renames, but did not confirm them. Each item can have its old "
              "name or its new name. Check each item in the folder before you click Mark as "
              "Complete.");
}

QString RecoveryDialog::copyText(const QList<OpenStep> &steps)
{
    QString text;
    for (const OpenStep &s : steps)
        text += QDir::toNativeSeparators(s.from) + u'\t' + QDir::toNativeSeparators(s.to) + u'\n';
    return text;
}

RecoveryDialog::RecoveryDialog(const RecoveryInfo &info, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("recoveryDialog"));
    setWindowModality(Qt::WindowModal);
    setWindowTitle(QStringLiteral("CTRM"));

    const CtrmColors colors = CtrmColors::fromPalette(palette());
    auto *icon = new QLabel(this);
    const int iconSize = 2 * fontMetrics().height();
    icon->setPixmap(stateIcon(info.damaged ? StateIcon::Error : StateIcon::Warning, colors)
                            .pixmap(iconSize, iconSize));

    auto *heading = new QLabel(headingText(), this);
    heading->setObjectName(QStringLiteral("recoveryHeading"));
    heading->setFont(typeStyle(TypeToken::Heading, font()));
    auto *body = new QLabel(bodyText(info), this);
    body->setObjectName(QStringLiteral("recoveryBody"));
    body->setTextFormat(Qt::PlainText);
    body->setWordWrap(true);

    auto *text = new QVBoxLayout;
    text->addWidget(heading);
    text->addWidget(body);

    if (!info.openSteps.isEmpty()) {
        const int n = int(info.openSteps.size());
        auto *label = new QLabel(openStepsLabelText(n), this);
        label->setObjectName(QStringLiteral("openStepsLabel"));
        label->setFont(typeStyle(TypeToken::LabelStrong, font()));
        auto *copy = new QToolButton(this);
        copy->setObjectName(QStringLiteral("copyList"));
        copy->setAutoRaise(true);
        copy->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::EditCopy,
                                       toolIcon(ToolIcon::Duplicate, palette())));
        copy->setToolTip(tr("Copy List"));
        copy->setAccessibleName(tr("Copy List"));
        const QList<OpenStep> steps = info.openSteps;
        connect(copy, &QToolButton::clicked, this,
                [steps] { QApplication::clipboard()->setText(copyText(steps)); });
        auto *labelRow = new QHBoxLayout;
        labelRow->addWidget(label, 1);
        labelRow->addWidget(copy);

        auto *model = new QStandardItemModel(0, 3, this);
        model->setHorizontalHeaderLabels({tr("Folder"), tr("Old name"), tr("New name")});
        for (const OpenStep &s : info.openSteps) {
            const QString folder = QFileInfo(s.from).path();
            auto *folderItem = new QStandardItem(HistoryModel::displayPath(folder));
            folderItem->setToolTip(HistoryModel::displayPath(folder));
            QStandardItem *from = nameItem(s.from, s.fromIsTemp);
            QStandardItem *to = nameItem(s.to, s.toIsTemp);
            const QString fromText =
                    s.fromIsTemp ? tr("temporary name %1").arg(QFileInfo(s.from).fileName())
                                 : QDir::toNativeSeparators(s.from);
            const QString toText = s.toIsTemp
                                           ? tr("temporary name %1").arg(QFileInfo(s.to).fileName())
                                           : QDir::toNativeSeparators(s.to);
            const QString accessible = tr("%1 to %2").arg(fromText, toText);
            for (QStandardItem *item : {folderItem, from, to})
                item->setData(accessible, Qt::AccessibleTextRole);
            model->appendRow({folderItem, from, to});
        }
        auto *table = new QTableView(this);
        table->setObjectName(QStringLiteral("openStepsTable"));
        table->setModel(model);
        table->setItemDelegate(new OpenStepDelegate(table));
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setTextElideMode(Qt::ElideMiddle);
        table->setWordWrap(false);
        table->verticalHeader()->hide();
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->resizeRowsToContents();
        int height = table->horizontalHeader()->sizeHint().height() + (2 * table->frameWidth());
        for (int row = 0; row < std::min(n, kVisibleRows); ++row)
            height += table->rowHeight(row);
        table->setFixedHeight(height);

        auto *note = new QLabel(openStepsNoteText(n), this);
        note->setObjectName(QStringLiteral("openStepsNote"));
        note->setWordWrap(true);

        text->addSpacing(fontMetrics().height() / 2);
        text->addLayout(labelRow);
        text->addWidget(table);
        text->addWidget(note);
    }

    auto *top = new QHBoxLayout;
    top->addWidget(icon, 0, Qt::AlignTop);
    top->addLayout(text, 1);

    auto *buttons = new QHBoxLayout;
    QPushButton *mark = makeButton(tr("Mark as Complete"), "markComplete", this);
    connect(mark, &QPushButton::clicked, this, [this] { finish(RecoveryChoice::MarkComplete); });
    QPushButton *primary = nullptr;
    if (info.damaged) {
        QPushButton *show = makeButton(showInFileManagerText(), "showFile", this);
        connect(show, &QPushButton::clicked, this, [this] { finish(RecoveryChoice::ShowFile); });
        primary = makeButton(tr("Close"), "close", this);
        connect(primary, &QPushButton::clicked, this, [this] { finish(RecoveryChoice::Close); });
        buttons->addWidget(show);
    } else {
        QPushButton *details = makeButton(tr("Show Details"), "showDetails", this);
        connect(details, &QPushButton::clicked, this,
                [this] { finish(RecoveryChoice::ShowDetails); });
        primary = makeButton(tr("Roll Back"), "rollBack", this);
        connect(primary, &QPushButton::clicked, this, [this] { finish(RecoveryChoice::RollBack); });
        buttons->addWidget(details);
    }
    buttons->addStretch(1);
    buttons->addWidget(mark);
    buttons->addWidget(primary);
    // Mark as Complete is never the default (ui-design 9.10).
    setPrimary(mark, false);
    setPrimary(primary, true);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addSpacing(fontMetrics().height() / 2);
    layout->addLayout(buttons);
    setMinimumWidth(32 * fontMetrics().averageCharWidth() * 2);
}

void RecoveryDialog::reject()
{
    // Escape or close: nothing changes; the batch stays in progress.
    finish(RecoveryChoice::Close);
}

void RecoveryDialog::finish(RecoveryChoice choice)
{
    m_choice = choice;
    done(choice == RecoveryChoice::Close ? QDialog::Rejected : QDialog::Accepted);
}

} // namespace ctrm::app
