// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs/failuredialog.h"

#include "dialogs/dialoglayout.h"
#include "text/plural.h"
#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/typography.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace ctrm::app {

namespace {

constexpr int kMaxListedFailures = 10;

} // namespace

FailureDialog::FailureDialog(const FailureInfo &info, QWidget *parent)
    : QDialog(parent)
{
    setWindowModality(Qt::WindowModal);
    setWindowTitle(titleText(info));
    // No close button in the title bar: the user must choose.
    setWindowFlags((windowFlags() | Qt::CustomizeWindowHint) & ~Qt::WindowCloseButtonHint);

    auto *icon = new QLabel(this);
    const int iconSize = style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, this);
    icon->setPixmap(stateIcon(StateIcon::Error, CtrmColors::fromPalette(palette()))
                            .pixmap(iconSize, iconSize));
    icon->setAlignment(Qt::AlignTop);

    auto *title = new QLabel(titleText(info), this);
    title->setObjectName(QStringLiteral("failureTitle"));
    title->setFont(typeStyle(TypeToken::LabelStrong, font()));
    title->setWordWrap(true);
    auto *body = new QLabel(bodyText(info), this);
    body->setObjectName(QStringLiteral("failureText"));
    body->setTextFormat(Qt::PlainText);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *report = buttons->addButton(tr("Show Report"), QDialogButtonBox::HelpRole);
    report->setObjectName(QStringLiteral("showReport"));
    report->setAutoDefault(false);
    QPushButton *keep = buttons->addButton(tr("Keep Renamed Items"), QDialogButtonBox::ActionRole);
    keep->setObjectName(QStringLiteral("keep"));
    keep->setAutoDefault(false);
    QPushButton *rollBack = buttons->addButton(tr("Roll Back"), QDialogButtonBox::AcceptRole);
    rollBack->setObjectName(QStringLiteral("rollBack"));
    rollBack->setDefault(true);
    rollBack->setFocus();

    connect(report, &QPushButton::clicked, this, &FailureDialog::showReportClicked);
    connect(keep, &QPushButton::clicked, this, [this] {
        m_choice = FailureChoice::Keep;
        accept();
    });
    connect(rollBack, &QPushButton::clicked, this, [this] {
        m_choice = FailureChoice::RollBack;
        accept();
    });

    auto *text = new QVBoxLayout;
    text->addWidget(title);
    text->addWidget(body);
    auto *top = new QHBoxLayout;
    top->addWidget(icon, 0, Qt::AlignTop);
    top->addLayout(text, 1);
    QVBoxLayout *layout = makeDialogLayout(this);
    layout->addLayout(top);
    addButtonRow(layout, buttons);
    layout->setSizeConstraint(QLayout::SetFixedSize);
}

FailureChoice FailureDialog::choice() const
{
    return m_choice;
}

void FailureDialog::reject()
{
    // The app must not choose for the user (ui-design 9.6).
}

QString FailureDialog::titleText(const FailureInfo &info)
{
    return info.cancelled ? tr("Rename cancelled") : tr("The rename stopped.");
}

QString FailureDialog::bodyText(const FailureInfo &info)
{
    QStringList lines;
    QString counts = info.total == 1
                             ? tr("%1 of 1 item was renamed.").arg(info.done)
                             : tr("%1 of %2 items were renamed.").arg(info.done).arg(info.total);
    const int failed = int(info.failed.size());
    if (failed > 0)
        counts += u' ' + numerus(tr("%n item(s) failed:", nullptr, failed), failed);
    lines.append(counts);
    for (int i = 0; i < failed && i < kMaxListedFailures; ++i) {
        const ReportRow &row = info.failed.at(i);
        lines.append(QStringLiteral("    ") +
                     tr("%1: %2").arg(QFileInfo(row.oldPath).fileName(), row.errorText));
    }
    if (failed > kMaxListedFailures) {
        const int more = failed - kMaxListedFailures;
        lines.append(QStringLiteral("    ") + numerus(tr("%n more item(s)", nullptr, more), more));
    }
    lines.append(QString());
    lines.append(
            info.done == 1
                    ? tr("Roll back puts the item back to its old name.")
                    : tr("Roll back puts the %1 items back to their old names.").arg(info.done));
    return lines.join(u'\n');
}

} // namespace ctrm::app
