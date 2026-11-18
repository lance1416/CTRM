// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs/savepresetdialog.h"

#include "dialogs/dialoglayout.h"
#include "widgets/buttonstyle.h"
#include "widgets/formutils.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ctrm::app {

SavePresetDialog::SavePresetDialog(const QStringList &userNames, const QStringList &builtInNames,
                                   const QString &suggested, QWidget *parent)
    : QDialog(parent)
    , m_userNames(userNames)
    , m_builtInNames(builtInNames)
    , m_edit(new QLineEdit(suggested, this))
    , m_form(new QFormLayout)
    , m_note(new QLabel(this))
    , m_save(nullptr)
{
    setObjectName(QStringLiteral("savePresetDialog"));
    setWindowTitle(tr("Save Preset"));
    m_edit->setObjectName(QStringLiteral("presetNameEdit"));
    m_edit->selectAll();
    m_note->setObjectName(QStringLiteral("presetNameNote"));
    m_note->setTextFormat(Qt::PlainText);
    m_note->setWordWrap(true);
    m_note->setForegroundRole(QPalette::PlaceholderText);

    m_form->addRow(tr("Preset name:"), m_edit);
    m_form->addRow(QString(), m_note); // the dimmed line under the field
    // The macOS style keeps fields at their size hint: the note must fill the field column.
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    applyFormRhythm(m_form);
    // The name field is field-w-max wide, so a normal name shows in full, and the note wraps at the
    // field width (ui-design 11.3, audit R6-07). The rhythm keeps a fixed width.
    m_edit->setFixedWidth(m_edit->maximumWidth());

    auto *box = new QDialogButtonBox(this);
    QPushButton *cancel = box->addButton(QDialogButtonBox::Cancel);
    cancel->setObjectName(QStringLiteral("cancelButton"));
    m_save = box->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
    m_save->setObjectName(QStringLiteral("saveButton"));
    setPrimary(m_save, true);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *layout = makeDialogLayout(this);
    layout->addLayout(m_form);
    addButtonRow(layout, box);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    connect(m_edit, &QLineEdit::textChanged, this, &SavePresetDialog::updateState);
    updateState();
}

QString SavePresetDialog::name() const
{
    return m_edit->text().trimmed();
}

void SavePresetDialog::updateState()
{
    const QString current = name();
    const bool builtIn = m_builtInNames.contains(current, Qt::CaseInsensitive);
    const bool exists = !builtIn && m_userNames.contains(current, Qt::CaseInsensitive);
    m_save->setEnabled(!current.isEmpty() && !builtIn);
    m_save->setText(exists ? tr("Replace") : tr("Save"));
    if (builtIn)
        m_note->setText(tr("This is a built-in preset name. Use a different name."));
    else if (exists)
        m_note->setText(tr("A preset with this name exists. Replace replaces it."));
    m_form->setRowVisible(m_note, builtIn || exists);
}

} // namespace ctrm::app
