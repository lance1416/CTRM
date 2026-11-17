// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

namespace ctrm::app {

// About CTRM (ui-design 17): the app icon, the full name, the version and the licence. The
// About box is one of the places for the brand and its cheese yellow.
class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent);
};

} // namespace ctrm::app
