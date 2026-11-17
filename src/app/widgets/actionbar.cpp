// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/actionbar.h"

#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/typography.h"
#include "widgets/buttonstyle.h"
#include "widgets/tertiarybutton.h"

#include <QAccessible>
#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include <optional>

namespace ctrm::app {

namespace {

constexpr int kProgressDelayMs = 1000;
constexpr int kAnnounceDelayMs = 500;

QToolButton *problemButton(QAction *action, Qt::ArrowType arrow, const QString &objectName,
                           QWidget *parent)
{
    auto *button = new TertiaryButton(parent);
    button->setObjectName(objectName);
    button->setDefaultAction(action);
    button->setArrowType(arrow);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
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
    , m_previousProblem(problemButton(actions.previousProblem, Qt::LeftArrow,
                                      QStringLiteral("previousProblemButton"), m_normalPage))
    , m_nextProblem(problemButton(actions.nextProblem, Qt::RightArrow,
                                  QStringLiteral("nextProblemButton"), m_normalPage))
    , m_reasonIcon(new QLabel(m_normalPage))
    , m_reason(new QLabel(m_normalPage))
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
    m_summaryLayout->setContentsMargins(0, 0, 0, 0);
    m_reasonIcon->setObjectName(QStringLiteral("renameReasonIcon"));
    m_reason->setObjectName(QStringLiteral("renameReason"));
    m_reason->setTextFormat(Qt::PlainText);
    connect(m_skip, &QPushButton::clicked, actions.skipAndRename, &QAction::trigger);
    connect(m_rename, &QPushButton::clicked, actions.rename, &QAction::trigger);

    auto *normal = new QHBoxLayout(m_normalPage);
    normal->setContentsMargins(0, 0, 0, 0);
    normal->addWidget(m_summaryWidget);
    normal->addWidget(m_previousProblem);
    normal->addWidget(m_nextProblem);
    normal->addStretch(1);
    normal->addWidget(m_reasonIcon);
    normal->addWidget(m_reason);
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
    progress->addWidget(m_progressLabel);
    progress->addWidget(m_progressBar, 1);
    progress->addWidget(m_cancel);

    m_pages->addWidget(m_normalPage);
    m_pages->addWidget(m_progressPage);
    auto *layout = new QHBoxLayout(this);
    layout->addWidget(m_pages);

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
    m_reason->setText(info.reason);
    m_reason->setVisible(showReason);
    m_reasonIcon->setVisible(showReason);

    updateSummary(summary); // sets m_summary, which the reason icon uses
    updateReasonIcon();
    updateAccent();
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
    const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    auto addLabel = [&](QLabel *label) {
        label->setFont(font);
        m_summaryLayout->addWidget(label);
        m_summaryParts.append(label);
    };

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

    // The screen reader announces the summary 500 ms after the last change (ui-design 5.5).
    const QString text = countSummaryText(summary);
    if (m_summaryWidget->accessibleName() != text) {
        m_summaryWidget->setAccessibleName(text);
        m_announceTimer->start();
    }
}

void ActionBar::updateReasonIcon()
{
    const int size = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
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
        updateReasonIcon();
        updateSummary(m_summary);
        updateAccent();
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
