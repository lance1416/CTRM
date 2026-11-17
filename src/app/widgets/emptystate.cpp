// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/emptystate.h"

#include "theme/icons.h"
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
#include <QStyle>
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

// The primary button of the empty state (ui-design 5.2): one type step bigger, with two times
// the padding. On a style with a fixed button height: bold, and two times the side padding only.
class LargeButton : public QPushButton
{
public:
    using QPushButton::QPushButton;

    QSize sizeHint() const override { return withPadding(QPushButton::sizeHint()); }
    QSize minimumSizeHint() const override { return withPadding(QPushButton::minimumSizeHint()); }

private:
    QSize withPadding(QSize size) const
    {
        const int margin = style()->pixelMetric(QStyle::PM_ButtonMargin, nullptr, this);
        return size + QSize(2 * margin, hasFixedButtonHeight(style()) ? 0 : margin);
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

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(m_addFiles);
    buttons->addWidget(m_addFolder);
    buttons->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->addStretch(1);
    layout->addWidget(m_icon);
    layout->addWidget(m_title);
    layout->addSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing, nullptr, this));
    layout->addLayout(buttons);
    layout->addSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing, nullptr, this));
    layout->addWidget(note);
    layout->addWidget(toast);
    layout->addStretch(1);

    updateLook();
}

void EmptyState::updateLook()
{
    m_icon->setPixmap(appIcon().pixmap(QSize(kIconSize, kIconSize), devicePixelRatioF()));
    m_title->setFont(typeStyle(TypeToken::Display, font()));
    m_addFiles->setFont(typeStyle(hasFixedButtonHeight(m_addFiles->style()) ? TypeToken::LabelStrong
                                                                            : TypeToken::Heading,
                                  font()));
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
    m_dragOver = true;
    update();
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
    m_dragOver = false;
    update();
}

void EmptyState::dropEvent(QDropEvent *event)
{
    m_dragOver = false;
    update();
    const QStringList paths = localDropPaths(event->mimeData());
    if (paths.isEmpty())
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit pathsDropped(paths);
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
