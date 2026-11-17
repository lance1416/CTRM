// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs/folderdropdialog.h"

#include "theme/typography.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ctrm::app {

FolderDropDialog::FolderDropDialog(const FolderDropQuestion &question, QWidget *parent)
    : QDialog(parent)
    , m_includeHidden(new QCheckBox(tr("Include hidden items"), this))
    , m_dontAskAgain(new QCheckBox(tr("Do not ask again"), this))
{
    setWindowModality(Qt::WindowModal);

    const qsizetype count = question.folders.size();
    const QString titleText = count == 1
                                      ? tr("You added the folder \"%1\".").arg(question.folders[0])
                                      : tr("You added %n folders.", nullptr, int(count));
    setWindowTitle(titleText);

    auto *title = new QLabel(titleText, this);
    title->setFont(typeStyle(TypeToken::LabelStrong, font()));
    title->setWordWrap(true);
    auto *questionLabel = new QLabel(tr("What do you want to rename?"), this);
    questionLabel->setWordWrap(true);
    auto *info = new QLabel(tr("Add Its Contents adds the items directly in the folder. It does "
                               "not add items from subfolders."),
                            this);
    info->setFont(typeStyle(TypeToken::Caption, font()));
    info->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(QDialogButtonBox::Cancel);
    QPushButton *renameButton =
            buttons->addButton(tr("Rename the Folder Itself"), QDialogButtonBox::ActionRole);
    QPushButton *contentsButton =
            buttons->addButton(tr("Add Its Contents"), QDialogButtonBox::AcceptRole);
    contentsButton->setDefault(true);
    contentsButton->setFocus();

    connect(renameButton, &QPushButton::clicked, this, [this] {
        m_choice = FolderDropReply::Choice::RenameFolder;
        accept();
    });
    connect(contentsButton, &QPushButton::clicked, this, [this] {
        m_choice = FolderDropReply::Choice::AddContents;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(questionLabel);
    layout->addWidget(info);
    layout->addWidget(m_includeHidden);
    layout->addWidget(m_dontAskAgain);
    layout->addWidget(buttons);
    layout->setSizeConstraint(QLayout::SetFixedSize);
}

FolderDropReply FolderDropDialog::reply() const
{
    FolderDropReply r;
    r.choice = result() == QDialog::Accepted ? m_choice : FolderDropReply::Choice::Cancel;
    r.includeHidden = m_includeHidden->isChecked();
    r.dontAskAgain = m_dontAskAgain->isChecked();
    return r;
}

FolderDropReply FolderDropDialog::ask(const FolderDropQuestion &question, QWidget *parent)
{
    if (!parent)
        parent = QApplication::activeWindow();
    FolderDropDialog dialog(question, parent);
    // The answer is needed before the items are added, so the dialog waits here. With a parent,
    // a window-modal dialog shows as a sheet on macOS.
    dialog.exec();
    return dialog.reply();
}

} // namespace ctrm::app
