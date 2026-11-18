// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/emptystate.h"

#include "text/plural.h"
#include "theme/icons.h"
#include "theme/tokens.h"
#include "theme/typography.h"
#include "widgets/buttonstyle.h"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionButton>
#include <QUrl>
#include <QVBoxLayout>

namespace ctrm::app {

namespace {

constexpr int kIconSize = 96;

// The macOS style draws the accent bezel of a default button only at the native height. A
// taller button becomes a flat square with no accent, so there the height must not change.
bool hasFixedButtonHeight(const QStyle *style)
{
    return style->name().compare(QLatin1StringView("macos"), Qt::CaseInsensitive) == 0;
}

// The primary button of the empty state (ui-design 5.2, audit M15): one type step bigger, with
// two times the padding. On a style with a fixed button height, two times the side padding only.
class LargeButton : public QPushButton
{
public:
    using QPushButton::QPushButton;

    QSize sizeHint() const override { return withPadding(QPushButton::sizeHint()); }
    QSize minimumSizeHint() const override { return withPadding(QPushButton::minimumSizeHint()); }

private:
    QSize withPadding(QSize size) const
    {
        // The style padding is the part of the hint that is not text. Add it one more time.
        const QFontMetrics metrics = fontMetrics();
        const int textWidth = metrics.size(Qt::TextShowMnemonic, text()).width();
        const int sidePadding = qMax(0, size.width() - textWidth);
        const int verticalPadding = qMax(0, size.height() - metrics.height());
        if (!hasFixedButtonHeight(style()))
            return size + QSize(sidePadding, verticalPadding);
        return {size.width() + sidePadding, nativeHeight()};
    }

    // The height of a normal button: the bigger font must not make the native bezel taller.
    int nativeHeight() const
    {
        QStyleOptionButton option;
        initStyleOption(&option);
        const QFont base = parentWidget() ? parentWidget()->font() : QApplication::font();
        option.fontMetrics = QFontMetrics(base);
        const QSize label = option.fontMetrics.size(Qt::TextShowMnemonic, text());
        return style()->sizeFromContents(QStyle::CT_PushButton, &option, label, this).height();
    }
};

QLabel *centredLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    return label;
}

} // namespace

QStringList localDropPaths(const QMimeData *mime)
{
    QStringList paths;
    if (!mime || !mime->hasUrls())
        return paths;
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    return paths;
}

void paintDropFrame(QPainter &painter, const QRect &rect, const QPalette &palette,
                    const QString &text)
{
    painter.save();
    QPen pen(palette.color(QPalette::Highlight), 2, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect.adjusted(1, 1, -2, -2));
    if (!text.isEmpty()) {
        painter.setPen(palette.color(QPalette::Text));
        painter.drawText(rect, Qt::AlignCenter, text);
    }
    painter.restore();
}

EmptyState::EmptyState(const Actions &actions, QWidget *parent)
    : QWidget(parent)
    , m_icon(new QLabel(this))
    , m_title(centredLabel(tr("Drop files or folders here"), this))
    , m_addFiles(new LargeButton(tr("Add Files..."), this))
    , m_addFolder(new QPushButton(tr("Add Folder..."), this))
{
    setObjectName(QStringLiteral("emptyState"));
    setAcceptDrops(true);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);

    m_icon->setObjectName(QStringLiteral("emptyIcon"));
    m_icon->setAlignment(Qt::AlignCenter);
    m_title->setObjectName(QStringLiteral("emptyTitle"));
    m_addFiles->setObjectName(QStringLiteral("emptyAddFiles"));
    m_addFiles->setToolTip(actions.addFiles->toolTip());
    m_addFolder->setObjectName(QStringLiteral("emptyAddFolder"));
    m_addFolder->setToolTip(actions.addFolder->toolTip());
    m_addFolder->setAutoDefault(false);
    connect(m_addFiles, &QPushButton::clicked, actions.addFiles, &QAction::trigger);
    connect(m_addFolder, &QPushButton::clicked, actions.addFolder, &QAction::trigger);

    auto *note = centredLabel(tr("Nothing is renamed until you click Rename."), this);
    note->setObjectName(QStringLiteral("emptyNote"));
    // The only humour in the main window (ui-design 5.4), dimmed.
    auto *toast = centredLabel(tr("Nothing to toast yet."), this);
    toast->setObjectName(QStringLiteral("emptyToast"));
    toast->setForegroundRole(QPalette::PlaceholderText);

    // O07, design-tokens 4.1 and 4.6: space-6 around the content, space-2 between the buttons,
    // space-3 between the groups, space-1 between the two lines of the note.
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(tokens::space2);
    buttons->addStretch(1);
    buttons->addWidget(m_addFiles);
    buttons->addWidget(m_addFolder);
    buttons->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(tokens::space6, tokens::space6, tokens::space6, tokens::space6);
    layout->setSpacing(0);
    layout->addStretch(1);
    layout->addWidget(m_icon);
    layout->addSpacing(tokens::space3);
    layout->addWidget(m_title);
    layout->addSpacing(tokens::space3);
    layout->addLayout(buttons);
    layout->addSpacing(tokens::space3);
    layout->addWidget(note);
    layout->addSpacing(tokens::space1);
    layout->addWidget(toast);
    layout->addStretch(1);

    updateLook();
}

void EmptyState::updateLook()
{
    m_icon->setPixmap(appIcon().pixmap(QSize(kIconSize, kIconSize), devicePixelRatioF()));
    m_title->setFont(typeStyle(TypeToken::Display, font()));
    m_addFiles->setFont(typeStyle(TypeToken::Heading, font()));
    setPrimary(m_addFiles, true);
    setPrimary(m_addFolder, false);
}

void EmptyState::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
    case QEvent::FontChange:
        updateLook();
        break;
    default:
        break;
    }
}

void EmptyState::dragEnterEvent(QDragEnterEvent *event)
{
    if (localDropPaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    setDropCount(int(localDropPaths(event->mimeData()).size()));
}

void EmptyState::dragMoveEvent(QDragMoveEvent *event)
{
    if (m_dragOver) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

void EmptyState::dragLeaveEvent(QDragLeaveEvent *)
{
    setDropCount(0);
}

void EmptyState::dropEvent(QDropEvent *event)
{
    setDropCount(0);
    const QStringList paths = localDropPaths(event->mimeData());
    if (paths.isEmpty())
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit pathsDropped(paths);
}

// The drag-over text (ui-design 9.2) takes the place of the title, so it does not cover the
// buttons in the centre.
void EmptyState::setDropCount(int count)
{
    m_dragOver = count > 0;
    m_title->setText(m_dragOver ? numerus(tr("Drop to add %n item(s)", nullptr, count), count)
                                : tr("Drop files or folders here"));
    update();
}

void EmptyState::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (!m_dragOver)
        return;
    QPainter painter(this);
    paintDropFrame(painter, rect(), palette(), {});
}

} // namespace ctrm::app
