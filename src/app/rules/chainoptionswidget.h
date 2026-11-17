// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rulechain.h"

#include <QWidget>

class QCheckBox;
class QFormLayout;
class QToolButton;

namespace ctrm::app {

// The "Chain options" area at the bottom of the rule panel (ui-design 8.10). Closed by default.
class ChainOptionsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChainOptionsWidget(QWidget *parent);
    void setOptions(const ctrm::ChainOptions &o); // does not emit optionsChanged()
    ctrm::ChainOptions options() const;
    void setOpen(bool open);
    // The form of the check boxes, for LabelColumnSync.
    QFormLayout *form() const;

signals:
    void optionsChanged(const ctrm::ChainOptions &o);

private:
    void updateHeader();

    QToolButton *m_header;
    QWidget *m_body;
    QFormLayout *m_form;
    QCheckBox *m_portableNames;
    QCheckBox *m_autoResolveDuplicates;
    QCheckBox *m_folderExtensions;
    bool m_loading = false;
};

} // namespace ctrm::app
