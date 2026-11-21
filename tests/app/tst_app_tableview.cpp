// SPDX-License-Identifier: GPL-3.0-or-later
#include "actions.h"
#include "dragpos.h"
#include "models/itemfilterproxy.h"
#include "models/itemtablemodel.h"
#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/tokens.h"
#include "widgets/emptystate.h"
#include "widgets/itemdelegate.h"
#include "widgets/itemtableview.h"

#include <QAccessible>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleHints>
#include <QtTest>

#include <cmath>

using namespace ctrm;
using namespace ctrm::app;

namespace {

Item makeItem(const QString &name, const QString &folder = QStringLiteral("/r"))
{
    Item it;
    it.folder = folder;
    it.name = name;
    return it;
}

ItemPreview row(Status s, const QString &newName, QList<Diagnostic> problems = {},
                QList<Diagnostic> warnings = {})
{
    ItemPreview p;
    p.status = s;
    p.newName = newName;
    p.problems = std::move(problems);
    p.warnings = std::move(warnings);
    return p;
}

Diagnostic diag(DiagCode c, const QString &text = {}, int count = 0)
{
    Diagnostic d{c, text};
    d.count = count;
    return d;
}

void setPreview(ItemTableModel &m, const QList<ItemPreview> &rows)
{
    PreviewResult r;
    r.items = rows;
    m.setPreview(r);
}

QStringList oldNames(const ItemTableModel &m)
{
    QStringList out;
    for (const Item &it : m.items())
        out.append(it.name);
    return out;
}

bool near(const QColor &a, const QColor &b, int tolerance)
{
    return std::abs(a.red() - b.red()) <= tolerance &&
           std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

QImage image(const QIcon &icon)
{
    return icon.pixmap(QSize(16, 16), 1.0).toImage().convertToFormat(QImage::Format_ARGB32);
}

struct Fixture
{
    QWidget window;
    ItemTableModel model;
    ItemFilterProxy proxy;
    ItemTableView *view;

    explicit Fixture(const QStringList &names)
    {
        QList<Item> items;
        for (const QString &n : names)
            items.append(makeItem(n));
        model.addItems(items);
        view = new ItemTableView(model, proxy, &window);
        // Pure black on pure white is a high-contrast theme, which has no tints.
        QPalette light = view->palette();
        light.setColor(QPalette::Text, QColor(0x1D, 0x1D, 0x1F));
        view->setPalette(light);
        window.resize(900, 400);
        view->resize(900, 400);
    }

    void show()
    {
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
    }

    QStyleOptionViewItem option(int r, int column) const
    {
        QStyleOptionViewItem opt;
        opt.initFrom(view);
        auto *delegate = qobject_cast<ItemDelegate *>(view->itemDelegate());
        Q_ASSERT(delegate);
        delegate->initStyleOption(&opt, view->model()->index(r, column));
        return opt;
    }

    void clickHeader(int column)
    {
        QHeaderView *header = view->horizontalHeader();
        const QPoint pos(header->sectionViewportPosition(column) + 5, header->height() / 2);
        QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, pos);
    }
};

} // namespace

class TestAppTableView : public QObject
{
    Q_OBJECT
private slots:
    void headerClickSortsOnce();
    void secondClickReverses();
    void folderColumnAuto();
    void messageColumnAlways();
    void messageElidesAtEnd();
    void headerLeftAligned();
    void iconColumnsHaveNoSeparator();
    void nameColumnsBalanced();
    void headerMenuHidesColumns();
    void tintsByStatus();
    void tintsFollowPaletteChange();
    void warningIconReplacesChange();
    void dimmedRows();
    void longNamesElideMiddle();
    void rowHeight();
    void spaceTogglesInclude();
    void nextProblemWraps();
    void selectedIds();
    void externalDrop();
    void emptyStatePrimary();
    void emptyStateFusionSameHeight();
    void emptyStateDrop();
    void accessibleRowText();
    void stateIconInEveryRow();
    void selectedRowStateIcons_data();
    void selectedRowStateIcons();
    void stateColumnFixedWidth();
    void unchangedStateTexts();
};

void TestAppTableView::headerClickSortsOnce()
{
    Fixture f({QStringLiteral("c.txt"), QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    setPreview(f.model, {row(Status::Changed, QStringLiteral("x10.txt")),
                         row(Status::Changed, QStringLiteral("x2.txt")),
                         row(Status::Changed, QStringLiteral("x1.txt"))});
    f.show();

    f.clickHeader(ItemTableModel::NewName);
    QCOMPARE(oldNames(f.model), (QStringList{QStringLiteral("b.txt"), QStringLiteral("a.txt"),
                                             QStringLiteral("c.txt")}));
    QHeaderView *header = f.view->horizontalHeader();
    QVERIFY(header->isSortIndicatorShown());
    QCOMPARE(header->sortIndicatorSection(), int(ItemTableModel::NewName));
    QCOMPARE(header->sortIndicatorOrder(), Qt::AscendingOrder);

    // A new preview with other names does not move the rows (AC3.4).
    setPreview(f.model, {row(Status::Changed, QStringLiteral("z.txt")),
                         row(Status::Changed, QStringLiteral("y.txt")),
                         row(Status::Changed, QStringLiteral("a.txt"))});
    QCOMPARE(oldNames(f.model), (QStringList{QStringLiteral("b.txt"), QStringLiteral("a.txt"),
                                             QStringLiteral("c.txt")}));

    // Another change of the order removes the arrow.
    f.model.moveRows({2}, 0);
    QVERIFY(!header->isSortIndicatorShown());
}

void TestAppTableView::secondClickReverses()
{
    Fixture f({QStringLiteral("b.txt"), QStringLiteral("a.txt"), QStringLiteral("c.txt")});
    f.show();
    f.clickHeader(ItemTableModel::OldName);
    QCOMPARE(oldNames(f.model), (QStringList{QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                                             QStringLiteral("c.txt")}));
    f.clickHeader(ItemTableModel::OldName);
    QCOMPARE(oldNames(f.model), (QStringList{QStringLiteral("c.txt"), QStringLiteral("b.txt"),
                                             QStringLiteral("a.txt")}));
    QCOMPARE(f.view->horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);

    // Include and State have no sort: the arrow stays on the sorted column.
    f.clickHeader(ItemTableModel::State);
    f.clickHeader(ItemTableModel::Include);
    QCOMPARE(f.view->horizontalHeader()->sortIndicatorSection(), int(ItemTableModel::OldName));
    QCOMPARE(f.view->horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
    QCOMPARE(oldNames(f.model), (QStringList{QStringLiteral("c.txt"), QStringLiteral("b.txt"),
                                             QStringLiteral("a.txt")}));
}

void TestAppTableView::folderColumnAuto()
{
    Fixture f({QStringLiteral("a.txt")});
    QVERIFY(f.view->isColumnHidden(ItemTableModel::Folder));
    QVERIFY(f.view->isColumnHidden(ItemTableModel::Size));
    QVERIFY(f.view->isColumnHidden(ItemTableModel::Modified));

    f.model.addItems({makeItem(QStringLiteral("b.txt"), QStringLiteral("/other"))});
    QVERIFY(!f.view->isColumnHidden(ItemTableModel::Folder));

    f.model.removeItems({f.model.items().last().id});
    QVERIFY(f.view->isColumnHidden(ItemTableModel::Folder));
}

void TestAppTableView::messageColumnAlways()
{
    // S02: the column does not jump on each edit. It shows when the list has items.
    Fixture f({QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    setPreview(f.model, {row(Status::Changed, QStringLiteral("x.txt")),
                         row(Status::Unchanged, QStringLiteral("b.txt"))});
    QVERIFY(!f.view->isColumnHidden(ItemTableModel::Message));

    setPreview(f.model, {row(Status::Conflict, QStringLiteral("x.txt"),
                             {diag(DiagCode::DuplicateInBatch, {}, 1)}),
                         row(Status::Unchanged, QStringLiteral("b.txt"))});
    QVERIFY(!f.view->isColumnHidden(ItemTableModel::Message));

    f.model.clear();
    QVERIFY(f.view->isColumnHidden(ItemTableModel::Message));
}

void TestAppTableView::messageElidesAtEnd()
{
    // M07: names elide in the middle, messages at the end (the key word is first).
    Fixture f({QStringLiteral("a.txt")});
    QCOMPARE(f.option(0, ItemTableModel::Message).textElideMode, Qt::ElideRight);
    QCOMPARE(f.option(0, ItemTableModel::OldName).textElideMode, Qt::ElideMiddle);
    QCOMPARE(f.option(0, ItemTableModel::NewName).textElideMode, Qt::ElideMiddle);
}

void TestAppTableView::headerLeftAligned()
{
    // M14: the header text has the same alignment as the cells, and it is semibold.
    Fixture f({QStringLiteral("a.txt")});
    const QHeaderView *header = f.view->horizontalHeader();
    QCOMPARE(header->defaultAlignment() & Qt::AlignHorizontal_Mask, Qt::AlignLeft);
    QCOMPARE(header->font().weight(), QFont::DemiBold);
}

void TestAppTableView::iconColumnsHaveNoSeparator()
{
    // C10: no section separator after the Include and State columns: the right edge of each
    // of these sections has the colour of its inside. The text columns keep their separators.
    Fixture f({QStringLiteral("a.txt")});
    f.show();
    QHeaderView *header = f.view->horizontalHeader();
    const QImage image = header->grab().toImage();
    const qreal dpr = image.devicePixelRatio();
    const int y = qRound(header->height() / 2 * dpr);
    for (const int column : {int(ItemTableModel::Include), int(ItemTableModel::State)}) {
        const int left = header->sectionViewportPosition(column);
        const int right = left + header->sectionSize(column);
        const QColor inside = image.pixelColor(qRound((left + 2) * dpr), y);
        for (int x = qRound((right - 2) * dpr); x < qRound(right * dpr); ++x)
            QCOMPARE(image.pixelColor(x, y), inside);
    }
}

void TestAppTableView::nameColumnsBalanced()
{
    // M14: Old name and New name have the same width (30% of the table each, as the mockup),
    // and the last column takes the rest.
    Fixture f({QStringLiteral("a.txt")});
    f.window.resize(1200, 400);
    f.view->resize(1200, 400);
    f.show();
    const QHeaderView *header = f.view->horizontalHeader();
    QTRY_COMPARE(header->sectionSize(ItemTableModel::OldName),
                 header->sectionSize(ItemTableModel::NewName));
    QCOMPARE(header->sectionSize(ItemTableModel::OldName), f.view->viewport()->width() * 3 / 10);

    // The user hides Message: New name is the last column, and the names still share.
    QMenu *menu = f.view->findChild<QMenu *>(QStringLiteral("columnsMenu"));
    QVERIFY(menu);
    for (QAction *a : menu->actions()) {
        if (a->data().toInt() == ItemTableModel::Message)
            a->trigger();
    }
    QVERIFY(f.view->isColumnHidden(ItemTableModel::Message));
    QTRY_VERIFY(qAbs(header->sectionSize(ItemTableModel::OldName) -
                     header->sectionSize(ItemTableModel::NewName)) <= 1);
}

void TestAppTableView::headerMenuHidesColumns()
{
    Fixture f({QStringLiteral("a.txt")});
    QMenu *menu = f.view->findChild<QMenu *>(QStringLiteral("columnsMenu"));
    QVERIFY(menu);
    QAction *size = nullptr;
    QAction *folder = nullptr;
    for (QAction *a : menu->actions()) {
        if (a->data().toInt() == ItemTableModel::Size)
            size = a;
        if (a->data().toInt() == ItemTableModel::Folder)
            folder = a;
        QVERIFY(a->data().toInt() != ItemTableModel::OldName);
    }
    QVERIFY(size && folder);
    QVERIFY(!size->isChecked());
    size->trigger();
    QVERIFY(!f.view->isColumnHidden(ItemTableModel::Size));

    // A column the user shows stays shown: the automatic rule stops for it.
    folder->trigger();
    QVERIFY(!f.view->isColumnHidden(ItemTableModel::Folder));
    f.model.addItems({makeItem(QStringLiteral("b.txt"))});
    QVERIFY(!f.view->isColumnHidden(ItemTableModel::Folder));
}

void TestAppTableView::tintsByStatus()
{
    Fixture f({QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")});
    setPreview(f.model, {row(Status::Conflict, QStringLiteral("x.txt"),
                             {diag(DiagCode::DuplicateInBatch, {}, 1)}),
                         row(Status::Changed, QStringLiteral("y.txt")),
                         row(Status::Invalid, QString(), {diag(DiagCode::EmptyName)})});
    const CtrmColors colors = CtrmColors::fromPalette(f.view->palette());
    QVERIFY(colors.tintsEnabled);
    QCOMPARE(f.option(0, ItemTableModel::OldName).backgroundBrush.color(), colors.tintConflict);
    QCOMPARE(f.option(0, ItemTableModel::NewName).backgroundBrush.color(), colors.tintConflict);
    QCOMPARE(f.option(1, ItemTableModel::OldName).backgroundBrush.style(), Qt::NoBrush);
    QCOMPARE(f.option(2, ItemTableModel::OldName).backgroundBrush.color(), colors.tintError);

    // Post-run Failed has the error tint.
    f.model.setPostRun({{f.model.items()[1].id, {PostRun::Failed, QStringLiteral("x")}}});
    QCOMPARE(f.option(1, ItemTableModel::OldName).backgroundBrush.color(), colors.tintError);
}

void TestAppTableView::tintsFollowPaletteChange()
{
    Fixture f({QStringLiteral("a.txt")});
    setPreview(f.model, {row(Status::Conflict, QStringLiteral("x.txt"),
                             {diag(DiagCode::ExistsInFolder, QStringLiteral("x.txt"))})});
    QPalette dark = f.view->palette();
    dark.setColor(QPalette::Base, QColor(0x20, 0x20, 0x20));
    dark.setColor(QPalette::Text, QColor(0xE0, 0xE0, 0xE0));
    dark.setColor(QPalette::Window, QColor(0x28, 0x28, 0x28));
    dark.setColor(QPalette::WindowText, QColor(0xE0, 0xE0, 0xE0));
    f.view->setPalette(dark);
    QCOMPARE(f.option(0, ItemTableModel::OldName).backgroundBrush.color(),
             mix(QColor(0xE0A000), QColor(0x20, 0x20, 0x20), 0.12));

    // High contrast: no tints; the icon shapes stay (design-tokens 2.6).
    QPalette contrast = f.view->palette();
    contrast.setColor(QPalette::Base, Qt::black);
    contrast.setColor(QPalette::Text, Qt::white);
    f.view->setPalette(contrast);
    if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Unknown) {
        QCOMPARE(f.option(0, ItemTableModel::OldName).backgroundBrush.style(), Qt::NoBrush);
        QVERIFY(!f.option(0, ItemTableModel::State).icon.isNull());
    }
}

void TestAppTableView::warningIconReplacesChange()
{
    Fixture f({QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")});
    setPreview(f.model, {row(Status::Changed, QStringLiteral("a.jpg"), {},
                             {diag(DiagCode::ExtensionChanged)}),
                         row(Status::Changed, QStringLiteral("x.txt")),
                         row(Status::Unchanged, QStringLiteral("c.txt"))});
    const CtrmColors colors = CtrmColors::fromPalette(f.view->palette());
    const QStyleOptionViewItem warning = f.option(0, ItemTableModel::State);
    QVERIFY(warning.features & QStyleOptionViewItem::HasDecoration);
    QCOMPARE(image(warning.icon), image(stateIcon(StateIcon::Warning, colors)));
    QCOMPARE(image(f.option(1, ItemTableModel::State).icon),
             image(stateIcon(StateIcon::Change, colors)));
    QCOMPARE(image(f.option(2, ItemTableModel::State).icon),
             image(stateIcon(StateIcon::Same, colors))); // O12
    // The State column shows the icon only.
    QVERIFY(f.option(1, ItemTableModel::State).text.isEmpty());
}

void TestAppTableView::dimmedRows()
{
    Fixture f({QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    setPreview(f.model, {row(Status::Unchanged, QStringLiteral("a.txt")),
                         row(Status::Changed, QStringLiteral("x.txt"))});
    const QColor dim = f.view->palette().color(QPalette::PlaceholderText);
    const QColor text = f.view->palette().color(QPalette::Text);
    // Unchanged: the new name is dimmed, the old name is not.
    QCOMPARE(f.option(0, ItemTableModel::NewName).palette.color(QPalette::Text), dim);
    QCOMPARE(f.option(0, ItemTableModel::OldName).palette.color(QPalette::Text), text);

    // Excluded: the whole row is dimmed, with the excluded icon.
    f.model.setIncluded({f.model.items()[1].id}, false);
    QCOMPARE(f.option(1, ItemTableModel::OldName).palette.color(QPalette::Text), dim);
    QCOMPARE(f.option(1, ItemTableModel::NewName).palette.color(QPalette::Text), dim);
    const CtrmColors colors = CtrmColors::fromPalette(f.view->palette());
    QCOMPARE(image(f.option(1, ItemTableModel::State).icon),
             image(stateIcon(StateIcon::Excluded, colors)));
}

void TestAppTableView::longNamesElideMiddle()
{
    const QString name = QString(200, QLatin1Char('a')) + QStringLiteral(".txt");
    Fixture f({name});
    QCOMPARE(f.view->textElideMode(), Qt::ElideMiddle);
    QVERIFY(f.view->model()
                    ->index(0, ItemTableModel::OldName)
                    .data(Qt::ToolTipRole)
                    .toString()
                    .startsWith(name));
}

void TestAppTableView::rowHeight()
{
    Fixture f({QStringLiteral("a.txt")});
    QCOMPARE(f.view->verticalHeader()->defaultSectionSize(), f.view->fontMetrics().height() + 8);
    QCOMPARE(f.view->verticalHeader()->sectionResizeMode(0), QHeaderView::Fixed);
    QVERIFY(!f.view->verticalHeader()->isVisible());
}

void TestAppTableView::spaceTogglesInclude()
{
    Fixture f({QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")});
    f.show();
    f.view->setFocus();
    f.view->selectRow(0);
    f.view->selectionModel()->select(f.view->model()->index(1, 0),
                                     QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QTest::keyClick(f.view, Qt::Key_Space);
    QVERIFY(!f.model.items()[0].included);
    QVERIFY(!f.model.items()[1].included);
    QVERIFY(f.model.items()[2].included);
    QTest::keyClick(f.view, Qt::Key_Space);
    QVERIFY(f.model.items()[0].included);
    QVERIFY(f.model.items()[1].included);
}

void TestAppTableView::nextProblemWraps()
{
    Fixture f({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"),
               QStringLiteral("e")});
    setPreview(f.model, {row(Status::Changed, QStringLiteral("x")),
                         row(Status::Conflict, QStringLiteral("y"),
                             {diag(DiagCode::ExistsInFolder, QStringLiteral("y"))}),
                         row(Status::Invalid, QString(), {diag(DiagCode::EmptyName)}),
                         row(Status::Missing, QStringLiteral("d"), {diag(DiagCode::NotOnDisk)}),
                         row(Status::Changed, QStringLiteral("z"))});
    QList<int> rows;
    for (int i = 0; i < 4; ++i) {
        f.view->selectNextProblem(true);
        rows.append(f.view->currentIndex().row());
        QCOMPARE(f.view->selectionModel()->selectedRows().size(), 1);
    }
    QCOMPARE(rows, (QList<int>{1, 2, 3, 1}));
    f.view->selectNextProblem(false);
    QCOMPARE(f.view->currentIndex().row(), 3);

    // Only rows that the filter shows.
    f.proxy.setText(QStringLiteral("b"));
    f.view->selectNextProblem(true);
    QCOMPARE(f.view->currentIndex().siblingAtColumn(ItemTableModel::OldName).data().toString(),
             QStringLiteral("b"));
}

void TestAppTableView::selectedIds()
{
    Fixture f({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    f.proxy.setText(QStringLiteral("c"));
    f.view->selectAll();
    QCOMPARE(f.view->selectedIds(), QList<ItemId>{f.model.items()[2].id});
}

void TestAppTableView::externalDrop()
{
    Fixture f({QStringLiteral("a")});
    f.show();
    QSignalSpy spy(f.view, &ItemTableView::pathsDropped);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/x.txt")),
                  QUrl::fromLocalFile(QStringLiteral("/tmp/y"))});
    const QPoint pos = f.view->viewport()->rect().center();
    QDragEnterEvent enter(test::DragPos(pos), Qt::CopyAction, &mime, Qt::LeftButton, {});
    QCoreApplication::sendEvent(f.view->viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QCOMPARE(f.view->dropText(), QStringLiteral("Drop to add 2 items"));
    QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, {});
    QCoreApplication::sendEvent(f.view->viewport(), &drop);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toStringList(),
             (QStringList{QStringLiteral("/tmp/x.txt"), QStringLiteral("/tmp/y")}));
    QVERIFY(f.view->dropText().isEmpty());
    QCOMPARE(f.model.rowCount(), 1); // the view does not add the items
}

void TestAppTableView::emptyStatePrimary()
{
    QWidget parent;
    const Actions actions = createActions(&parent);
    EmptyState empty(actions, &parent);
    auto *addFiles = empty.findChild<QPushButton *>(QStringLiteral("emptyAddFiles"));
    auto *addFolder = empty.findChild<QPushButton *>(QStringLiteral("emptyAddFolder"));
    QVERIFY(addFiles && addFolder);
    QVERIFY(addFiles->isDefault());
    QVERIFY(!addFolder->isDefault());
    QCOMPARE(addFiles->text(), QStringLiteral("Add Files..."));
    // R5-05: one row, one height (control-h-large), body-size text; the primary keeps the
    // label-strong weight.
    QCOMPARE(addFiles->height(), tokens::controlHLarge);
    QCOMPARE(addFolder->height(), tokens::controlHLarge);
    QCOMPARE(addFiles->font().pointSizeF(), addFolder->font().pointSizeF());
    QVERIFY(addFiles->font().weight() > addFolder->font().weight());

    QSignalSpy files(actions.addFiles, &QAction::triggered);
    QSignalSpy folder(actions.addFolder, &QAction::triggered);
    addFiles->click();
    addFolder->click();
    QCOMPARE(files.size(), 1);
    QCOMPARE(folder.size(), 1);

    QStringList texts;
    for (QLabel *l : empty.findChildren<QLabel *>())
        texts.append(l->text());
    QVERIFY(texts.contains(QStringLiteral("Drop files or folders here")));
    QVERIFY(texts.contains(QStringLiteral("Nothing is renamed until you click Rename.")));
    QVERIFY(texts.contains(QStringLiteral("Nothing to toast yet.")));
    auto *icon = empty.findChild<QLabel *>(QStringLiteral("emptyIcon"));
    QVERIFY(icon);
    QCOMPARE(icon->pixmap().deviceIndependentSize().toSize(), QSize(96, 96));
}

void TestAppTableView::emptyStateFusionSameHeight()
{
    const QString oldStyle = QApplication::style()->name();
    QApplication::setStyle(QStringLiteral("Fusion"));
    {
        QWidget parent;
        const Actions actions = createActions(&parent);
        EmptyState empty(actions, &parent);
        auto *addFiles = empty.findChild<QPushButton *>(QStringLiteral("emptyAddFiles"));
        auto *addFolder = empty.findChild<QPushButton *>(QStringLiteral("emptyAddFolder"));
        QCOMPARE(addFiles->height(), tokens::controlHLarge);
        QCOMPARE(addFolder->height(), tokens::controlHLarge);
        QCOMPARE(addFiles->palette().color(QPalette::Button),
                 empty.palette().color(QPalette::Accent));
        QCOMPARE(addFolder->palette().color(QPalette::Button),
                 empty.palette().color(QPalette::Button));
    }
    QApplication::setStyle(oldStyle);
}

void TestAppTableView::emptyStateDrop()
{
    QWidget parent;
    const Actions actions = createActions(&parent);
    EmptyState empty(actions, &parent);
    QSignalSpy spy(&empty, &EmptyState::pathsDropped);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/x.txt")),
                  QUrl(QStringLiteral("https://example.com/a"))});
    QDragEnterEvent enter(test::DragPos(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, {});
    QCoreApplication::sendEvent(&empty, &enter);
    QVERIFY(enter.isAccepted());
    // ui-design 9.2 (audit M16): the drag-over text, counting only the local files.
    auto *title = empty.findChild<QLabel *>(QStringLiteral("emptyTitle"));
    QCOMPARE(title->text(), QStringLiteral("Drop to add 1 item"));
    QDragLeaveEvent leave;
    QCoreApplication::sendEvent(&empty, &leave);
    QCOMPARE(title->text(), QStringLiteral("Drop files or folders here"));
    QMimeData two;
    two.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/x.txt")),
                 QUrl::fromLocalFile(QStringLiteral("/tmp/y.txt"))});
    QDragEnterEvent enterTwo(test::DragPos(5, 5), Qt::CopyAction, &two, Qt::LeftButton, {});
    QCoreApplication::sendEvent(&empty, &enterTwo);
    QCOMPARE(title->text(), QStringLiteral("Drop to add 2 items"));
    QCoreApplication::sendEvent(&empty, &leave);
    QCoreApplication::sendEvent(&empty, &enter);
    QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, {});
    QCoreApplication::sendEvent(&empty, &drop);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toStringList(), QStringList{QStringLiteral("/tmp/x.txt")});
    QCOMPARE(title->text(), QStringLiteral("Drop files or folders here"));

    // No local file: the drop is refused.
    QMimeData web;
    web.setUrls({QUrl(QStringLiteral("https://example.com/a"))});
    QDragEnterEvent refused(test::DragPos(5, 5), Qt::CopyAction, &web, Qt::LeftButton, {});
    refused.setAccepted(false);
    QCoreApplication::sendEvent(&empty, &refused);
    QVERIFY(!refused.isAccepted());
}

void TestAppTableView::accessibleRowText()
{
    Fixture f({QStringLiteral("IMG_1.jpg")});
    setPreview(f.model, {row(Status::Changed, QStringLiteral("Trip_1.jpg"))});
    f.show();
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(f.view);
    QVERIFY(iface && iface->tableInterface());
    QAccessibleInterface *cell = iface->tableInterface()->cellAt(0, ItemTableModel::OldName);
    QVERIFY(cell);
    const QString text = cell->text(QAccessible::Name);
    QVERIFY2(text.contains(QStringLiteral("will become")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("IMG_1.jpg")));
    QVERIFY(text.contains(QStringLiteral("Trip_1.jpg")));
    QVERIFY(text.contains(QStringLiteral("Will rename")));
}

void TestAppTableView::stateIconInEveryRow()
{
    // O12 (ui-design 7.2, table "State column"): each row has a centred 16 px state icon, also
    // a row where nothing happens.
    Fixture f({QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt"),
               QStringLiteral("d.txt"), QStringLiteral("e.txt"), QStringLiteral("f.txt"),
               QStringLiteral("g.txt")});
    setPreview(f.model, {row(Status::Unchanged, QStringLiteral("a.txt")),
                         row(Status::Changed, QStringLiteral("x.txt")),
                         row(Status::CaseOnly, QStringLiteral("C.txt")),
                         row(Status::Changed, QStringLiteral("d.jpg"), {},
                             {diag(DiagCode::ExtensionChanged)}),
                         row(Status::Conflict, QStringLiteral("x.txt"),
                             {diag(DiagCode::DuplicateInBatch, {}, 1)}),
                         row(Status::Invalid, QString(), {diag(DiagCode::EmptyName)}),
                         row(Status::Changed, QStringLiteral("y.txt"))});
    f.model.setIncluded({f.model.items()[6].id}, false);
    const CtrmColors colors = CtrmColors::fromPalette(f.view->palette());
    const auto check = [&](const QList<StateIcon> &expected) {
        for (int r = 0; r < expected.size(); ++r) {
            const QStyleOptionViewItem o = f.option(r, ItemTableModel::State);
            QVERIFY2(o.features & QStyleOptionViewItem::HasDecoration,
                     qPrintable(QString::number(r)));
            QVERIFY(!o.icon.isNull());
            QCOMPARE(image(o.icon), image(stateIcon(expected[r], colors)));
            QCOMPARE(o.decorationSize, QSize(tokens::iconS, tokens::iconS));
            QCOMPARE(o.decorationAlignment, Qt::AlignCenter);
            QVERIFY(o.text.isEmpty());
        }
    };
    check({StateIcon::Same, StateIcon::Change, StateIcon::Change, StateIcon::Warning,
           StateIcon::Conflict, StateIcon::Error, StateIcon::Excluded});

    // After a batch: done, error, dash, rolled back; a row with no post-run state keeps its icon.
    const QList<Item> items = f.model.items();
    f.model.setPostRun({{items[0].id, {PostRun::Renamed, {}}},
                        {items[1].id, {PostRun::Failed, QStringLiteral("x")}},
                        {items[2].id, {PostRun::Skipped, QStringLiteral("x")}},
                        {items[3].id, {PostRun::RolledBack, {}}}});
    check({StateIcon::Done, StateIcon::Error, StateIcon::Excluded, StateIcon::RolledBack,
           StateIcon::Conflict, StateIcon::Error, StateIcon::Excluded});

    // The same icon is an equals sign in the dim text colour, not the change colour.
    QVERIFY(image(stateIcon(StateIcon::Same, colors)) !=
            image(stateIcon(StateIcon::Excluded, colors)));
}

void TestAppTableView::selectedRowStateIcons_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

void TestAppTableView::selectedRowStateIcons()
{
    // R8-02 (ui-design 7.2): in an active selected row the line icons (same, change arrow, done
    // check, dash, rolled back) use the highlighted text colour; the filled icons (error,
    // warning, conflict) keep their colour. In an inactive selection all icons keep their colours.
    QFETCH(bool, dark);
    Fixture f({QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt"),
               QStringLiteral("d.jpg"), QStringLiteral("e.txt"), QStringLiteral("f.txt"),
               QStringLiteral("g.txt"), QStringLiteral("h.txt")});
    QPalette palette = f.view->palette();
    for (const auto group : {QPalette::Active, QPalette::Inactive}) {
        palette.setColor(group, QPalette::Window, dark ? QColor(0x2B2B2D) : QColor(0xFFFFFF));
        palette.setColor(group, QPalette::Base, dark ? QColor(0x1E1E1E) : QColor(0xFFFFFF));
        palette.setColor(group, QPalette::Text, dark ? QColor(0xE3E3E3) : QColor(0x1D1D1F));
        palette.setColor(group, QPalette::Accent, dark ? QColor(0x0F51CF) : QColor(0x0A60FF));
        palette.setColor(group, QPalette::HighlightedText, QColor(0xFFFFFF));
    }
    palette.setColor(QPalette::Active, QPalette::Highlight,
                     dark ? QColor(0x0640C6) : QColor(0x0A60FF));
    palette.setColor(QPalette::Inactive, QPalette::Highlight,
                     dark ? QColor(0x464646) : QColor(0xDCDCDC));
    palette.setColor(QPalette::Inactive, QPalette::HighlightedText, palette.color(QPalette::Text));
    f.view->setPalette(palette);

    setPreview(f.model, {row(Status::Unchanged, QStringLiteral("a.txt")),
                         row(Status::Changed, QStringLiteral("x.txt")),
                         row(Status::Changed, QStringLiteral("y.txt")),
                         row(Status::Changed, QStringLiteral("d.png"), {},
                             {diag(DiagCode::ExtensionChanged)}),
                         row(Status::Conflict, QStringLiteral("z.txt"),
                             {diag(DiagCode::DuplicateInBatch, {}, 1)}),
                         row(Status::Invalid, QString(), {diag(DiagCode::EmptyName)}),
                         row(Status::Changed, QStringLiteral("q.txt")),
                         row(Status::Changed, QStringLiteral("r.txt"))});
    const QList<Item> items = f.model.items();
    f.model.setIncluded({items[2].id}, false);
    f.model.setPostRun(
            {{items[6].id, {PostRun::Renamed, {}}}, {items[7].id, {PostRun::RolledBack, {}}}});
    const QList<StateIcon> icons{StateIcon::Same,    StateIcon::Change,    StateIcon::Excluded,
                                 StateIcon::Warning, StateIcon::Conflict,  StateIcon::Error,
                                 StateIcon::Done,    StateIcon::RolledBack};
    const QSet<StateIcon> filled{StateIcon::Error, StateIcon::Warning, StateIcon::Conflict};

    const CtrmColors colors = CtrmColors::fromPalette(f.view->palette());
    CtrmColors onSelection = colors;
    const QColor highlighted = palette.color(QPalette::Active, QPalette::HighlightedText);
    onSelection.stateNeutral = highlighted;
    onSelection.stateChange = highlighted;
    onSelection.stateDone = highlighted;
    const auto selectedOption = [&](int r, bool active) {
        QStyleOptionViewItem opt;
        opt.initFrom(f.view);
        opt.state &= ~QStyle::State_Active;
        opt.state |= QStyle::State_Selected;
        if (active)
            opt.state |= QStyle::State_Active;
        auto *delegate = qobject_cast<ItemDelegate *>(f.view->itemDelegate());
        delegate->initStyleOption(&opt, f.view->model()->index(r, ItemTableModel::State));
        return opt;
    };
    for (int r = 0; r < icons.size(); ++r) {
        const StateIcon icon = icons[r];
        const QString name = QString::number(r);
        QCOMPARE(ItemDelegate::rowStateIcon(f.view->model()->index(r, 0)), icon);
        const QImage active = image(selectedOption(r, true).icon);
        if (filled.contains(icon)) {
            QVERIFY2(active == image(stateIcon(icon, colors)), qPrintable(name));
        } else {
            QVERIFY2(active == image(stateIcon(icon, onSelection)), qPrintable(name));
            QVERIFY2(active != image(stateIcon(icon, colors)), qPrintable(name));
        }
        QVERIFY2(image(selectedOption(r, false).icon) == image(stateIcon(icon, colors)),
                 qPrintable(name));
    }

    // As painted: the style draws a selected icon in QIcon::Selected mode. It must not tint the
    // white arrow with the selection colour.
    const auto paintedHas = [&](int r, bool active, const QColor &colour) {
        QStyleOptionViewItem opt = selectedOption(r, active);
        const QRect cell(0, 0, 28, 24);
        opt.rect = cell;
        QImage canvas(cell.size(), QImage::Format_ARGB32_Premultiplied);
        canvas.fill(
                palette.color(active ? QPalette::Active : QPalette::Inactive, QPalette::Highlight));
        QPainter painter(&canvas);
        f.view->itemDelegate()->paint(&painter, opt,
                                      f.view->model()->index(r, ItemTableModel::State));
        painter.end();
        for (int y = 0; y < canvas.height(); ++y)
            for (int x = 0; x < canvas.width(); ++x)
                if (near(canvas.pixelColor(x, y), colour, 6))
                    return true;
        return false;
    };
    QVERIFY(paintedHas(1, true, highlighted));       // the change arrow, white
    QVERIFY(paintedHas(5, true, colors.stateError)); // the error icon keeps its red
    QVERIFY(paintedHas(1, false, colors.stateChange));
}

void TestAppTableView::stateColumnFixedWidth()
{
    // O12: the State column is 28 px, QHeaderView::Fixed, in all states.
    Fixture f({});
    f.show();
    QHeaderView *header = f.view->horizontalHeader();
    const auto check = [&](const char *when) {
        QApplication::processEvents();
        QVERIFY2(header->sectionResizeMode(ItemTableModel::State) == QHeaderView::Fixed, when);
        QVERIFY2(header->sectionSize(ItemTableModel::State) == 28,
                 qPrintable(QStringLiteral("%1: %2")
                                    .arg(QLatin1String(when))
                                    .arg(header->sectionSize(ItemTableModel::State))));
    };
    check("empty");
    f.model.addItems({makeItem(QStringLiteral("a.txt")), makeItem(QStringLiteral("b.txt")),
                      makeItem(QStringLiteral("c.txt"), QStringLiteral("/s"))});
    setPreview(f.model, {row(Status::Unchanged, QStringLiteral("a.txt")),
                         row(Status::Changed, QStringLiteral("x.txt")),
                         row(Status::Conflict, QStringLiteral("x.txt"),
                             {diag(DiagCode::DuplicateInBatch, {}, 1)})});
    check("items");
    f.proxy.setText(QStringLiteral("b"));
    check("filter");
    f.proxy.setText(QString());
    f.proxy.setShowMode(ShowMode::Changed);
    check("show changed only");
    f.proxy.setShowMode(ShowMode::All);
    f.clickHeader(ItemTableModel::OldName);
    check("sort");
    const QList<Item> items = f.model.items();
    f.model.setPostRun({{items[0].id, {PostRun::Renamed, {}}}});
    check("renamed");
    f.model.setPostRun({{items[0].id, {PostRun::RolledBack, {}}}});
    check("undo");
    QFont big = f.view->font();
    big.setPointSizeF(big.pointSizeF() * 2);
    f.view->setFont(big);
    check("font");
    f.window.resize(500, 300);
    f.view->resize(500, 300);
    check("window size");
    // The user cannot drag it: a drag on the right edge of the section does nothing.
    const int edge = header->sectionViewportPosition(ItemTableModel::State) +
                     header->sectionSize(ItemTableModel::State) - 1;
    QTest::mousePress(header->viewport(), Qt::LeftButton, {}, QPoint(edge, header->height() / 2));
    QTest::mouseMove(header->viewport(), QPoint(edge + 40, header->height() / 2));
    QTest::mouseRelease(header->viewport(), Qt::LeftButton, {},
                        QPoint(edge + 40, header->height() / 2));
    check("drag");

    // A big font before the view exists (an accessibility text size at start).
    QWidget parent;
    QFont start = parent.font();
    start.setPointSizeF(start.pointSizeF() * 2);
    parent.setFont(start);
    ItemTableModel model;
    ItemFilterProxy proxy;
    ItemTableView bigView(model, proxy, &parent);
    QCOMPARE(bigView.horizontalHeader()->sectionSize(ItemTableModel::State), 28);
}

void TestAppTableView::unchangedStateTexts()
{
    // O12: tooltip "No change. The name stays 'a.txt'.", accessible text "No change"; the header
    // has no text, and its tooltip and accessible name are "State". New name does not change.
    Fixture f({QStringLiteral("a.txt")});
    setPreview(f.model, {row(Status::Unchanged, QStringLiteral("a.txt"))});
    f.show();
    const QModelIndex state = f.view->model()->index(0, ItemTableModel::State);
    QCOMPARE(state.data(Qt::ToolTipRole).toString(),
             QStringLiteral("No change. The name stays 'a.txt'."));
    QCOMPARE(ItemDelegate::rowStateName(state), QStringLiteral("No change"));
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(f.view);
    QVERIFY(iface && iface->tableInterface());
    QAccessibleInterface *cell = iface->tableInterface()->cellAt(0, ItemTableModel::State);
    QVERIFY(cell);
    QVERIFY2(cell->text(QAccessible::Name).startsWith(QStringLiteral("No change, a.txt")),
             qPrintable(cell->text(QAccessible::Name)));

    const QAbstractItemModel *m = f.view->model();
    QCOMPARE(m->headerData(ItemTableModel::State, Qt::Horizontal).toString(), QString());
    QCOMPARE(m->headerData(ItemTableModel::State, Qt::Horizontal, Qt::ToolTipRole).toString(),
             QStringLiteral("State"));
    QCOMPARE(
            m->headerData(ItemTableModel::State, Qt::Horizontal, Qt::AccessibleTextRole).toString(),
            QStringLiteral("State"));

    // New name: the name in text-dim, with no icon.
    const QStyleOptionViewItem newName = f.option(0, ItemTableModel::NewName);
    QCOMPARE(newName.text, QStringLiteral("a.txt"));
    QVERIFY(newName.icon.isNull());
    QCOMPARE(newName.palette.color(QPalette::Text),
             f.view->palette().color(QPalette::PlaceholderText));
}

QTEST_MAIN(TestAppTableView)
#include "tst_app_tableview.moc"
