// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs/reportproblemdialog.h"

#include "dialogs/dialoglayout.h"
#include "theme/tokens.h"
#include "widgets/buttonstyle.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ctrm::app {

ReportProblemDialog::ReportProblemDialog(const QString &info, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("reportProblemDialog"));
    setWindowTitle(tr("Report a Problem"));

    auto *text = new QLabel(tr("Open the issue page in your browser and tell us what happened. "
                               "Add this information to the report. CTRM sends nothing itself."),
                            this);
    text->setObjectName(QStringLiteral("reportProblemText"));
    text->setWordWrap(true);

    auto *details = new QPlainTextEdit(info, this);
    details->setObjectName(QStringLiteral("reportProblemInfo"));
    details->setReadOnly(true);
    details->setTabChangesFocus(true);
    const int lines = int(info.count(u'\n')) + 2;
    // The document margin is space-1 above and under the text.
    details->setFixedHeight(details->fontMetrics().lineSpacing() * lines +
                            2 * details->frameWidth() + 2 * tokens::space1);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *copy = buttons->addButton(tr("Copy Information"), QDialogButtonBox::ActionRole);
    copy->setObjectName(QStringLiteral("copyInformation"));
    copy->setAutoDefault(false);
    QPushButton *open = buttons->addButton(tr("Open Issue Page"), QDialogButtonBox::AcceptRole);
    open->setObjectName(QStringLiteral("openIssuePage"));
    setPrimary(open, true);
    QPushButton *close = buttons->addButton(QDialogButtonBox::Close);
    close->setAutoDefault(false);

    connect(copy, &QPushButton::clicked, this,
            [info] { QApplication::clipboard()->setText(info); });
    connect(open, &QPushButton::clicked, this, [this] {
        emit openIssuePageClicked();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *layout = makeDialogLayout(this);
    layout->addWidget(text);
    layout->addWidget(details);
    addButtonRow(layout, buttons);
    resize(qMax(440, sizeHint().width()), sizeHint().height());
}

} // namespace ctrm::app
