// SPDX-License-Identifier: GPL-3.0-or-later
#include "chainoptionswidget.h"

#include "theme/typography.h"
#include "widgets/formutils.h"
#include "widgets/tertiarybutton.h"

#include <QCheckBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace ctrm::app {

ChainOptionsWidget::ChainOptionsWidget(QWidget *parent)
    : QWidget(parent)
    , m_header(new TertiaryButton)
    , m_body(new QWidget)
    , m_form(makeForm(m_body))
    , m_portableNames(new QCheckBox(tr("Use portable names (check Windows rules on all systems)")))
    , m_autoResolveDuplicates(new QCheckBox(tr("Add a number to duplicate names: x (2).txt")))
    , m_folderExtensions(new QCheckBox(tr("Treat folder names as name + extension")))
{
    m_header->setObjectName(QStringLiteral("chainOptionsHeader"));
    m_portableNames->setObjectName(QStringLiteral("portableNames"));
    m_autoResolveDuplicates->setObjectName(QStringLiteral("autoResolveDuplicates"));
    m_folderExtensions->setObjectName(QStringLiteral("folderExtensions"));

    m_header->setCheckable(true);
    m_header->setAutoRaise(true);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setArrowType(Qt::RightArrow);
    // The body font, not the small tool button font of macOS (M19).
    m_header->setFont(typeStyle(TypeToken::Body, font()));

    m_form->setContentsMargins(0, 0, 0, 0);
    for (QCheckBox *check : {m_portableNames, m_autoResolveDuplicates, m_folderExtensions}) {
        // An empty label keeps a label column, so LabelColumnSync can align the check boxes
        // with the field column of the rule editors (ui-design 8.10).
        m_form->addRow(new QLabel, check);
        connect(check, &QCheckBox::toggled, this, [this] {
            updateHeader();
            if (!m_loading)
                emit optionsChanged(options());
        });
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_header, 0, Qt::AlignLeft);
    layout->addWidget(m_body);

    connect(m_header, &QToolButton::toggled, this, &ChainOptionsWidget::setOpen);
    setOptions(ctrm::ChainOptions{});
    setOpen(false);
}

void ChainOptionsWidget::setOptions(const ctrm::ChainOptions &o)
{
    m_loading = true;
    m_portableNames->setChecked(o.portableNames);
    m_autoResolveDuplicates->setChecked(o.autoResolveDuplicates);
    m_folderExtensions->setChecked(o.folderExtensions);
    m_loading = false;
    updateHeader();
}

ctrm::ChainOptions ChainOptionsWidget::options() const
{
    return ctrm::ChainOptions{m_portableNames->isChecked(), m_autoResolveDuplicates->isChecked(),
                              m_folderExtensions->isChecked()};
}

void ChainOptionsWidget::setOpen(bool open)
{
    m_header->setChecked(open);
    m_header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    m_body->setVisible(open);
}

void ChainOptionsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        m_header->setFont(typeStyle(TypeToken::Body, font()));
}

QFormLayout *ChainOptionsWidget::form() const
{
    return m_form;
}

void ChainOptionsWidget::updateHeader()
{
    const ctrm::ChainOptions defaults;
    const ctrm::ChainOptions current = options();
    const int changed = int(current.portableNames != defaults.portableNames) +
                        int(current.autoResolveDuplicates != defaults.autoResolveDuplicates) +
                        int(current.folderExtensions != defaults.folderExtensions);
    const QString text =
            changed == 0 ? tr("Chain options") : tr("Chain options (%n changed)", nullptr, changed);
    m_header->setText(text);
    m_header->setAccessibleName(text);
}

} // namespace ctrm::app
