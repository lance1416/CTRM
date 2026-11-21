// SPDX-License-Identifier: GPL-3.0-or-later
#include "managepresetsdialog.h"

#include "dialogs/dialoglayout.h"
#include "rules/rulesummary.h"
#include "theme/typography.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace ctrm::app {

ManagePresetsDialog::ManagePresetsDialog(PresetStore &store, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_list(new QListWidget)
    , m_rules(new QLabel)
    , m_rename(new QPushButton(tr("&Rename...")))
    , m_duplicate(new QPushButton(tr("D&uplicate")))
    , m_delete(new QPushButton(tr("&Delete")))
    , m_showFile(new QPushButton(tr("Show &File")))
{
    setWindowTitle(tr("Manage Presets"));
    m_list->setObjectName(QStringLiteral("presetList"));
    m_rules->setObjectName(QStringLiteral("ruleSummaries"));
    m_rename->setObjectName(QStringLiteral("renameButton"));
    m_duplicate->setObjectName(QStringLiteral("duplicateButton"));
    m_delete->setObjectName(QStringLiteral("deleteButton"));
    m_showFile->setObjectName(QStringLiteral("showFileButton"));

    askName = [this](const QString &current) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename Preset"), tr("Name:"),
                                                   QLineEdit::Normal, current, &ok);
        return ok ? name.trimmed() : QString();
    };
    confirmDelete = [this](const QString &name) {
        QMessageBox box(QMessageBox::Question, tr("Delete Preset"),
                        tr("Delete the preset \"%1\"?").arg(name), QMessageBox::Cancel, this);
        QPushButton *del = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        return box.clickedButton() == del;
    };
    showError = [this](const QString &text) {
        QMessageBox::warning(this, tr("Manage Presets"), text);
    };

    m_rules->setWordWrap(true);
    m_rules->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_rules->setTextFormat(Qt::PlainText);
    auto *rulesTitle = new QLabel(tr("Rules in this preset:"));
    auto *right = new QVBoxLayout;
    right->addWidget(rulesTitle);
    right->addWidget(m_rules, 1);
    auto *top = new QHBoxLayout;
    top->addWidget(m_list, 1);
    top->addLayout(right, 1);

    auto *buttons = new QHBoxLayout;
    for (QPushButton *b : {m_rename, m_duplicate, m_delete, m_showFile}) {
        b->setAutoDefault(false);
        buttons->addWidget(b);
    }
    buttons->addStretch();

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton *close = box->button(QDialogButtonBox::Close);
    close->setObjectName(QStringLiteral("closeButton"));
    close->setDefault(true);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *layout = makeDialogLayout(this);
    layout->addLayout(top, 1);
    layout->addLayout(buttons);
    addButtonRow(layout, box);
    // The focus at open: no ring before the user acts (ui-design 14, focus ring rule 6). After the
    // layout: the list is in the dialog only now.
    m_list->setFocus();

    connect(m_list, &QListWidget::currentRowChanged, this, &ManagePresetsDialog::updateSelection);
    connect(m_rename, &QPushButton::clicked, this, &ManagePresetsDialog::rename);
    connect(m_duplicate, &QPushButton::clicked, this, &ManagePresetsDialog::duplicate);
    connect(m_delete, &QPushButton::clicked, this, &ManagePresetsDialog::remove);
    connect(m_showFile, &QPushButton::clicked, this, &ManagePresetsDialog::showFile);

    reload(QString());
}

void ManagePresetsDialog::reload(const QString &selectPath)
{
    m_entries = m_store.userPresets() + m_store.builtIns();
    m_list->clear();
    QListWidgetItem *select = nullptr;
    bool builtInHeader = false;
    // Audit S08: two groups, each under a dimmed header that the user cannot select.
    const auto addHeader = [this](const QString &text) {
        auto *header = new QListWidgetItem(text, m_list);
        header->setFlags(Qt::NoItemFlags);
        header->setFont(typeStyle(TypeToken::LabelStrong, m_list->font()));
    };
    if (!m_store.userPresets().isEmpty())
        addHeader(tr("My Presets"));
    for (int i = 0; i < m_entries.size(); ++i) {
        const PresetEntry &e = m_entries.at(i);
        if (e.builtIn && !builtInHeader) {
            addHeader(tr("Built-in"));
            builtInHeader = true;
        }
        auto *item = new QListWidgetItem(e.name, m_list);
        item->setData(Qt::UserRole, i);
        if (e.builtIn)
            item->setToolTip(tr("Built-in. Duplicate it to change it."));
        if (!select || (!selectPath.isEmpty() && e.filePath == selectPath))
            select = item;
    }
    m_list->setCurrentItem(select);
    updateSelection();
}

std::optional<PresetEntry> ManagePresetsDialog::selected() const
{
    const QListWidgetItem *item = m_list->currentItem();
    if (!item || !item->data(Qt::UserRole).isValid())
        return std::nullopt;
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_entries.size())
        return std::nullopt;
    return m_entries.at(index);
}

void ManagePresetsDialog::updateSelection()
{
    const std::optional<PresetEntry> entry = selected();
    const bool user = entry && !entry->builtIn;
    m_rename->setEnabled(user);
    m_delete->setEnabled(user);
    m_showFile->setEnabled(user);
    m_duplicate->setEnabled(entry.has_value());
    if (!entry) {
        m_rules->clear();
        return;
    }
    const PresetParseResult result = m_store.load(*entry);
    if (!result.preset) {
        m_rules->setText(result.error);
        return;
    }
    QStringList lines;
    const RuleChain &chain = result.preset->chain;
    for (int i = 0; i < chain.size(); ++i)
        lines.append(QStringLiteral("%1 %2").arg(i + 1).arg(ruleSummary(*chain.at(i))));
    m_rules->setText(lines.isEmpty() ? tr("No rules") : lines.join(QLatin1Char('\n')));
}

void ManagePresetsDialog::rename()
{
    const std::optional<PresetEntry> entry = selected();
    if (!entry || entry->builtIn)
        return;
    const QString name = askName(entry->name);
    if (name.isEmpty() || name == entry->name)
        return;
    QString error;
    const std::optional<PresetEntry> renamed = m_store.rename(*entry, name, &error);
    if (!renamed) {
        showError(error);
        return;
    }
    emit presetRenamed(*entry, *renamed);
    reload(renamed->filePath);
}

void ManagePresetsDialog::duplicate()
{
    const std::optional<PresetEntry> entry = selected();
    if (!entry)
        return;
    QString error;
    const std::optional<PresetEntry> copy = m_store.duplicate(*entry, &error);
    if (!copy) {
        showError(error);
        return;
    }
    reload(copy->filePath);
}

void ManagePresetsDialog::remove()
{
    const std::optional<PresetEntry> entry = selected();
    if (!entry || entry->builtIn || !confirmDelete(entry->name))
        return;
    QString error;
    if (!m_store.remove(*entry, &error)) {
        showError(error);
        return;
    }
    emit presetRemoved(*entry);
    reload(QString());
}

void ManagePresetsDialog::showFile()
{
    const std::optional<PresetEntry> entry = selected();
    if (!entry || entry->builtIn)
        return;
    // Opens the folder that holds the file.
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(entry->filePath).absolutePath()));
}

} // namespace ctrm::app
