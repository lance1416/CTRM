// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;

namespace ctrm::app {

struct FolderDropQuestion
{
    QStringList folders; // display names
};

struct FolderDropReply
{
    enum class Choice { Cancel, RenameFolder, AddContents } choice = Choice::Cancel;
    bool includeHidden = false; // applies to AddContents only
    bool dontAskAgain = false;
};

// The folder question of ui-design 9.9: rename the folders themselves or add their contents.
class FolderDropDialog : public QDialog
{
    Q_OBJECT
public:
    FolderDropDialog(const FolderDropQuestion &question, QWidget *parent);

    // The answer after the dialog closed. Cancel when the user closed it any other way.
    FolderDropReply reply() const;

    // Shows the dialog window-modal (a sheet on macOS) and waits for the answer.
    static FolderDropReply ask(const FolderDropQuestion &question, QWidget *parent);

private:
    QCheckBox *m_includeHidden;
    QCheckBox *m_dontAskAgain;
    FolderDropReply::Choice m_choice = FolderDropReply::Choice::Cancel;
};

} // namespace ctrm::app
