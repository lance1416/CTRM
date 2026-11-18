// SPDX-License-Identifier: GPL-3.0-or-later
#include "chainoptionswidget.h"

#include "theme/icons.h"
#include "theme/tokens.h"
#include "theme/typography.h"
#include "widgets/formutils.h"
#include "widgets/tertiarybutton.h"

#include <QCheckBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace ctrm::app {

ChainOptionsWidget::ChainOptionsWidget(QWidget *parent)
    : QWidget(parent)
    , m_header(new TertiaryButton)
    , m_top(new QWidget)
    , m_separator(new QWidget)
    , m_body(new QWidget)
    , m_form(makeForm(m_body))
    , m_portableNames(new QCheckBox)
    , m_autoResolveDuplicates(new QCheckBox)
    , m_folderExtensions(new QCheckBox)
{
    m_header->setObjectName(QStringLiteral("chainOptionsHeader"));
    m_portableNames->setObjectName(QStringLiteral("portableNames"));
    m_autoResolveDuplicates->setObjectName(QStringLiteral("autoResolveDuplicates"));
    m_folderExtensions->setObjectName(QStringLiteral("folderExtensions"));

    m_header->setCheckable(true);
    m_header->setAutoRaise(true);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // A 12 px chevron from the icon set, not a style arrow (O04, ui-design 8.10).
    m_header->setIconSize(QSize(tokens::iconXs, tokens::iconXs));
    // The body font, not the small tool button font of macOS (M19).
    m_header->setFont(typeStyle(TypeToken::Body, font()));

    // The labels wrap: they never make the column wider (O08, C06). One group: space-1 between
    // the rows.
    auto *group = new QWidget;
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(tokens::space1);
    const QList<std::pair<QCheckBox *, QString>> checks{
            {m_portableNames, tr("Use portable names (check Windows rules on all systems)")},
            {m_autoResolveDuplicates, tr("Add a number to duplicate names: x (2).txt")},
            {m_folderExtensions, tr("Treat folder names as name + extension")}};
    for (const auto &[check, text] : checks)
        groupLayout->addWidget(makeWrappingCheckBox(check, text));
    // An empty label keeps a label column, so LabelColumnSync can align the check boxes with the
    // field column of the rule editors (ui-design 8.10).
    m_form->addRow(new QLabel, group);
    for (QCheckBox *check : {m_portableNames, m_autoResolveDuplicates, m_folderExtensions}) {
        connect(check, &QCheckBox::toggled, this, [this] {
            updateHeader();
            if (!m_loading)
                emit optionsChanged(options());
        });
    }

    applyFormRhythm(m_form); // the rhythm of the rule editors (C03)

    // The gap to the editor is space-4 with a 1 px separator line in the middle (ui-design 8.10).
    m_separator->setObjectName(QStringLiteral("chainOptionsSeparator"));
    // A plain widget: the macOS style gives a QFrame line layout margins.
    m_separator->setFixedHeight(1);
    m_separator->setAutoFillBackground(true);
    m_separator->setBackgroundRole(QPalette::Mid); // separator (design-tokens 2)

    auto *top = new QVBoxLayout(m_top);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(0);
    top->addSpacing(tokens::space2);
    top->addWidget(m_separator);
    top->addSpacing(tokens::space4 - tokens::space2 - 1);
    top->addWidget(m_header, 0, Qt::AlignLeft);
    m_form->setContentsMargins(0, tokens::space2, 0, 0); // under the header

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_top);
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
    m_body->setVisible(open);
    updateIcon();
}

int ChainOptionsWidget::openHeight() const
{
    // The current geometry, not the hints: while the layout changes, the splitter height still
    // has the old open height in it.
    return std::max(0, height() - m_top->height());
}

void ChainOptionsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        m_header->setFont(typeStyle(TypeToken::Body, font()));
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        updateIcon();
}

void ChainOptionsWidget::updateIcon()
{
    // text-dim (design-tokens 2): the placeholder colour, as the dim captions.
    QPalette dim = palette();
    dim.setColor(QPalette::WindowText, dim.color(QPalette::PlaceholderText));
    m_header->setIcon(toolIcon(m_header->isChecked() ? QStringLiteral("chevron-xs-down")
                                                     : QStringLiteral("chevron-xs-right"),
                               dim));
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
