// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs/preferencesdialog.h"

#include "settings.h"
#include "theme/typography.h"
#include "widgets/formutils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ctrm::app {

namespace {

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(typeStyle(TypeToken::LabelStrong, parent->font()));
    return label;
}

QSpinBox *makeSpin(const char *name, int maximum)
{
    auto *spin = new QSpinBox;
    spin->setObjectName(QLatin1StringView(name));
    spin->setRange(1, maximum);
    spin->setKeyboardTracking(false); // save the typed number, not each digit
    return spin;
}

} // namespace

PreferencesDialog::ApplyMode PreferencesDialog::platformMode()
{
#ifdef Q_OS_MACOS
    return ApplyMode::Immediately;
#else
    return ApplyMode::OnAccept;
#endif
}

PreferencesDialog::PreferencesDialog(Settings &settings, ApplyMode mode, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_mode(mode)
    , m_folderDrop(new QComboBox)
    , m_includeHidden(new QCheckBox(tr("Include hidden items")))
    , m_highlight(new QCheckBox(tr("Highlight changed characters")))
    , m_keepCount(makeSpin("retentionCount", 100000))
    , m_keepDays(makeSpin("retentionDays", 3650))
{
#ifdef Q_OS_WIN
    setWindowTitle(tr("Options"));
#else
    setWindowTitle(tr("Preferences"));
#endif
    m_folderDrop->setObjectName(QStringLiteral("folderDropAnswer"));
    m_includeHidden->setObjectName(QStringLiteral("includeHidden"));
    m_highlight->setObjectName(QStringLiteral("highlightChanged"));
    m_folderDrop->addItem(tr("Ask"), int(FolderDropAnswer::Ask));
    m_folderDrop->addItem(tr("Rename the folder itself"), int(FolderDropAnswer::RenameFolder));
    m_folderDrop->addItem(tr("Add its contents"), int(FolderDropAnswer::AddContents));

    auto *layout = new QVBoxLayout(this);
    auto *sync = new LabelColumnSync(this);
    const auto addSection = [&](const QString &title) {
        layout->addWidget(sectionTitle(title, this));
        auto *page = new QWidget;
        QFormLayout *form = makeForm(page);
        form->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(page);
        sync->addForm(form);
        return form;
    };

    QFormLayout *general = addSection(tr("General"));
    general->addRow(tr("When a folder is added:"), m_folderDrop);
    general->addRow(new QLabel, m_includeHidden);

    QFormLayout *preview = addSection(tr("Preview"));
    preview->addRow(new QLabel, m_highlight);

    QFormLayout *history = addSection(tr("History"));
    history->addRow(tr("Keep at least:"),
                    makeInlineField(m_keepCount, {new QLabel(tr("batches"))}));
    history->addRow(tr("Keep for:"), makeInlineField(m_keepDays, {new QLabel(tr("days"))}));
    sync->sync(); // one field edge for the three sections

    if (m_mode == ApplyMode::OnAccept) {
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->setObjectName(QStringLiteral("buttons"));
        connect(buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }
    layout->setSizeConstraint(QLayout::SetFixedSize);

    connect(m_folderDrop, &QComboBox::currentIndexChanged, this,
            &PreferencesDialog::controlChanged);
    connect(m_includeHidden, &QCheckBox::toggled, this, &PreferencesDialog::controlChanged);
    connect(m_highlight, &QCheckBox::toggled, this, &PreferencesDialog::controlChanged);
    connect(m_keepCount, &QSpinBox::valueChanged, this, &PreferencesDialog::controlChanged);
    connect(m_keepDays, &QSpinBox::valueChanged, this, &PreferencesDialog::controlChanged);

    load();
}

void PreferencesDialog::load()
{
    m_loading = true;
    m_folderDrop->setCurrentIndex(m_folderDrop->findData(int(m_settings.folderDropAnswer())));
    m_includeHidden->setChecked(m_settings.includeHidden());
    m_highlight->setChecked(m_settings.highlightChanged());
    m_keepCount->setValue(m_settings.retentionCount());
    m_keepDays->setValue(m_settings.retentionDays());
    m_loading = false;
}

void PreferencesDialog::accept()
{
    if (m_mode == ApplyMode::OnAccept)
        save();
    QDialog::accept();
}

void PreferencesDialog::done(int result)
{
    QDialog::done(result);
    load(); // after Cancel, the controls show the saved values again
}

void PreferencesDialog::syncHighlight()
{
    m_loading = true;
    m_highlight->setChecked(m_settings.highlightChanged());
    m_loading = false;
}

void PreferencesDialog::save()
{
    m_settings.setFolderDropAnswer(FolderDropAnswer(m_folderDrop->currentData().toInt()));
    m_settings.setIncludeHidden(m_includeHidden->isChecked());
    m_settings.setHighlightChanged(m_highlight->isChecked());
    m_settings.setRetentionCount(m_keepCount->value());
    m_settings.setRetentionDays(m_keepDays->value());
    emit settingsChanged();
}

void PreferencesDialog::controlChanged()
{
    if (!m_loading && m_mode == ApplyMode::Immediately)
        save();
}

} // namespace ctrm::app
