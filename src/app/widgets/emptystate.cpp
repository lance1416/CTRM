// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/emptystate.h"

#include "text/plural.h"
#include "theme/icons.h"
#include "theme/tokens.h"
#include "theme/typography.h"
#include "widgets/buttonstyle.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace ctrm::app {

namespace {

constexpr int kIconSize = 96;

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
    , m_addFiles(new QPushButton(tr("Add Files..."), this))
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
    // R5-05: one row of buttons, one height: control-h-large (32) for both (design-tokens 4).
    m_addFiles->setFixedHeight(tokens::controlHLarge);
    m_addFolder->setFixedHeight(tokens::controlHLarge);
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
    // R5-05: body size; the primary keeps its fill and the label-strong weight.
    m_addFiles->setFont(typeStyle(TypeToken::LabelStrong, font()));
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
