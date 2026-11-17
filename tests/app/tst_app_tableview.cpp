// SPDX-License-Identifier: GPL-3.0-or-later
#include "actions.h"
#include "models/itemfilterproxy.h"
#include "models/itemtablemodel.h"
#include "theme/colors.h"
#include "theme/icons.h"
#include "widgets/emptystate.h"
#include "widgets/itemdelegate.h"
#include "widgets/itemtableview.h"

#include <QAccessible>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleHints>
#include <QtTest>

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
    void messageColumnAuto();
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
    void emptyStateFusionLarger();
    void emptyStateDrop();
    void accessibleRowText();
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

void TestAppTableView::messageColumnAuto()
{
    Fixture f({QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    setPreview(f.model, {row(Status::Changed, QStringLiteral("x.txt")),
                         row(Status::Unchanged, QStringLiteral("b.txt"))});
    QVERIFY(f.view->isColumnHidden(ItemTableModel::Message));

    setPreview(f.model, {row(Status::Conflict, QStringLiteral("x.txt"),
                             {diag(DiagCode::DuplicateInBatch, {}, 1)}),
                         row(Status::Unchanged, QStringLiteral("b.txt"))});
    QVERIFY(!f.view->isColumnHidden(ItemTableModel::Message));
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
    QVERIFY(f.option(2, ItemTableModel::State).icon.isNull());
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
    QDragEnterEvent enter(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, {});
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
    // Larger than the secondary button (ui-design 5.2). The macOS style keeps the native height,
    // so the accent bezel stays; there the button is bold and wider.
    if (addFiles->style()->name() == QLatin1StringView("macos")) {
        QVERIFY(addFiles->font().weight() > addFolder->font().weight());
    } else {
        QVERIFY(addFiles->font().pointSizeF() > addFolder->font().pointSizeF());
        QVERIFY(addFiles->sizeHint().height() > addFolder->sizeHint().height());
    }
    QVERIFY(addFiles->sizeHint().width() > addFolder->sizeHint().width());

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

void TestAppTableView::emptyStateFusionLarger()
{
    const QString oldStyle = QApplication::style()->name();
    QApplication::setStyle(QStringLiteral("Fusion"));
    {
        QWidget parent;
        const Actions actions = createActions(&parent);
        EmptyState empty(actions, &parent);
        auto *addFiles = empty.findChild<QPushButton *>(QStringLiteral("emptyAddFiles"));
        auto *addFolder = empty.findChild<QPushButton *>(QStringLiteral("emptyAddFolder"));
        QVERIFY(addFiles->font().pointSizeF() > addFolder->font().pointSizeF());
        QVERIFY(addFiles->sizeHint().height() > addFolder->sizeHint().height());
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
    QDragEnterEvent enter(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, {});
    QCoreApplication::sendEvent(&empty, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, {});
    QCoreApplication::sendEvent(&empty, &drop);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toStringList(), QStringList{QStringLiteral("/tmp/x.txt")});

    // No local file: the drop is refused.
    QMimeData web;
    web.setUrls({QUrl(QStringLiteral("https://example.com/a"))});
    QDragEnterEvent refused(QPointF(5, 5), Qt::CopyAction, &web, Qt::LeftButton, {});
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

QTEST_MAIN(TestAppTableView)
#include "tst_app_tableview.moc"
