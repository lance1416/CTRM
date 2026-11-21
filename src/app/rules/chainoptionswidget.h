// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rulechain.h"

#include <QWidget>

class QCheckBox;
class QToolButton;

namespace ctrm::app {

// The "Chain options" area under the rule editor page (ui-design 8.10). Closed by default.
class ChainOptionsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChainOptionsWidget(QWidget *parent);
    void setOptions(const ctrm::ChainOptions &o); // does not emit optionsChanged()
    ctrm::ChainOptions options() const;
    void setOpen(bool open);
    // The height of the closed area: the gap with the separator line, and the header.
    int closedHeight() const;
    QWidget *lastOption() const; // the last check box

signals:
    void optionsChanged(const ctrm::ChainOptions &o);
    void openChanged(bool open);

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateHeader();
    void updateIcon();

    QToolButton *m_header;
    QWidget *m_top; // the separator and the header
    QWidget *m_separator;
    QWidget *m_body;
    QCheckBox *m_portableNames;
    QCheckBox *m_autoResolveDuplicates;
    QCheckBox *m_folderExtensions;
    bool m_loading = false;
};

} // namespace ctrm::app
