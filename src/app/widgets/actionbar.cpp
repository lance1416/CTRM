// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/actionbar.h"

#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/tokens.h"
#include "theme/typography.h"
#include "widgets/buttonstyle.h"
#include "widgets/tertiarybutton.h"

#include <QAccessible>
#include <QAction>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <optional>

namespace ctrm::app {

namespace {

constexpr int kProgressDelayMs = 1000;
constexpr int kAnnounceDelayMs = 500;

// The designer icon of a problem button, in the palette of the button (C07, design-tokens 5).
void applyProblemIcon(QToolButton *button)
{
    button->setIcon(toolIcon(button->property("ctrmIcon").toString(), button->palette()));
}

QToolButton *problemButton(QAction *action, const QString &iconName, const QString &objectName,
                           QWidget *parent)
{
    auto *button = new TertiaryButton(parent);
    button->setObjectName(objectName);
    button->setDefaultAction(action);
    button->setIconButton(true);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // C07: hit-min with an icon-s icon, as all icon buttons.
    button->setFixedSize(tokens::hitMin, tokens::hitMin);
    button->setIconSize(QSize(tokens::iconS, tokens::iconS));
    button->setProperty("ctrmIcon", iconName);
    applyProblemIcon(button);
    // The button copies the (empty) action icon each time the action changes; the action has no
    // icon, so that it shows none in the menu.
    QObject::connect(action, &QAction::changed, button, [button] { applyProblemIcon(button); });
    button->setAccessibleName(QString(action->text()).remove(QLatin1Char('&')));
    button->setAccessibleDescription(action->toolTip());
    return button;
}

QPushButton *pushButton(const QString &objectName, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(objectName);
    button->setAutoDefault(false);
    return button;
}

} // namespace

ActionBar::ActionBar(const Actions &actions, QWidget *parent)
    : QWidget(parent)
    , m_actions(actions)
    , m_pages(new QStackedWidget(this))
    , m_normalPage(new QWidget(m_pages))
    , m_progressPage(new QWidget(m_pages))
    , m_summaryWidget(new QWidget(m_normalPage))
    , m_summaryLayout(new QHBoxLayout(m_summaryWidget))
    , m_previousProblem(problemButton(actions.previousProblem, QStringLiteral("tool-left"),
                                      QStringLiteral("previousProblemButton"), m_normalPage))
    , m_nextProblem(problemButton(actions.nextProblem, QStringLiteral("tool-right"),
                                  QStringLiteral("nextProblemButton"), m_normalPage))
    , m_reasonIcon(new QLabel(m_normalPage))
    , m_reason(new QLabel(m_normalPage))
    , m_tipText(new QLabel(m_normalPage))
    , m_addNumbering(new TertiaryButton(m_normalPage))
    , m_skip(pushButton(QStringLiteral("skipButton"), m_normalPage))
    , m_rename(pushButton(QStringLiteral("renameButton"), m_normalPage))
    , m_progressLabel(new QLabel(m_progressPage))
    , m_progressBar(new QProgressBar(m_progressPage))
    , m_cancel(pushButton(QStringLiteral("cancelButton"), m_progressPage))
    , m_progressTimer(new QTimer(this))
    , m_announceTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("actionBar"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // Normal page: summary on the left, reason, Skip and Rename on the right.
    m_summaryWidget->setObjectName(QStringLiteral("countSummary"));
    // R2-01: the counts never shrink below their size.
    m_summaryWidget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    m_summaryLayout->setContentsMargins(0, 0, 0, 0);
    m_reasonIcon->setObjectName(QStringLiteral("renameReasonIcon"));
    m_reason->setObjectName(QStringLiteral("renameReason"));
    m_reason->setTextFormat(Qt::PlainText);
    m_tipText->setObjectName(QStringLiteral("conflictTipText"));
    m_tipText->setTextFormat(Qt::PlainText);
    m_tipText->setText(tr("Same names:"));
    m_tipText->setToolTip(tr("The new names are the same. Add a Numbering rule, or turn on \"Add "
                             "a number to duplicate names\" in Chain options."));
    // A link-style tertiary button: a fix, not a main action (5.1).
    m_addNumbering->setObjectName(QStringLiteral("addNumberingButton"));
    m_addNumbering->setAutoRaise(true);
    m_addNumbering->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_addNumbering->setForegroundRole(QPalette::Link);
    connect(m_addNumbering, &QToolButton::clicked, this, &ActionBar::addNumberingClicked);
    updateLinkColor();
    connect(m_skip, &QPushButton::clicked, actions.skipAndRename, &QAction::trigger);
    connect(m_rename, &QPushButton::clicked, actions.rename, &QAction::trigger);

    // design-tokens 4.1: space-2 between items; space-1 between the two icon buttons (C07).
    auto *problems = new QHBoxLayout;
    problems->setContentsMargins(0, 0, 0, 0);
    problems->setSpacing(tokens::space1);
    problems->addWidget(m_previousProblem);
    problems->addWidget(m_nextProblem);
    auto *normal = new QHBoxLayout(m_normalPage);
    normal->setContentsMargins(0, 0, 0, 0);
    normal->setSpacing(tokens::space2);
    normal->addWidget(m_summaryWidget);
    normal->addLayout(problems);
    normal->addStretch(1);
    normal->addWidget(m_reasonIcon);
    normal->addWidget(m_reason);
    normal->addWidget(m_tipText);
    normal->addWidget(m_addNumbering);
    normal->addWidget(m_skip);
    normal->addWidget(m_rename);

    // Progress page (ui-design 9.5).
    m_progressLabel->setObjectName(QStringLiteral("progressLabel"));
    m_progressLabel->setTextFormat(Qt::PlainText);
    m_progressBar->setObjectName(QStringLiteral("progressBar"));
    m_progressBar->setTextVisible(false);
    m_cancel->setText(tr("Cancel"));
    connect(m_cancel, &QPushButton::clicked, actions.cancelBatch, &QAction::trigger);
    auto *progress = new QHBoxLayout(m_progressPage);
    progress->setContentsMargins(0, 0, 0, 0);
    progress->setSpacing(tokens::space2);
    progress->addWidget(m_progressLabel);
    progress->addWidget(m_progressBar, 1);
    progress->addWidget(m_cancel);

    m_pages->addWidget(m_normalPage);
    m_pages->addWidget(m_progressPage);
    // O07, design-tokens 4.2: inset space-3 on all sides.
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(tokens::space3, tokens::space3, tokens::space3, tokens::space3);
    layout->setSpacing(0);
    layout->addWidget(m_pages);
    updateFixedLook();

    m_progressTimer->setObjectName(QStringLiteral("progressTimer"));
    m_progressTimer->setSingleShot(true);
    m_progressTimer->setTimerType(Qt::PreciseTimer);
    m_progressTimer->setInterval(kProgressDelayMs);
    connect(m_progressTimer, &QTimer::timeout, this, &ActionBar::revealProgress);

    m_announceTimer->setObjectName(QStringLiteral("announceTimer"));
    m_announceTimer->setSingleShot(true);
    m_announceTimer->setInterval(kAnnounceDelayMs);
    connect(m_announceTimer, &QTimer::timeout, this, [this] {
        QAccessibleEvent event(m_summaryWidget, QAccessible::NameChanged);
        QAccessible::updateAccessibility(&event);
    });

    setState(computeWindowState({}), {});
    m_announceTimer->stop(); // nothing to announce at start
}

QPushButton *ActionBar::renameButton() const
{
    return m_rename;
}

QPushButton *ActionBar::skipButton() const
{
    return m_skip;
}

void ActionBar::setState(const WindowStateInfo &info, const ctrm::PreviewSummary &summary)
{
    m_info = info;
    const bool running = info.state == WindowState::Running;

    m_rename->setText(info.renameText);
    m_rename->setEnabled(info.renameEnabled);
    m_rename->setVisible(!running);
    if (info.renameEnabled) {
        m_rename->setToolTip(m_actions.rename->toolTip());
        m_rename->setAccessibleDescription(QString());
    } else {
        m_rename->setToolTip(info.reason);
        m_rename->setAccessibleDescription(info.reason);
    }
    m_actions.rename->setEnabled(info.renameEnabled);

    m_skip->setText(info.skipText);
    m_skip->setVisible(info.skipVisible && !running);
    m_skip->setToolTip(m_actions.skipAndRename->toolTip());
    m_skip->setAccessibleDescription(m_actions.skipAndRename->toolTip());
    m_actions.skipAndRename->setEnabled(info.skipVisible && !running);

    const bool showReason = !running && !info.renameEnabled && !info.reason.isEmpty();
    m_fullReason = showReason ? info.reason : QString();
    m_reasonIcon->setVisible(showReason);

    updateSummary(summary); // sets m_summary, which the reason icon uses
    updateReasonIcon();
    updateAccent();
    updateTip();
}

void ActionBar::setConflictTip(bool shown, int offNumberingRule)
{
    m_tipWanted = shown;
    m_addNumbering->setText(offNumberingRule > 0 ? tr("Turn On Rule %1").arg(offNumberingRule)
                                                 : tr("Add Numbering"));
    updateTip();
}

void ActionBar::updateTip()
{
    const bool shown = m_tipWanted && m_info.state == WindowState::Blocked;
    m_addNumbering->setVisible(shown);
    m_tipText->setVisible(shown);
    m_reason->setText(m_fullReason);
    m_reason->setVisible(!m_fullReason.isEmpty());
    m_reasonIcon->setVisible(!m_fullReason.isEmpty());
    m_reason->setToolTip(QString());
    m_reasonIcon->setToolTip(QString());
    if (!isVisible())
        return; // the width is not final yet; showEvent fits the bar

    // R2-01 (5.2, 9.3): when the bar is too narrow, it gives up space in this order: the tip
    // text ("Same names:"; the link stays), then the reason text (elided, then hidden), then the
    // reason icon. The count summary, the problem buttons, Skip and Rename keep their size.
    QLayout *page = m_normalPage->layout();
    const QMargins margins = layout()->contentsMargins();
    const int available = width() - margins.left() - margins.right();
    const auto over = [&] {
        page->invalidate();
        return page->sizeHint().width() - available;
    };
    if (over() <= 0)
        return;
    if (shown) {
        m_tipText->setVisible(false);
        if (over() <= 0)
            return;
    }
    if (m_fullReason.isEmpty())
        return;
    m_reason->setToolTip(m_fullReason);
    m_reasonIcon->setToolTip(m_fullReason);
    const QFontMetrics metrics(m_reason->font());
    const int room = metrics.horizontalAdvance(m_fullReason) - over();
    if (room >= 6 * metrics.averageCharWidth()) {
        m_reason->setText(metrics.elidedText(m_fullReason, Qt::ElideRight, room));
        if (over() <= 0)
            return;
    }
    m_reason->setVisible(false);
    // Last: the icon. The Rename tooltip and the summary icons still give the reason.
    if (over() > 0)
        m_reasonIcon->setVisible(false);
}

void ActionBar::updateLinkColor()
{
    QPalette link;
    link.setColor(QPalette::ButtonText, palette().color(QPalette::Link));
    m_addNumbering->setPalette(link);
}

void ActionBar::updateFixedLook()
{
    // R1-04: the link is on one line with "Same names:", so it has the body font, not the
    // smaller tool button font of some styles (macOS).
    const QFont body = m_tipText->font();
    QFont linkFont = m_addNumbering->font();
    linkFont.setFamilies(body.families());
    if (body.pointSizeF() > 0)
        linkFont.setPointSizeF(body.pointSizeF());
    else
        linkFont.setPixelSize(body.pixelSize());
    m_addNumbering->setFont(linkFont);

    // R1-03: the height of the page with all its controls, also the ones that only some states
    // show (the problem buttons, the tip, Skip). The layout adds the style margins of each
    // control (macOS), so measure the layout with all controls not hidden for a moment.
    QList<QWidget *> hidden;
    for (QWidget *child : m_normalPage->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
        if (child->isHidden()) {
            hidden.append(child);
            child->setHidden(false);
        }
    }
    m_normalPage->setMinimumHeight(0);
    m_normalPage->layout()->invalidate();
    const int height = m_normalPage->layout()->minimumSize().height();
    for (QWidget *child : std::as_const(hidden))
        child->setHidden(true);
    m_normalPage->setMinimumHeight(height);
}

void ActionBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateTip();
}

void ActionBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTip();
}

void ActionBar::updateSummary(const ctrm::PreviewSummary &summary)
{
    m_summary = summary;
    for (QWidget *w : std::as_const(m_summaryParts)) {
        w->hide();
        w->deleteLater();
    }
    m_summaryParts.clear();

    const CtrmColors colors = CtrmColors::fromPalette(palette());
    const QFont font = tabularFigures(this->font());
    const int iconSize = tokens::iconS;
    auto addLabel = [&](QLabel *label) {
        label->setFont(font);
        m_summaryLayout->addWidget(label);
        // R3-01: show it now, not later from the layout, so that updateTip() measures it.
        label->show();
        m_summaryParts.append(label);
    };

    if (m_info.state == WindowState::Result) {
        // ui-design 9.8 (M10): "42 renamed". The preview counts are of the new names.
        auto *renamed = new QLabel(m_summaryWidget);
        renamed->setTextFormat(Qt::PlainText);
        renamed->setText(summaryPartText(SummaryPart::Renamed, m_info.renamedCount));
        addLabel(renamed);
        m_previousProblem->hide();
        m_nextProblem->hide();
        announce(renamed->text());
        return;
    }

    struct Part
    {
        SummaryPart part;
        int count;
        const char *link; // empty: not a link
        std::optional<StateIcon> icon;
    };
    const Part parts[] = {
            {SummaryPart::WillChange, summary.willChange, "", std::nullopt},
            {SummaryPart::Conflicts, summary.conflicts, "problems", StateIcon::Conflict},
            {SummaryPart::Errors, summary.errors, "problems", StateIcon::Error},
            {SummaryPart::Warnings, summary.warnings, "warnings", StateIcon::Warning},
            {SummaryPart::Excluded, summary.excluded, "", std::nullopt},
            {SummaryPart::Unchanged, summary.unchanged, "", std::nullopt},
    };
    bool first = true;
    for (const Part &part : parts) {
        if (part.count <= 0)
            continue;
        if (!first)
            addLabel(new QLabel(QStringLiteral("·"), m_summaryWidget));
        first = false;
        if (part.icon) {
            auto *icon = new QLabel(m_summaryWidget);
            icon->setPixmap(stateIcon(*part.icon, colors)
                                    .pixmap(QSize(iconSize, iconSize), devicePixelRatioF()));
            addLabel(icon);
        }
        const QString text = summaryPartText(part.part, part.count);
        auto *label = new QLabel(m_summaryWidget);
        if (*part.link) {
            const QString link = QString::fromLatin1(part.link);
            label->setTextFormat(Qt::RichText);
            label->setText(QStringLiteral("<a href=\"%1\">%2</a>").arg(link, text.toHtmlEscaped()));
            label->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                           Qt::LinksAccessibleByKeyboard);
            label->setAccessibleName(text);
            connect(label, &QLabel::linkActivated, this, [this](const QString &href) {
                if (href == QLatin1StringView("warnings"))
                    emit warningsLinkClicked();
                else
                    emit problemLinkClicked();
            });
        } else {
            label->setTextFormat(Qt::PlainText);
            label->setText(text);
        }
        addLabel(label);
    }
    if (first) {
        auto *none = new QLabel(m_summaryWidget);
        none->setTextFormat(Qt::PlainText);
        none->setText(countSummaryText(summary));
        addLabel(none);
    }

    const bool problems = summary.blocked() > 0 && m_info.state != WindowState::Running;
    m_previousProblem->setVisible(problems);
    m_nextProblem->setVisible(problems);

    announce(countSummaryText(summary));
}

void ActionBar::announce(const QString &text)
{
    // The screen reader announces the summary 500 ms after the last change (ui-design 5.5).
    if (m_summaryWidget->accessibleName() != text) {
        m_summaryWidget->setAccessibleName(text);
        m_announceTimer->start();
    }
}

void ActionBar::updateReasonIcon()
{
    const int size = tokens::iconS;
    QIcon icon;
    if (m_info.state == WindowState::Blocked) {
        icon = stateIcon(m_summary.errors > 0 ? StateIcon::Error : StateIcon::Conflict,
                         CtrmColors::fromPalette(palette()));
    } else {
        icon = toolIcon(ToolIcon::Info, palette());
    }
    m_reasonIcon->setPixmap(icon.pixmap(QSize(size, size), devicePixelRatioF()));
}

void ActionBar::updateAccent()
{
    setPrimary(m_rename, m_info.state == WindowState::Ready && m_info.renameEnabled);
}

void ActionBar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
    case QEvent::FontChange:
        updateLinkColor();
        applyProblemIcon(m_previousProblem);
        applyProblemIcon(m_nextProblem);
        updateFixedLook();
        updateReasonIcon();
        updateSummary(m_summary);
        updateAccent();
        updateTip();
        break;
    default:
        break;
    }
}

void ActionBar::showProgress(int done, int total)
{
    const QLocale locale;
    m_progressLabel->setText(
            tr("Renaming %1 of %2...").arg(locale.toString(done), locale.toString(total)));
    m_progressLabel->setFont(tabularFigures(font()));
    m_progressBar->setRange(0, qMax(total, 0));
    m_progressBar->setValue(qBound(0, done, qMax(total, 0)));
    m_actions.cancelBatch->setEnabled(true);

    if (!m_progressClock.isValid()) {
        m_progressClock.start();
        m_progressTimer->start();
    } else if (m_progressClock.elapsed() >= kProgressDelayMs &&
               m_pages->currentWidget() != m_progressPage) {
        revealProgress();
    }
}

void ActionBar::revealProgress()
{
    m_progressTimer->stop();
    m_pages->setCurrentWidget(m_progressPage);
    m_cancel->setFocus(Qt::OtherFocusReason);
}

void ActionBar::hideProgress()
{
    m_progressTimer->stop();
    m_progressClock.invalidate();
    m_actions.cancelBatch->setEnabled(false);
    m_pages->setCurrentWidget(m_normalPage);
}

} // namespace ctrm::app
